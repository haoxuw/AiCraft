#include "server/python_bridge.h"
#include "server/structure_blueprint.h"
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
                                         const std::vector<BlockSample>&,
                                         float, float, std::string&, std::string& err,
                                         BlockQueryFn) {
	err = "Python not available in web build";
	{ BehaviorAction a; a.type = BehaviorAction::Idle; return a; }
}
std::string PythonBridge::getSource(BehaviorHandle) const { return ""; }
void PythonBridge::unloadBehavior(BehaviorHandle) {}
PythonBridge& pythonBridge() { static PythonBridge b; return b; }
BehaviorAction PythonBehavior::decide(BehaviorWorldView&) { { BehaviorAction a; a.type = BehaviorAction::Idle; return a; } }
bool loadWorldConfig(const std::string&, WorldPyConfig&) { return false; }
bool loadStructureBlueprint(const std::string&, StructureBlueprint&) { return false; }
std::optional<BlockSlot> StructureBlueprintManager::firstMissingBlock(
	const Entity&, const std::function<std::string(int,int,int)>&) const { return std::nullopt; }
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

// Python-visible container reference (mirrors C++ Container struct)
struct PyContainer {
	int      kind      = 0;              // 0=Self, 1=Ground, 2=Entity, 3=Block
	EntityId entity_id = ENTITY_NONE;
	float    x = 0, y = 0, z = 0;       // block position (kind=Block)
};

// Python-visible action types (what a behavior can return)
struct PyAction {
	std::string type;    // "move", "relocate", "convert", "interact"
	float x = 0, y = 0, z = 0;  // target position (Move, Interact)
	float speed = 2.0f;

	// Relocate
	PyContainer relocate_from;
	PyContainer relocate_to;
	std::string item_id;
	int         item_count = 1;
	std::string equip_slot;

	// Convert
	std::string from_item;
	int         from_count = 1;
	std::string to_item;
	int         to_count   = 1;
	PyContainer convert_from;   // source container (default = Self)
	PyContainer convert_into;   // dest container (default = Self)
};

// Per-call state — set before callDecide(), cleared after.
// Safe for single-threaded agent processes (one callDecide at a time).
static std::function<std::string(int,int,int)> s_blockQueryFn;

// Block scan callback — set by agent_client before callDecide().
// Returns list of {type_id, x, y, z, distance} for blocks of a given type
// near the supplied origin (anchor for distance sorting + early-exit).
using ScanBlocksFn = std::function<std::vector<BlockSample>(const std::string&, glm::vec3, float, int)>;
static ScanBlocksFn s_scanBlocksFn;

// Default search anchor when Python passes near=None: the entity's current
// position. Set by callDecide() alongside s_scanBlocksFn so the binding
// can substitute it without needing access to `self`.
static glm::vec3 s_selfPos;

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

	py::class_<PyContainer>(m, "Container")
		.def(py::init<>())
		.def_readwrite("kind",      &PyContainer::kind)
		.def_readwrite("entity_id", &PyContainer::entity_id)
		.def_readwrite("x", &PyContainer::x)
		.def_readwrite("y", &PyContainer::y)
		.def_readwrite("z", &PyContainer::z);

	// Container factory functions — used as convert_from / convert_into / relocate_from / relocate_to
	m.def("Self",   []()                          { PyContainer c; c.kind = 0; return c; });
	m.def("Ground", []()                          { PyContainer c; c.kind = 1; return c; });
	m.def("Entity", [](EntityId id)               { PyContainer c; c.kind = 2; c.entity_id = id; return c; },
	      py::arg("entity_id"));
	m.def("Block",  [](float x, float y, float z) { PyContainer c; c.kind = 3; c.x = x; c.y = y; c.z = z; return c; },
	      py::arg("x"), py::arg("y"), py::arg("z"));

	py::class_<PyAction>(m, "Action")
		.def(py::init<>())
		.def_readwrite("type", &PyAction::type)
		.def_readwrite("x", &PyAction::x)
		.def_readwrite("y", &PyAction::y)
		.def_readwrite("z", &PyAction::z)
		.def_readwrite("speed", &PyAction::speed)
		// Relocate
		.def_readwrite("relocate_from", &PyAction::relocate_from)
		.def_readwrite("relocate_to",   &PyAction::relocate_to)
		.def_readwrite("item_id",       &PyAction::item_id)
		.def_readwrite("item_count",    &PyAction::item_count)
		.def_readwrite("equip_slot",    &PyAction::equip_slot)
		// Convert
		.def_readwrite("from_item",    &PyAction::from_item)
		.def_readwrite("from_count",   &PyAction::from_count)
		.def_readwrite("to_item",      &PyAction::to_item)
		.def_readwrite("to_count",     &PyAction::to_count)
		.def_readwrite("convert_from", &PyAction::convert_from)
		.def_readwrite("convert_into", &PyAction::convert_into);

	// Core action constructors — these are the only write primitives the server accepts.
	// High-level helpers (BreakBlock, StoreItem, PickupItem, DropItem) are Python wrappers
	// in python/actions.py that map to Relocate/Convert.
	m.def("Idle", []() {
		PyAction a; a.type = "idle"; return a;
	});
	m.def("Move", [](float x, float y, float z, float speed) {
		PyAction a; a.type = "move"; a.x = x; a.y = y; a.z = z; a.speed = speed; return a;
	}, py::arg("x"), py::arg("y"), py::arg("z"), py::arg("speed") = 2.0f);
	m.def("Relocate", [](PyContainer relocate_from, PyContainer relocate_to,
	                     const std::string& item_id, int count, const std::string& equip_slot) {
		PyAction a; a.type = "relocate";
		a.relocate_from = relocate_from; a.relocate_to = relocate_to;
		a.item_id = item_id; a.item_count = count; a.equip_slot = equip_slot;
		return a;
	}, py::arg("relocate_from") = PyContainer{}, py::arg("relocate_to") = PyContainer{},
	   py::arg("item_id") = "", py::arg("count") = 1, py::arg("equip_slot") = "");
	m.def("Convert", [](const std::string& from_item, int from_count,
	                    const std::string& to_item, int to_count,
	                    PyContainer convert_from, PyContainer convert_into) {
		PyAction a; a.type = "convert";
		a.from_item = from_item; a.from_count = from_count;
		a.to_item   = to_item;   a.to_count   = to_count;
		a.convert_from = convert_from;
		a.convert_into = convert_into;
		return a;
	}, py::arg("from_item") = "", py::arg("from_count") = 1,
	   py::arg("to_item") = "", py::arg("to_count") = 1,
	   py::arg("convert_from") = PyContainer{}, py::arg("convert_into") = PyContainer{});
	m.def("Interact", [](int x, int y, int z) {
		PyAction a; a.type = "interact"; a.x = (float)x; a.y = (float)y; a.z = (float)z; return a;
	}, py::arg("x"), py::arg("y"), py::arg("z"));

	// get_block(x, y, z) → str — query block type from the agent's local chunk cache.
	// Valid only inside decide(); returns "base:air" if called outside callDecide().
	// Use this for pathfinding or any logic that needs to probe arbitrary world positions.
	m.def("get_block", [](int x, int y, int z) -> std::string {
		if (s_blockQueryFn) return s_blockQueryFn(x, y, z);
		return "base:air";
	}, py::arg("x"), py::arg("y"), py::arg("z"),
	   "Query block type string at world position (x,y,z). Call only inside decide().");

	// scan_blocks(type_id, near=None, max_dist, max_results) → list of dicts
	// Targeted search: picks the nearest non-empty chunk (per the ChunkInfo
	// index) and scans its real data. `near` is the world-space anchor for
	// distance sorting; pass an entity's home/bed position to keep AI from
	// chasing dense matches across the map. Defaults to the calling
	// entity's position when omitted.
	m.def("scan_blocks", [](const std::string& typeId, py::object near,
	                        float maxDist, int maxResults) -> py::list {
		py::list result;
		if (!s_scanBlocksFn) return result;
		glm::vec3 origin = s_selfPos;
		if (!near.is_none()) {
			auto t = near.cast<std::tuple<float, float, float>>();
			origin = {std::get<0>(t), std::get<1>(t), std::get<2>(t)};
		}
		auto blocks = s_scanBlocksFn(typeId, origin, maxDist, maxResults);
		for (auto& b : blocks) {
			py::dict d;
			d["type"] = b.typeId;
			d["x"] = b.x;
			d["y"] = b.y;
			d["z"] = b.z;
			d["distance"] = b.distance;
			result.append(d);
		}
		return result;
	}, py::arg("type_id"), py::arg("near") = py::none(),
	   py::arg("max_dist") = 80.0f, py::arg("max_results") = 20,
	   "Find blocks of a specific type near `near` (default: self position).");

	// Expose C++ name constants as Python submodules so behaviors can use the
	// same identifiers as C++ code (LivingName.Chicken, BlockType.Stone, etc.).
	auto living = m.def_submodule("LivingName", "Living entity type IDs");
	living.attr("Player")       = LivingName::Player;
	living.attr("Pig")          = LivingName::Pig;
	living.attr("Chicken")      = LivingName::Chicken;
	living.attr("Dog")          = LivingName::Dog;
	living.attr("Villager")     = LivingName::Villager;
	living.attr("Cat")          = LivingName::Cat;
	living.attr("BraveChicken") = LivingName::BraveChicken;

	auto item = m.def_submodule("ItemName", "Item type IDs");
	item.attr("ItemEntity")  = ItemName::ItemEntity;
	item.attr("Jetpack")     = ItemName::Jetpack;
	item.attr("Parachute")   = ItemName::Parachute;
	item.attr("WoodPickaxe") = ItemName::WoodPickaxe;
	item.attr("StonePickaxe")= ItemName::StonePickaxe;
	item.attr("WoodAxe")     = ItemName::WoodAxe;
	item.attr("WoodShovel")  = ItemName::WoodShovel;
	item.attr("Apple")       = ItemName::Apple;
	item.attr("Bread")       = ItemName::Bread;
	item.attr("Egg")         = ItemName::Egg;

	auto block = m.def_submodule("BlockType", "Block type IDs");
	block.attr("Air")         = BlockType::Air;
	block.attr("Stone")       = BlockType::Stone;
	block.attr("Cobblestone") = BlockType::Cobblestone;
	block.attr("Dirt")        = BlockType::Dirt;
	block.attr("Grass")       = BlockType::Grass;
	block.attr("Sand")        = BlockType::Sand;
	block.attr("Water")       = BlockType::Water;
	block.attr("Wood")        = BlockType::Wood;
	block.attr("Log")         = BlockType::Log;
	block.attr("Leaves")      = BlockType::Leaves;
	block.attr("Snow")        = BlockType::Snow;
	block.attr("TNT")         = BlockType::TNT;
	block.attr("Wheat")       = BlockType::Wheat;
	block.attr("Wire")        = BlockType::Wire;
	block.attr("NANDGate")    = BlockType::NANDGate;
	block.attr("WheatSeeds")  = BlockType::WheatSeeds;
	block.attr("Chest")       = BlockType::Chest;
	block.attr("Planks")      = BlockType::Planks;
	block.attr("Bed")         = BlockType::Bed;
	block.attr("Fence")       = BlockType::Fence;
	block.attr("Farmland")    = BlockType::Farmland;
	block.attr("Stair")       = BlockType::Stair;
	block.attr("Glass")       = BlockType::Glass;
	block.attr("Door")        = BlockType::Door;
	block.attr("DoorOpen")    = BlockType::DoorOpen;
	block.attr("Portal")      = BlockType::Portal;
	block.attr("ArcaneStone") = BlockType::ArcaneStone;
	block.attr("SpawnPoint")  = BlockType::SpawnPoint;
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
		auto sys = py::module_::import("sys");
		// Add python/ directory to sys.path so behaviors can import modcraft
		sys.attr("path").cast<py::list>().append(pythonPath);
		// Force line-buffered stdout so Python print() flushes immediately
		// (needed when agent stdout is piped through to server log)
		sys.attr("stdout").attr("reconfigure")(py::arg("line_buffering") = true);

		// Cache LocalWorld and SelfEntity pydantic classes so we don't re-import
		// local_world on every callDecide() tick.
		auto localWorldMod = py::module_::import("local_world");
		m_localWorldClass = new py::object(localWorldMod.attr("LocalWorld"));
		m_selfEntityClass = new py::object(localWorldMod.attr("SelfEntity"));

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
	// Destroy cached pydantic class refs before finalizing interpreter
	delete static_cast<py::object*>(m_localWorldClass);  m_localWorldClass = nullptr;
	delete static_cast<py::object*>(m_selfEntityClass);  m_selfEntityClass = nullptr;
	py::finalize_interpreter();
	m_initialized = false;
	printf("[PythonBridge] Shutdown.\n");
}

// Helper: translate PyContainer → C++ Container
static Container pyContainerToC(const PyContainer& pc) {
	switch (pc.kind) {
	case 1:  return Container::ground();
	case 2:  return Container::entity(pc.entity_id);
	case 3:  return Container::block((int)pc.x, (int)pc.y, (int)pc.z);
	default: return Container::self();
	}
}

// Goal string synthesized from action type when the behavior doesn't provide one.
static const char* goalForAction(const std::string& actionType) {
	if (actionType == "move")     return "Moving...";
	if (actionType == "relocate") return "Relocating item";
	if (actionType == "convert")  return "Converting";
	if (actionType == "interact") return "Interacting";
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

		// Import engine actions + convenience wrappers + Behavior base class.
		py::exec("from modcraft_engine import *", ns);
		py::exec("from actions import *", ns);
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
                                         const std::vector<BlockSample>& blocks,
                                         float dt,
                                         float timeOfDay,
                                         std::string& goalOut,
                                         std::string& errorOut,
                                         BlockQueryFn blockQueryFn,
                                         ScanBlocksFn scanBlocksFn) {
	// Inject callbacks for this call so Python can query blocks.
	s_blockQueryFn = blockQueryFn ? std::move(blockQueryFn) : [](int,int,int){ return std::string("base:air"); };
	s_scanBlocksFn = std::move(scanBlocksFn);
	s_selfPos      = self.position;
	struct Cleanup { ~Cleanup() { s_blockQueryFn = nullptr; s_scanBlocksFn = nullptr; } } _cleanup;
	auto it = m_behaviors.find(handle);
	if (it == m_behaviors.end()) {
		errorOut = "Invalid behavior handle";
		{ BehaviorAction a; a.type = BehaviorAction::Idle; return a; }
	}

	try {
		py::object& instance = *static_cast<py::object*>(it->second.instanceObj);

		// Build the world view for Python — entities as dicts (consistent with blocks)
		py::list pyNearby;
		for (auto& ne : nearby) {
			py::dict info;
			info["id"] = ne.id;
			info["type_id"] = ne.typeId;
			info["kind"] = (ne.kind == EntityKind::Living) ? "living" : "item";
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
		// Expose ALL entity props so Python behaviors can read custom values
		// (home_x, chest_x, work_radius, etc.) via entity.get("prop_name")
		for (auto& [key, val] : self.props()) {
			if (auto* s = std::get_if<std::string>(&val)) pySelf[key.c_str()] = *s;
			else if (auto* f = std::get_if<float>(&val)) pySelf[key.c_str()] = *f;
			else if (auto* i = std::get_if<int>(&val)) pySelf[key.c_str()] = *i;
			else if (auto* b = std::get_if<bool>(&val)) pySelf[key.c_str()] = *b;
		}
		// Inventory: expose as {"item_id": count, ...} dict
		if (self.inventory) {
			py::dict pyInv;
			for (auto& [itemId, count] : self.inventory->items())
				pyInv[itemId.c_str()] = count;
			pySelf["inventory"] = pyInv;
		} else {
			pySelf["inventory"] = py::dict();
		}
		// Build block list from ChunkInfo samples (see docs/29_CHUNK_INFO.md).
		py::list pyBlocks;
		for (auto& b : blocks) {
			py::dict bd;
			bd["type"]     = b.typeId;
			bd["x"]        = b.x;
			bd["y"]        = b.y;
			bd["z"]        = b.z;
			bd["distance"] = b.distance;
			pyBlocks.append(bd);
		}

		// Build raw dicts (intermediate form — consumed by LocalWorld._from_raw /
		// SelfEntity._from_raw which produce the pydantic objects behaviors receive).
		py::dict pyWorld;
		pyWorld["nearby"] = pyNearby;
		pyWorld["blocks"] = pyBlocks;
		pyWorld["dt"] = dt;
		pyWorld["time"] = timeOfDay;

		// Goal is no longer injected — server handles navigation directly.
		pyWorld["goal"] = py::none();

		// Wrap in LocalWorld / SelfEntity pydantic objects.
		// _from_raw() uses model_construct() to skip validators (trusted C++ data).
		py::object& LocalWorldCls = *static_cast<py::object*>(m_localWorldClass);
		py::object& SelfEntityCls = *static_cast<py::object*>(m_selfEntityClass);
		py::object pyLocalWorld = LocalWorldCls.attr("_from_raw")(pyWorld);
		py::object pySelfEntity = SelfEntityCls.attr("_from_raw")(pySelf);

		// Call instance.decide(entity, world) — returns (action, goal_str)
		py::object result = instance.attr("decide")(pySelfEntity, pyLocalWorld);

		// Validate tuple return: (action, goal_str) or (action, goal_str, duration_seconds)
		if (!py::isinstance<py::tuple>(result)) {
			goalOut = "ERROR: decide() must return (action, goal_str[, duration]) — got " +
			          std::string(py::str(result));
			errorOut = goalOut;
			{ BehaviorAction a; a.type = BehaviorAction::Idle; return a; }
		}
		py::tuple tup = result.cast<py::tuple>();
		size_t tupSize = tup.size();
		if (tupSize < 2 || tupSize > 3) {
			goalOut = "ERROR: decide() must return 2- or 3-tuple — got " +
			          std::to_string(tupSize) + "-tuple";
			errorOut = goalOut;
			{ BehaviorAction a; a.type = BehaviorAction::Idle; return a; }
		}
		py::object pyActionObj = tup[0];
		goalOut = tup[1].cast<std::string>();

		// Optional 3rd element: duration (seconds) until next decide()
		float decideDuration = 0.25f;
		if (tupSize == 3) {
			decideDuration = tup[2].cast<float>();
			if (decideDuration < 0.02f) decideDuration = 0.02f;   // floor: 1 tick at 50 Hz
			if (decideDuration > 30.0f) decideDuration = 30.0f;   // ceiling: 30 seconds
		}

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
		action.targetPos = {pyAction.x, pyAction.y, pyAction.z};
		action.speed = pyAction.speed;

		if (pyAction.type == "move") {
			action.type = BehaviorAction::Move;
		} else if (pyAction.type == "relocate") {
			action.type        = BehaviorAction::Relocate;
			action.relocateFrom= pyContainerToC(pyAction.relocate_from);
			action.relocateTo  = pyContainerToC(pyAction.relocate_to);
			action.itemId      = pyAction.item_id;
			action.itemCount   = pyAction.item_count;
			action.equipSlot   = pyAction.equip_slot;
		} else if (pyAction.type == "convert") {
			action.type        = BehaviorAction::Convert;
			action.fromItem    = pyAction.from_item;
			action.fromCount   = pyAction.from_count;
			action.toItem      = pyAction.to_item;
			action.toCount     = pyAction.to_count;
			action.convertFrom = pyContainerToC(pyAction.convert_from);
			action.convertInto = pyContainerToC(pyAction.convert_into);
		} else if (pyAction.type == "interact") {
			action.type    = BehaviorAction::Interact;
			action.blockPos= {(int)pyAction.x, (int)pyAction.y, (int)pyAction.z};
		} else if (pyAction.type == "idle") {
			action.type = BehaviorAction::Idle;
		} else {
			// Unknown type — idle (no action)
			action.type = BehaviorAction::Idle;
		}

		action.decideDuration = decideDuration;
		return action;

	} catch (const py::error_already_set& e) {
		errorOut = e.what();
		fprintf(stderr, "[PythonBridge] decide() exception: %s\n", e.what());
		fflush(stderr);
		{ BehaviorAction a; a.type = BehaviorAction::Idle; return a; }
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
		{ BehaviorAction a; a.type = BehaviorAction::Idle; return a; }
	}

	std::string goal, error;
	auto action = bridge.callDecide(m_handle, view.self, view.nearbyEntities,
	                                view.chunkBlocks,
	                                view.dt, view.timeOfDay, goal, error,
	                                view.blockQueryFn, view.scanBlocksFn);

	if (!error.empty()) {
		view.self.goalText = "ERROR: " + error.substr(0, 80);
		view.self.hasError = true;
		view.self.errorText = error;
		{ BehaviorAction a; a.type = BehaviorAction::Idle; return a; }
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
						if (hd.contains("type")) layout.type      = hd["type"].cast<std::string>();
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
				if (md.contains("props")) {
					for (auto& [k, v] : md["props"].cast<py::dict>())
						mc.props[k.cast<std::string>()] = py::str(v).cast<std::string>();
				}
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

// ================================================================
// Structure blueprint loader — reads artifacts/structures/*.py via pybind11.
// Same pattern as loadWorldConfig() above.
// ================================================================

bool loadStructureBlueprint(const std::string& filePath, StructureBlueprint& out) {
	auto& bridge = pythonBridge();
	if (!bridge.isInitialized()) return false;

	std::ifstream f(filePath);
	if (!f.is_open()) {
		printf("[StructureBlueprint] File not found: %s\n", filePath.c_str());
		return false;
	}
	std::string src((std::istreambuf_iterator<char>(f)), {});

	try {
		py::dict ns;
		py::exec(src.c_str(), ns);
		if (!ns.contains("blueprint")) return false;

		py::dict bp = ns["blueprint"].cast<py::dict>();

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

		out.id           = getStr(bp, "id", "");
		out.display_name = getStr(bp, "display_name", "");
		out.regenerates  = getBool(bp, "regenerates", false);
		out.regen_interval_s = getFloat(bp, "regen_interval_s", 60.0f);

		// Anchor
		if (bp.contains("anchor")) {
			py::dict a = bp["anchor"].cast<py::dict>();
			out.anchor.block_type = getStr(a, "block_type", "base:root");
			out.anchor.hardness   = getInt(a, "hardness", 3);
			if (a.contains("offset")) {
				py::list off = a["offset"].cast<py::list>();
				out.anchor.offset = {off[0].cast<int>(), off[1].cast<int>(), off[2].cast<int>()};
			}
		}

		// Blocks
		if (bp.contains("blocks")) {
			for (auto& item : bp["blocks"].cast<py::list>()) {
				py::dict bd = item.cast<py::dict>();
				BlockSlot slot;
				slot.block_type = getStr(bd, "type", "");
				if (bd.contains("offset")) {
					py::list off = bd["offset"].cast<py::list>();
					slot.offset = {off[0].cast<int>(), off[1].cast<int>(), off[2].cast<int>()};
				}
				out.blocks.push_back(std::move(slot));
			}
		}

		printf("[StructureBlueprint] Loaded %s from %s (%zu blocks)\n",
		       out.id.c_str(), filePath.c_str(), out.blocks.size());
		return !out.id.empty();

	} catch (const std::exception& e) {
		printf("[StructureBlueprint] Error loading %s: %s\n", filePath.c_str(), e.what());
		return false;
	}
}

// ================================================================
// StructureBlueprintManager::firstMissingBlock
// ================================================================

std::optional<BlockSlot> StructureBlueprintManager::firstMissingBlock(
	const Entity& e,
	const std::function<std::string(int,int,int)>& blockAt) const
{
	if (!e.structure) return std::nullopt;
	const auto* bp = get(e.structure->blueprintId);
	if (!bp) return std::nullopt;

	const glm::ivec3& anchor = e.structure->anchorPos;
	for (const auto& slot : bp->blocks) {
		const glm::ivec3 wp = anchor + slot.offset;
		const std::string actual = blockAt(wp.x, wp.y, wp.z);
		if (slot.block_type.empty()) {
			if (actual == BlockType::Air) return slot;
		} else {
			if (actual != slot.block_type) return slot;
		}
	}
	return std::nullopt;
}

} // namespace modcraft
#endif // __EMSCRIPTEN__
