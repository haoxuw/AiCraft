#include "server/python_bridge.h"

#ifdef __EMSCRIPTEN__
// ================================================================
// Web build stub: Python bridge is server-only, not available in WASM.
// All methods return safe defaults.
// ================================================================
namespace agentworld {
bool PythonBridge::init(const std::string&) { return false; }
void PythonBridge::shutdown() {}
BehaviorHandle PythonBridge::loadBehavior(const std::string&, std::string& err) {
	err = "Python not available in web build";
	return -1;
}
BehaviorAction PythonBridge::callDecide(BehaviorHandle, Entity&,
                                         const std::vector<NearbyEntity>&,
                                         const std::vector<NearbyBlock>&,
                                         float, std::string&, std::string& err) {
	err = "Python not available in web build";
	return {BehaviorAction::Idle};
}
std::string PythonBridge::getSource(BehaviorHandle) const { return ""; }
void PythonBridge::unloadBehavior(BehaviorHandle) {}
PythonBridge& pythonBridge() { static PythonBridge b; return b; }
BehaviorAction PythonBehavior::decide(BehaviorWorldView&) { return {BehaviorAction::Idle}; }
} // namespace agentworld
#else
// ================================================================
// Native build: full pybind11 implementation
// ================================================================
#include <pybind11/embed.h>
#include <pybind11/stl.h>
#include <cstdio>

namespace py = pybind11;

namespace agentworld {

// ================================================================
// pybind11 module: expose C++ types to Python behaviors
// ================================================================

// Python-visible entity info (what a behavior can see about nearby entities)
struct PyEntityInfo {
	EntityId id;
	std::string type_id;
	std::string category;
	float x, y, z;
	float distance;
	int hp;
};

// Python-visible action types (what a behavior can return)
struct PyAction {
	std::string type;    // "idle", "wander", "move_to", "follow", "flee", "attack"
	float x = 0, y = 0, z = 0;  // target position
	EntityId target_id = ENTITY_NONE;
	float speed = 2.0f;
	float param = 0.0f;
};

PYBIND11_EMBEDDED_MODULE(agentworld_engine, m) {
	m.doc() = "AgentWorld engine bridge — exposes world view to Python behaviors";

	py::class_<PyEntityInfo>(m, "EntityInfo")
		.def_readonly("id", &PyEntityInfo::id)
		.def_readonly("type_id", &PyEntityInfo::type_id)
		.def_readonly("category", &PyEntityInfo::category)
		.def_readonly("x", &PyEntityInfo::x)
		.def_readonly("y", &PyEntityInfo::y)
		.def_readonly("z", &PyEntityInfo::z)
		.def_readonly("distance", &PyEntityInfo::distance)
		.def_readonly("hp", &PyEntityInfo::hp);

	py::class_<PyAction>(m, "Action")
		.def(py::init<>())
		.def_readwrite("type", &PyAction::type)
		.def_readwrite("x", &PyAction::x)
		.def_readwrite("y", &PyAction::y)
		.def_readwrite("z", &PyAction::z)
		.def_readwrite("target_id", &PyAction::target_id)
		.def_readwrite("speed", &PyAction::speed)
		.def_readwrite("param", &PyAction::param);

	// Convenience action constructors
	m.def("Idle", []() {
		PyAction a; a.type = "idle"; return a;
	});
	m.def("Wander", [](float speed) {
		PyAction a; a.type = "wander"; a.speed = speed; return a;
	}, py::arg("speed") = 2.0f);
	m.def("MoveTo", [](float x, float y, float z, float speed) {
		PyAction a; a.type = "move_to"; a.x = x; a.y = y; a.z = z; a.speed = speed; return a;
	}, py::arg("x"), py::arg("y"), py::arg("z"), py::arg("speed") = 2.0f);
	m.def("Follow", [](EntityId target, float speed, float min_dist) {
		PyAction a; a.type = "follow"; a.target_id = target; a.speed = speed; a.param = min_dist; return a;
	}, py::arg("target"), py::arg("speed") = 2.0f, py::arg("min_distance") = 1.5f);
	m.def("Flee", [](EntityId target, float speed) {
		PyAction a; a.type = "flee"; a.target_id = target; a.speed = speed; return a;
	}, py::arg("target"), py::arg("speed") = 4.0f);
	m.def("BreakBlock", [](int x, int y, int z) {
		PyAction a; a.type = "break_block"; a.x = (float)x; a.y = (float)y; a.z = (float)z; return a;
	}, py::arg("x"), py::arg("y"), py::arg("z"));
}

// ================================================================
// PythonBridge implementation
// ================================================================

static PythonBridge s_bridge;
PythonBridge& pythonBridge() { return s_bridge; }

bool PythonBridge::init(const std::string& pythonPath) {
	if (m_initialized) return true;
	try {
		py::initialize_interpreter();
		// Add python/ directory to sys.path so behaviors can import agentworld
		py::module_::import("sys").attr("path").cast<py::list>().append(pythonPath);
		m_initialized = true;
		printf("[PythonBridge] Initialized. Python path: %s\n", pythonPath.c_str());
		return true;
	} catch (const py::error_already_set& e) {
		printf("[PythonBridge] Init failed: %s\n", e.what());
		return false;
	}
}

void PythonBridge::shutdown() {
	if (!m_initialized) return;
	m_behaviors.clear();
	py::finalize_interpreter();
	m_initialized = false;
	printf("[PythonBridge] Shutdown.\n");
}

BehaviorHandle PythonBridge::loadBehavior(const std::string& sourceCode, std::string& errorOut) {
	if (!m_initialized) {
		errorOut = "Python bridge not initialized";
		return -1;
	}

	try {
		// Compile the source code into a module
		py::dict globals = py::globals();
		py::dict locals;

		// Import the engine module so behaviors can use Idle(), Wander(), etc.
		py::exec("from agentworld_engine import *", globals);

		// Execute the behavior source code
		py::exec(sourceCode, globals, locals);

		// Check that decide() function exists
		if (!locals.contains("decide")) {
			errorOut = "Behavior must define a decide(self, world) function";
			return -1;
		}

		int handle = m_nextHandle++;
		auto& beh = m_behaviors[handle];
		beh.source = sourceCode;
		// Store the compiled module namespace (heap-allocate py::object)
		beh.moduleObj = new py::dict(locals);

		printf("[PythonBridge] Loaded behavior (handle=%d)\n", handle);
		return handle;

	} catch (const py::error_already_set& e) {
		errorOut = e.what();
		return -1;
	}
}

BehaviorAction PythonBridge::callDecide(BehaviorHandle handle,
                                         Entity& self,
                                         const std::vector<NearbyEntity>& nearby,
                                         const std::vector<NearbyBlock>& nearbyBlocks,
                                         float dt,
                                         std::string& goalOut,
                                         std::string& errorOut) {
	auto it = m_behaviors.find(handle);
	if (it == m_behaviors.end()) {
		errorOut = "Invalid behavior handle";
		return {BehaviorAction::Idle};
	}

	try {
		py::dict& ns = *static_cast<py::dict*>(it->second.moduleObj);

		// Build the world view for Python
		py::list pyNearby;
		for (auto& ne : nearby) {
			PyEntityInfo info;
			info.id = ne.id;
			info.type_id = ne.typeId;
			info.category = ne.category;
			info.x = ne.position.x;
			info.y = ne.position.y;
			info.z = ne.position.z;
			info.distance = ne.distance;
			info.hp = ne.hp;
			pyNearby.append(py::cast(info));
		}

		// Build self info
		py::dict pySelf;
		pySelf["id"] = self.id();
		pySelf["type_id"] = self.typeId();
		pySelf["x"] = self.position.x;
		pySelf["y"] = self.position.y;
		pySelf["z"] = self.position.z;
		pySelf["yaw"] = self.yaw;
		pySelf["hp"] = self.hp();
		pySelf["walk_speed"] = self.def().walk_speed;
		pySelf["on_ground"] = self.onGround;
		pySelf["goal"] = self.goalText;

		// Build nearby blocks list for Python
		py::list pyBlocks;
		for (auto& nb : nearbyBlocks) {
			py::dict block;
			block["x"] = nb.x;
			block["y"] = nb.y;
			block["z"] = nb.z;
			block["type"] = nb.typeId;
			block["distance"] = nb.distance;
			pyBlocks.append(block);
		}

		// Build world view
		py::dict pyWorld;
		pyWorld["nearby"] = pyNearby;
		pyWorld["blocks"] = pyBlocks;
		pyWorld["dt"] = dt;

		// Call decide(self, world)
		py::object decideFn = ns["decide"];
		py::object result = decideFn(pySelf, pyWorld);

		// Read goal from self dict (behavior may have modified it)
		if (pySelf.contains("goal"))
			goalOut = pySelf["goal"].cast<std::string>();

		// Convert Python action to C++ BehaviorAction
		if (result.is_none()) {
			return {BehaviorAction::Idle};
		}

		PyAction pyAction = result.cast<PyAction>();

		BehaviorAction action;
		if (pyAction.type == "idle") action.type = BehaviorAction::Idle;
		else if (pyAction.type == "wander") action.type = BehaviorAction::Wander;
		else if (pyAction.type == "move_to") action.type = BehaviorAction::MoveTo;
		else if (pyAction.type == "follow") action.type = BehaviorAction::Follow;
		else if (pyAction.type == "flee") action.type = BehaviorAction::Flee;
		else if (pyAction.type == "attack") action.type = BehaviorAction::Attack;
		else if (pyAction.type == "break_block") action.type = BehaviorAction::BreakBlock;
		else action.type = BehaviorAction::Idle;

		action.targetPos = {pyAction.x, pyAction.y, pyAction.z};
		action.targetEntity = pyAction.target_id;
		action.speed = pyAction.speed;
		action.param = pyAction.param;

		// For follow/flee: resolve target entity position
		if ((action.type == BehaviorAction::Follow || action.type == BehaviorAction::Flee)
		    && action.targetEntity != ENTITY_NONE) {
			for (auto& ne : nearby) {
				if (ne.id == action.targetEntity) {
					action.targetPos = ne.position;
					break;
				}
			}
		}

		return action;

	} catch (const py::error_already_set& e) {
		errorOut = e.what();
		return {BehaviorAction::Idle};
	}
}

std::string PythonBridge::getSource(BehaviorHandle handle) const {
	auto it = m_behaviors.find(handle);
	return it != m_behaviors.end() ? it->second.source : "";
}

void PythonBridge::unloadBehavior(BehaviorHandle handle) {
	auto it = m_behaviors.find(handle);
	if (it != m_behaviors.end()) {
		if (it->second.moduleObj)
			delete static_cast<py::dict*>(it->second.moduleObj);
		m_behaviors.erase(it);
	}
}

// ================================================================
// PythonBehavior::decide — runs the Python decide() function
// ================================================================

BehaviorAction PythonBehavior::decide(BehaviorWorldView& view) {
	auto& bridge = pythonBridge();
	if (!bridge.isInitialized()) {
		view.self.goalText = "Python not initialized";
		return {BehaviorAction::Idle};
	}

	// Convert behavior-level NearbyBlock to PythonBridge::NearbyBlock
	std::vector<PythonBridge::NearbyBlock> blocks;
	blocks.reserve(view.nearbyBlocks.size());
	for (auto& nb : view.nearbyBlocks)
		blocks.push_back({nb.pos.x, nb.pos.y, nb.pos.z, nb.typeId, nb.distance});

	std::string goal, error;
	auto action = bridge.callDecide(m_handle, view.self, view.nearbyEntities, blocks, view.dt, goal, error);

	if (!error.empty()) {
		view.self.goalText = "ERROR: " + error.substr(0, 60);
		view.self.hasError = true;
		view.self.errorText = error;
		return {BehaviorAction::Idle};
	}

	if (!goal.empty())
		view.self.goalText = goal;
	view.self.hasError = false;
	view.self.errorText.clear();

	return action;
}

} // namespace agentworld
#endif // __EMSCRIPTEN__
