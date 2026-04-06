#include "server/python_bridge.h"
#include <fstream>
#include <iterator>

#ifdef __EMSCRIPTEN__
// ================================================================
// Web build stub: Python bridge is server-only, not available in WASM.
// All methods return safe defaults.
// ================================================================
namespace modcraft {
bool PythonBridge::init(const std::string&) { return false; }
void PythonBridge::shutdown() {}
BehaviorHandle PythonBridge::loadBehavior(const std::string&, std::string& err) {
	err = "Python not available in web build";
	return -1;
}
BehaviorAction PythonBridge::callDecide(BehaviorHandle, Entity&,
                                         const std::vector<NearbyEntity>&,
                                         const std::vector<NearbyBlock>&,
                                         float, float, std::string&, std::string& err) {
	err = "Python not available in web build";
	return {BehaviorAction::Idle};
}
std::string PythonBridge::getSource(BehaviorHandle) const { return ""; }
void PythonBridge::unloadBehavior(BehaviorHandle) {}
PythonBridge& pythonBridge() { static PythonBridge b; return b; }
BehaviorAction PythonBehavior::decide(BehaviorWorldView&) { return {BehaviorAction::Idle}; }
bool loadWorldConfig(const std::string&, WorldPyConfig&) { return false; }
} // namespace modcraft
#else
// ================================================================
// Native build: full pybind11 implementation
// ================================================================
#include <pybind11/embed.h>
#include <pybind11/stl.h>
#include <cstdio>

namespace py = pybind11;

namespace modcraft {

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
	std::string type;    // "idle", "wander", "move_to", "follow", "flee", "attack", "drop_item"
	float x = 0, y = 0, z = 0;  // target position
	EntityId target_id = ENTITY_NONE;
	float speed = 2.0f;
	float param = 0.0f;
	std::string item_type;  // for drop_item
	int item_count = 1;     // for drop_item
};

PYBIND11_EMBEDDED_MODULE(modcraft_engine, m) {
	m.doc() = "ModCraft engine bridge — exposes world view to Python behaviors";

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
		.def_readwrite("param", &PyAction::param)
		.def_readwrite("item_type", &PyAction::item_type)
		.def_readwrite("item_count", &PyAction::item_count);

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
	m.def("DropItem", [](const std::string& itemType, int count) {
		PyAction a; a.type = "drop_item"; a.item_type = itemType; a.item_count = count; return a;
	}, py::arg("item_type"), py::arg("count") = 1);
	m.def("PickupItem", [](EntityId entityId) {
		PyAction a; a.type = "pickup_item"; a.target_id = entityId; return a;
	}, py::arg("entity_id"));
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
		// Add python/ directory to sys.path so behaviors can import modcraft
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

// Goal string synthesized from action type when the behavior doesn't provide one.
static const char* goalForAction(const std::string& actionType) {
	if (actionType == "idle")        return "Idle";
	if (actionType == "wander")      return "Wandering";
	if (actionType == "move_to")     return "Moving...";
	if (actionType == "follow")      return "Following";
	if (actionType == "flee")        return "Fleeing!";
	if (actionType == "break_block") return "Breaking block";
	if (actionType == "drop_item")   return "Dropping item";
	if (actionType == "pickup_item") return "Picking up";
	return "Active";
}

BehaviorHandle PythonBridge::loadBehavior(const std::string& sourceCode, std::string& errorOut) {
	if (!m_initialized) {
		errorOut = "Python bridge not initialized";
		return -1;
	}

	try {
		// Isolated namespace per behavior (one per entity instance).
		py::dict ns;

		// Import engine actions + Behavior base class into the namespace.
		py::exec("from modcraft_engine import *", ns);
		py::exec("from behavior_base import Behavior", ns);

		// Execute the behavior source code.
		py::exec(sourceCode, ns);

		// Find the Behavior subclass and instantiate it.
		// Uses Python introspection so behaviors don't need registration boilerplate.
		py::exec(R"(
import inspect as _i
_behavior_instance = None
for _n, _c in list(globals().items()):
    if _i.isclass(_c) and _c is not Behavior and issubclass(_c, Behavior):
        _behavior_instance = _c()
        break
del _i, _n, _c
)", ns);

		if (!ns.contains("_behavior_instance") || ns["_behavior_instance"].is_none()) {
			errorOut = "No Behavior subclass found — define a class that inherits from Behavior";
			return -1;
		}

		int handle = m_nextHandle++;
		auto& beh = m_behaviors[handle];
		beh.source = sourceCode;
		beh.moduleObj  = new py::dict(ns);
		beh.instanceObj = new py::object(ns["_behavior_instance"]);

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
                                         float timeOfDay,
                                         std::string& goalOut,
                                         std::string& errorOut) {
	auto it = m_behaviors.find(handle);
	if (it == m_behaviors.end()) {
		errorOut = "Invalid behavior handle";
		return {BehaviorAction::Idle};
	}

	try {
		py::object& instance = *static_cast<py::object*>(it->second.instanceObj);

		// Build the world view for Python — entities as dicts (consistent with blocks)
		py::list pyNearby;
		for (auto& ne : nearby) {
			py::dict info;
			info["id"] = ne.id;
			info["type_id"] = ne.typeId;
			info["category"] = ne.category;
			info["x"] = ne.position.x;
			info["y"] = ne.position.y;
			info["z"] = ne.position.z;
			info["distance"] = ne.distance;
			info["hp"] = ne.hp;
			pyNearby.append(info);
		}

		// Build self info — start with all entity props (custom fields set by server),
		// then override with authoritative C++ values so hardcoded fields are always correct.
		py::dict pySelf;
		for (auto& [key, val] : self.props()) {
			if (auto* s = std::get_if<std::string>(&val))
				pySelf[key.c_str()] = *s;
			else if (auto* iv = std::get_if<int>(&val))
				pySelf[key.c_str()] = *iv;
			else if (auto* fv = std::get_if<float>(&val))
				pySelf[key.c_str()] = *fv;
			else if (auto* bv = std::get_if<bool>(&val))
				pySelf[key.c_str()] = *bv;
		}
		pySelf["id"] = self.id();
		pySelf["type_id"] = self.typeId();
		pySelf["x"] = self.position.x;
		pySelf["y"] = self.position.y;
		pySelf["z"] = self.position.z;
		pySelf["yaw"] = self.yaw;
		pySelf["hp"] = self.hp();
		pySelf["walk_speed"] = self.def().walk_speed;
		pySelf["on_ground"] = self.onGround;
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
		pyWorld["time"] = timeOfDay;

		// Call instance.decide(entity, world) — returns (action, goal_str)
		py::object result = instance.attr("decide")(pySelf, pyWorld);

		// Validate tuple return: (action, goal_str)
		if (!py::isinstance<py::tuple>(result) || result.cast<py::tuple>().size() != 2) {
			goalOut = "ERROR: decide() must return (action, goal_str) — got " +
			          std::string(py::str(result));
			errorOut = goalOut;
			return {BehaviorAction::Idle};
		}
		py::tuple tup = result.cast<py::tuple>();
		py::object pyActionObj = tup[0];
		goalOut = tup[1].cast<std::string>();

		// Enforce non-empty goal (engine synthesizes if behavior forgot)
		PyAction pyAction;
		if (pyActionObj.is_none()) {
			pyAction.type = "idle";
		} else {
			pyAction = pyActionObj.cast<PyAction>();
		}
		if (goalOut.empty())
			goalOut = goalForAction(pyAction.type);

		BehaviorAction action;
		if (pyAction.type == "idle") action.type = BehaviorAction::Idle;
		else if (pyAction.type == "wander") action.type = BehaviorAction::Wander;
		else if (pyAction.type == "move_to") action.type = BehaviorAction::MoveTo;
		else if (pyAction.type == "follow") action.type = BehaviorAction::Follow;
		else if (pyAction.type == "flee") action.type = BehaviorAction::Flee;
		else if (pyAction.type == "attack") action.type = BehaviorAction::Attack;
		else if (pyAction.type == "break_block") action.type = BehaviorAction::BreakBlock;
		else if (pyAction.type == "drop_item") {
			action.type = BehaviorAction::DropItem;
			action.itemType = pyAction.item_type;
			action.itemCount = pyAction.item_count;
		}
		else if (pyAction.type == "pickup_item") action.type = BehaviorAction::PickupItem;
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
		if (it->second.instanceObj)
			delete static_cast<py::object*>(it->second.instanceObj);
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
	auto action = bridge.callDecide(m_handle, view.self, view.nearbyEntities, blocks, view.dt, view.timeOfDay, goal, error);

	if (!error.empty()) {
		view.self.goalText = "ERROR: " + error.substr(0, 80);
		view.self.hasError = true;
		view.self.errorText = error;
		return {BehaviorAction::Idle};
	}

	// Goal is always set by bridge (synthesized if behavior returned empty).
	view.self.goalText = goal;
	view.self.hasError = false;
	view.self.errorText.clear();

	return action;
}

// ================================================================
// World config loader — reads artifacts/worlds/*.py via pybind11
// ================================================================

bool loadWorldConfig(const std::string& filePath, WorldPyConfig& out) {
	auto& bridge = pythonBridge();
	if (!bridge.isInitialized()) return false;

	std::ifstream f(filePath);
	if (!f.is_open()) {
		printf("[WorldConfig] File not found: %s\n", filePath.c_str());
		return false;
	}
	std::string src((std::istreambuf_iterator<char>(f)), {});

	try {
		py::dict ns;
		py::exec(src.c_str(), ns);
		if (!ns.contains("world")) return false;

		py::dict w = ns["world"].cast<py::dict>();

		auto getStr = [&](py::dict& d, const char* k, const std::string& def) -> std::string {
			return d.contains(k) ? d[k].cast<std::string>() : def;
		};
		auto getFloat = [&](py::dict& d, const char* k, float def) -> float {
			return d.contains(k) ? (float)py::float_(d[k]) : def;
		};
		auto getInt = [&](py::dict& d, const char* k, int def) -> int {
			return d.contains(k) ? d[k].cast<int>() : def;
		};
		auto getBool = [&](py::dict& d, const char* k, bool def) -> bool {
			return d.contains(k) ? d[k].cast<bool>() : def;
		};

		// metadata
		out.name        = getStr(w, "name", "");
		out.description = getStr(w, "description", "");

		// terrain
		if (w.contains("terrain") && !w["terrain"].is_none()) {
			py::dict t = w["terrain"].cast<py::dict>();
			out.terrainType      = getStr(t, "type",               out.terrainType);
			out.surfaceY         = getFloat(t, "surface_y",         out.surfaceY);
			out.continentScale   = getFloat(t, "continent_scale",   out.continentScale);
			out.continentAmplitude = getFloat(t, "continent_amplitude", out.continentAmplitude);
			out.hillScale        = getFloat(t, "hill_scale",        out.hillScale);
			out.hillAmplitude    = getFloat(t, "hill_amplitude",    out.hillAmplitude);
			out.detailScale      = getFloat(t, "detail_scale",      out.detailScale);
			out.detailAmplitude  = getFloat(t, "detail_amplitude",  out.detailAmplitude);
			out.microScale       = getFloat(t, "micro_scale",       out.microScale);
			out.microAmplitude   = getFloat(t, "micro_amplitude",   out.microAmplitude);
			out.waterLevel       = getFloat(t, "water_level",       out.waterLevel);
			out.snowThreshold    = getFloat(t, "snow_threshold",    out.snowThreshold);
			out.dirtDepth        = getInt(t,   "dirt_depth",        out.dirtDepth);
		}

		// trees
		if (w.contains("trees") && !w["trees"].is_none()) {
			py::dict t = w["trees"].cast<py::dict>();
			out.treeDensity    = getFloat(t, "density",          out.treeDensity);
			out.trunkHeightMin = getInt(t,   "trunk_height_min", out.trunkHeightMin);
			out.trunkHeightMax = getInt(t,   "trunk_height_max", out.trunkHeightMax);
			out.leafRadius     = getInt(t,   "leaf_radius",      out.leafRadius);
		}

		// spawn
		if (w.contains("spawn") && !w["spawn"].is_none()) {
			py::dict s = w["spawn"].cast<py::dict>();
			out.spawnSearchX = getFloat(s, "search_x",   out.spawnSearchX);
			out.spawnSearchZ = getFloat(s, "search_z",   out.spawnSearchZ);
			out.spawnMinH    = getFloat(s, "min_height", out.spawnMinH);
			out.spawnMaxH    = getFloat(s, "max_height", out.spawnMaxH);
			// flat world fixed spawn
			if (s.contains("x")) out.spawnSearchX = getFloat(s, "x", out.spawnSearchX);
			if (s.contains("z")) out.spawnSearchZ = getFloat(s, "z", out.spawnSearchZ);
		}

		// chest (flat world)
		if (w.contains("chest") && !w["chest"].is_none()) {
			py::dict c = w["chest"].cast<py::dict>();
			out.chestOffsetX = getFloat(c, "offset_x", out.chestOffsetX);
			out.chestOffsetZ = getFloat(c, "offset_z", out.chestOffsetZ);
		}

		// village
		out.hasVillage = w.contains("village") && !w["village"].is_none();
		if (out.hasVillage) {
			py::dict v = w["village"].cast<py::dict>();
			out.villageOffsetX  = getFloat(v, "offset_x",        out.villageOffsetX);
			out.villageOffsetZ  = getFloat(v, "offset_z",        out.villageOffsetZ);
			out.clearingRadius  = getInt(v,   "clearing_radius", out.clearingRadius);
			out.wallBlock       = getStr(v,   "wall_block",      out.wallBlock);
			out.roofBlock       = getStr(v,   "roof_block",      out.roofBlock);
			out.floorBlock      = getStr(v,   "floor_block",     out.floorBlock);
			out.pathBlock       = getStr(v,   "path_block",      out.pathBlock);
			out.storyHeight     = getInt(v,   "story_height",    out.storyHeight);
			out.doorHeight      = getInt(v,   "door_height",     out.doorHeight);
			out.windowRow       = getInt(v,   "window_row",      out.windowRow);

			if (v.contains("houses")) {
				out.houses.clear();
				for (auto& h : v["houses"].cast<py::list>()) {
					WorldPyConfig::HouseLayout layout;
					if (py::isinstance<py::dict>(h)) {
						// Dict format: {"cx":0,"cz":0,"w":14,"d":14,"stories":2,"wall":"base:wood"}
						py::dict hd = h.cast<py::dict>();
						layout.cx      = hd["cx"].cast<int>();
						layout.cz      = hd["cz"].cast<int>();
						layout.w       = hd["w"].cast<int>();
						layout.d       = hd["d"].cast<int>();
						layout.stories = hd.contains("stories") ? hd["stories"].cast<int>() : 1;
						if (hd.contains("wall")) layout.wallBlock = hd["wall"].cast<std::string>();
						if (hd.contains("roof")) layout.roofBlock = hd["roof"].cast<std::string>();
					} else {
						// List format: [cx, cz, w, d, stories(optional)]
						auto hl = h.cast<py::list>();
						layout.cx      = hl[0].cast<int>();
						layout.cz      = hl[1].cast<int>();
						layout.w       = hl[2].cast<int>();
						layout.d       = hl[3].cast<int>();
						layout.stories = (py::len(hl) >= 5) ? hl[4].cast<int>() : 1;
					}
					out.houses.push_back(layout);
				}
			}
		}

		// mobs
		if (w.contains("mobs")) {
			out.mobs.clear();
			for (auto& m : w["mobs"].cast<py::list>()) {
				py::dict md = m.cast<py::dict>();
				WorldPyConfig::MobConfig mc;
				mc.type   = md["type"].cast<std::string>();
				mc.count  = md["count"].cast<int>();
				mc.radius = getFloat(md, "radius", 20.0f);
				out.mobs.push_back(mc);
			}
		}

		printf("[WorldConfig] Loaded from %s\n", filePath.c_str());
		return true;

	} catch (const std::exception& e) {
		printf("[WorldConfig] Error loading %s: %s\n", filePath.c_str(), e.what());
		return false;
	}
}

} // namespace modcraft
#endif // __EMSCRIPTEN__
