#include "server/python_bridge.h"
#include "server/structure_blueprint.h"
#include "logic/material_values.h"
#include <fstream>
#include <iterator>

#ifdef __EMSCRIPTEN__
// Web/WASM stub — Python is server-only, everything returns safe defaults.
namespace civcraft {
bool PythonBridge::init(const std::string&) { return false; }
void PythonBridge::shutdown() {}
BehaviorHandle PythonBridge::loadBehavior(const std::string&, std::string& err) {
	err = "Python not available in web build";
	return -1;
}
Plan PythonBridge::callDecide(BehaviorHandle, const EntitySnapshot&,
                               const std::vector<NearbyEntity>&,
                               float, float, std::string&, std::string& err,
                               BlockQueryFn, ScanBlocksFn, ScanEntitiesFn,
                               ScanAnnotationsFn, const std::string&,
                               const std::string&, const std::string&,
                               AppearanceQueryFn) {
	err = "Python not available in web build"; return {};
}
bool PythonBridge::callReact(BehaviorHandle, const EntitySnapshot&,
                              const std::vector<NearbyEntity>&,
                              float, float, const std::string&,
                              const std::vector<std::pair<std::string, std::string>>&,
                              Plan&, std::string&, std::string& err,
                              BlockQueryFn, ScanBlocksFn, ScanEntitiesFn,
                              ScanAnnotationsFn, AppearanceQueryFn) {
	err = "Python not available in web build"; return false;
}
std::string PythonBridge::getSource(BehaviorHandle) const { return ""; }
void PythonBridge::unloadBehavior(BehaviorHandle) {}
PythonBridge& pythonBridge() { static PythonBridge b; return b; }
bool loadWorldConfig(const std::string&, WorldPyConfig&) { return false; }
bool loadWeatherSchedule(const std::string&, WeatherPyConfig&) { return false; }
bool loadStructureBlueprint(const std::string&, StructureBlueprint&) { return false; }
std::optional<BlockSlot> StructureBlueprintManager::firstMissingBlock(
	const Entity&, const std::function<std::string(int,int,int)>&) const { return std::nullopt; }
} // namespace civcraft
#else
// Native build — full pybind11.
#include <pybind11/embed.h>
#include <pybind11/stl.h>
#include <cstdio>

namespace py = pybind11;

namespace civcraft {

struct PyEntityInfo {
	EntityId id;
	std::string type;
	float x, y, z;
	float distance;
	int hp;
};

// Mirrors C++ Container.
struct PyContainer {
	int      kind      = 0;              // 0=Self, 1=Ground, 2=Entity, 3=Block
	EntityId entity_id = ENTITY_NONE;
	float    x = 0, y = 0, z = 0;       // kind=Block
};

// TODO: replace with Plan/PlanStep when decide() stops returning (action, goal_str).
struct PyAction {
	std::string type;    // "move", "relocate", "convert", "interact"
	float x = 0, y = 0, z = 0;
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
	PyContainer convert_from;
	PyContainer convert_into;

	// Interact: -1 = legacy toggle (door/TNT); >=0 = write appearance index (I3).
	int         appearance_idx = -1;

	// Move: optional live-follow/flee anchor (see ActionProposal::anchorEntityId).
	EntityId    anchor_entity_id = ENTITY_NONE;
	float       keep_within      = 0.0f;
	float       keep_away        = 0.0f;
};

// Per-call state, set before callDecide(). Agent is single-threaded.
static std::function<std::string(int,int,int)> s_blockQueryFn;
static std::function<int(int,int,int)>         s_appearanceQueryFn;

using ScanBlocksFn = std::function<std::vector<BlockSample>(const std::string&, glm::vec3, float, int)>;
static ScanBlocksFn s_scanBlocksFn;

using ScanEntitiesFn = std::function<std::vector<NearbyEntity>(const std::string&, glm::vec3, float, int)>;
static ScanEntitiesFn s_scanEntitiesFn;

// See shared/annotation.h. Same shape as scan_blocks.
using ScanAnnotationsFn = std::function<std::vector<BlockSample>(const std::string&, glm::vec3, float, int)>;
static ScanAnnotationsFn s_scanAnnotationsFn;

// Default anchor when Python passes near=None.
static glm::vec3 s_selfPos;

// Resolve a Python `near` argument to a world-space origin. `None` →
// the agent's own position; a (x, y, z) tuple → those coords.
static glm::vec3 resolveScanOrigin(py::object near) {
	if (near.is_none()) return s_selfPos;
	auto t = near.cast<std::tuple<float, float, float>>();
	return {std::get<0>(t), std::get<1>(t), std::get<2>(t)};
}

// Shared body of scan_blocks / scan_entities / scan_annotations. Guards
// the null-callable case, resolves the origin, invokes the scan functor,
// and serialises each hit to py::dict via the caller-supplied `fill`.
template <typename ScanFn, typename DictFill>
static py::list runScan(const ScanFn& scanFn, const std::string& typeId,
                         py::object near, float maxDist, int maxResults,
                         DictFill fill) {
	py::list result;
	if (!scanFn) return result;
	auto hits = scanFn(typeId, resolveScanOrigin(near), maxDist, maxResults);
	for (auto& h : hits) {
		py::dict d;
		fill(d, h);
		result.append(d);
	}
	return result;
}

PYBIND11_EMBEDDED_MODULE(civcraft_engine, m) {
	m.doc() = "CivCraft engine bridge — exposes world view to Python behaviors";

	py::class_<PyEntityInfo>(m, "EntityInfo")
		.def_readonly("id", &PyEntityInfo::id)
		.def_readonly("type", &PyEntityInfo::type)
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

	// Factories for {relocate,convert}_{from,into,to}.
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
		.def_readwrite("convert_into", &PyAction::convert_into)
		// Interact
		.def_readwrite("appearance_idx", &PyAction::appearance_idx)
		// Move anchor
		.def_readwrite("anchor_entity_id", &PyAction::anchor_entity_id)
		.def_readwrite("keep_within",      &PyAction::keep_within)
		.def_readwrite("keep_away",        &PyAction::keep_away);

	// Only write primitives the server accepts. High-level helpers
	// (BreakBlock, StoreItem, PickupItem, DropItem) wrap these in python/actions.py.
	m.def("Idle", []() {
		PyAction a; a.type = "idle"; return a;
	});
	m.def("Move", [](float x, float y, float z, float speed,
	                 EntityId anchor, float keep_within, float keep_away) {
		PyAction a; a.type = "move"; a.x = x; a.y = y; a.z = z; a.speed = speed;
		a.anchor_entity_id = anchor;
		a.keep_within = keep_within;
		a.keep_away   = keep_away;
		return a;
	}, py::arg("x"), py::arg("y"), py::arg("z"), py::arg("speed") = 2.0f,
	   py::arg("anchor") = ENTITY_NONE,
	   py::arg("keep_within") = 0.0f,
	   py::arg("keep_away")   = 0.0f,
	   "Move toward (x,y,z). When `anchor` is a live entity id, the server "
	   "re-aims each tick: `keep_within` chases and stops inside the ring; "
	   "`keep_away` flees and stops once farther than the ring. Pick one.");
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
	m.def("material_value", [](const std::string& itemId) {
		return getMaterialValue(itemId);
	}, py::arg("item_id"),
	   "Lookup material value for an item/block. Single source of truth "
	   "is src/shared/material_values.h — never hardcode values in Python.");

	m.def("Interact", [](int x, int y, int z, int appearance) {
		PyAction a;
		a.type = "interact";
		a.x = (float)x; a.y = (float)y; a.z = (float)z;
		a.appearance_idx = appearance;
		return a;
	}, py::arg("x"), py::arg("y"), py::arg("z"), py::arg("appearance") = -1,
	   "Toggle interactive block (door/TNT). Pass appearance>=0 to write a "
	   "palette entry at (x,y,z) without changing block type.");

	m.def("get_appearance", [](int x, int y, int z) -> int {
		return s_appearanceQueryFn ? s_appearanceQueryFn(x, y, z) : 0;
	}, py::arg("x"), py::arg("y"), py::arg("z"),
	   "Return the appearance-palette index at world position (x,y,z). "
	   "Call only inside decide().");

	m.def("set_appearance", [](int x, int y, int z, int appearance) {
		PyAction a;
		a.type = "interact";
		a.x = (float)x; a.y = (float)y; a.z = (float)z;
		a.appearance_idx = appearance;
		return a;
	}, py::arg("x"), py::arg("y"), py::arg("z"), py::arg("appearance"),
	   "Emit an Interact action that writes appearance idx at (x,y,z). "
	   "Block id is preserved; only the palette entry changes.");

	// Valid only inside decide(); returns "air" outside.
	m.def("get_block", [](int x, int y, int z) -> std::string {
		if (s_blockQueryFn) return s_blockQueryFn(x, y, z);
		return "air";
	}, py::arg("x"), py::arg("y"), py::arg("z"),
	   "Query block type string at world position (x,y,z). Call only inside decide().");

	// Picks nearest non-empty chunk via ChunkInfo index. `near` anchors distance
	// sort (e.g. pass home/bed so AI doesn't chase dense matches across the map).
	m.def("scan_blocks", [](const std::string& typeId, py::object near,
	                        float maxDist, int maxResults) -> py::list {
		return runScan(s_scanBlocksFn, typeId, near, maxDist, maxResults,
			[](py::dict& d, const auto& b) {
				d["type"]     = b.typeId;
				d["x"]        = b.x;
				d["y"]        = b.y;
				d["z"]        = b.z;
				d["distance"] = b.distance;
			});
	}, py::arg("type"), py::arg("near") = py::none(),
	   py::arg("max_dist") = 80.0f, py::arg("max_results") = 20,
	   "Find blocks of a specific type near `near` (default: self position).");

	// World-wide lookup; bypasses the per-agent 64-block nearby cache.
	m.def("scan_entities", [](const std::string& typeId, py::object near,
	                          float maxDist, int maxResults) -> py::list {
		return runScan(s_scanEntitiesFn, typeId, near, maxDist, maxResults,
			[](py::dict& d, const auto& e) {
				d["id"]       = e.id;
				d["type"]     = e.typeId;
				d["x"]        = e.position.x;
				d["y"]        = e.position.y;
				d["z"]        = e.position.z;
				d["distance"] = e.distance;
			});
	}, py::arg("type"), py::arg("near") = py::none(),
	   py::arg("max_dist") = 80.0f, py::arg("max_results") = 20,
	   "Find entities of a specific type near `near` (default: self position). "
	   "Bypasses the per-agent nearby cache; scans the whole entity list.");

	// Same shape as scan_blocks; hits are block decorators (flower, moss, …).
	m.def("scan_annotations", [](const std::string& typeId, py::object near,
	                             float maxDist, int maxResults) -> py::list {
		return runScan(s_scanAnnotationsFn, typeId, near, maxDist, maxResults,
			[](py::dict& d, const auto& h) {
				d["type"]     = h.typeId;
				d["x"]        = h.x;
				d["y"]        = h.y;
				d["z"]        = h.z;
				d["distance"] = h.distance;
			});
	}, py::arg("type"), py::arg("near") = py::none(),
	   py::arg("max_dist") = 80.0f, py::arg("max_results") = 20,
	   "Find annotations (block decorators) of a specific type near `near`.");

	// Expose C++ name constants so behaviors use identical identifiers.
	auto living = m.def_submodule("LivingName", "Living entity type IDs");
	// No "Player" binding: any Living with playable=true can be a player
	// character. Behaviors that need "is this a playable creature?" should
	// check EntityDef.playable via the entity bindings.
	living.attr("Pig")          = LivingName::Pig;
	living.attr("Chicken")      = LivingName::Chicken;
	living.attr("Dog")          = LivingName::Dog;
	living.attr("Villager")     = LivingName::Villager;
	living.attr("Cat")          = LivingName::Cat;
	living.attr("BraveChicken") = LivingName::BraveChicken;
	living.attr("Knight")       = LivingName::Knight;
	living.attr("Mage")         = LivingName::Mage;
	living.attr("Skeleton")     = LivingName::Skeleton;
	living.attr("Giant")        = LivingName::Giant;

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
	block.attr("Slab")        = BlockType::Slab;
	block.attr("CornerStair") = BlockType::CornerStair;
	block.attr("Pillar")      = BlockType::Pillar;
	block.attr("Trapdoor")    = BlockType::Trapdoor;
	block.attr("Torch")       = BlockType::Torch;
	block.attr("Glass")       = BlockType::Glass;
	block.attr("Door")        = BlockType::Door;
	block.attr("DoorOpen")    = BlockType::DoorOpen;
	block.attr("Portal")      = BlockType::Portal;
	block.attr("ArcaneStone") = BlockType::ArcaneStone;
	block.attr("SpawnPoint")  = BlockType::SpawnPoint;
}

static PythonBridge s_bridge;
PythonBridge& pythonBridge() { return s_bridge; }

bool PythonBridge::init(const std::string& pythonPath) {
	if (m_initialized) return true;

	py::initialize_interpreter();

	// Inner scope so stack py::objects destruct (Py_DECREF) while we still hold
	// the GIL. GIL is released only after this scope exits.
	bool ok = false;
	{
		try {
			auto sys = py::module_::import("sys");
			sys.attr("path").cast<py::list>().append(pythonPath);
			sys.attr("stdout").attr("reconfigure")(py::arg("line_buffering") = true);

			auto localWorldMod = py::module_::import("local_world");
			m_localWorldClass = new py::object(localWorldMod.attr("LocalWorld"));
			m_selfEntityClass = new py::object(localWorldMod.attr("SelfEntity"));

			printf("[PythonBridge] Initialized. Python path: %s\n", pythonPath.c_str());
			ok = true;
		} catch (const py::error_already_set& e) {
			printf("[PythonBridge] Init failed: %s\n", e.what());
		}
	}

	if (!ok) {
		py::finalize_interpreter();
		return false;
	}

	m_initialized = true;
	// Release main GIL so DecideWorker can acquire it. All subsequent Python
	// calls (any thread) must scope-acquire via py::gil_scoped_acquire.
	m_gilReleaser = new py::gil_scoped_release();
	return true;
}

void PythonBridge::shutdown() {
	if (!m_initialized) return;
	// Re-acquire GIL to tear down objects, then release to finalize.
	{
		py::gil_scoped_acquire gil;
		m_behaviors.clear();
		delete static_cast<py::object*>(m_localWorldClass);  m_localWorldClass = nullptr;
		delete static_cast<py::object*>(m_selfEntityClass);  m_selfEntityClass = nullptr;
	}
	delete static_cast<py::gil_scoped_release*>(m_gilReleaser);
	m_gilReleaser = nullptr;
	py::finalize_interpreter();
	m_initialized = false;
	printf("[PythonBridge] Shutdown.\n");
}

// PyContainer → C++ Container. Used when converting Plan steps to ActionProposals.
static Container pyContainerToC(const PyContainer& pc) {
	switch (pc.kind) {
	case 1:  return Container::ground();
	case 2:  return Container::entity(pc.entity_id);
	case 3:  return Container::block((int)pc.x, (int)pc.y, (int)pc.z);
	default: return Container::self();
	}
}

BehaviorHandle PythonBridge::loadBehavior(const std::string& sourceCode, std::string& errorOut) {
	if (!m_initialized) {
		errorOut = "Python bridge not initialized";
		return -1;
	}

	py::gil_scoped_acquire gil;

	try {
		// Isolated namespace per behavior (one per entity instance).
		py::dict ns;

		py::exec("from civcraft_engine import *", ns);
		py::exec("from actions import *", ns);
		py::exec("from behavior_base import Behavior", ns);

		py::exec(sourceCode, ns);

		// Instantiate the most-derived Behavior subclass — so RulesBehavior
		// subclasses don't end up picking the base instead of the concrete.
		py::exec(R"(
import inspect as _i
_candidates = [_c for _n, _c in list(globals().items())
               if _i.isclass(_c) and _c is not Behavior and issubclass(_c, Behavior)]
_behavior_instance = None
for _c in _candidates:
    if not any(_o is not _c and issubclass(_o, _c) for _o in _candidates):
        _behavior_instance = _c()
        break
del _i, _candidates
try: del _c, _o, _n
except NameError: pass
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

		return handle;

	} catch (const py::error_already_set& e) {
		errorOut = e.what();
		return -1;
	}
}

// Builds the pydantic SelfEntity + LocalWorld from a snapshot. Shared by
// callDecide and callReact so the two entry points see identical world
// state. GIL must be held.
static void buildPyWorld(const EntitySnapshot& self,
                         const std::vector<NearbyEntity>& nearby,
                         float dt, float timeOfDay,
                         const std::string& lastOutcome,
                         const std::string& lastGoal,
                         const std::string& lastReason,
                         void* localWorldClsRaw, void* selfEntityClsRaw,
                         py::object& outSelfEntity,
                         py::object& outLocalWorld) {
	py::list pyNearby;
	for (auto& ne : nearby) {
		py::dict info;
		info["id"] = ne.id; info["type"] = ne.typeId;
		info["kind"] = (ne.kind == EntityKind::Living) ? "living" : "item";
		info["x"] = ne.position.x; info["y"] = ne.position.y; info["z"] = ne.position.z;
		info["distance"] = ne.distance; info["hp"] = ne.hp;
		py::list pyTags; for (auto& t : ne.tags) pyTags.append(t);
		info["tags"] = pyTags;
		pyNearby.append(info);
	}

	py::dict pySelf;
	for (auto& [key, val] : self.props) {
		if (auto* s = std::get_if<std::string>(&val)) pySelf[key.c_str()] = *s;
		else if (auto* i = std::get_if<int>(&val))     pySelf[key.c_str()] = *i;
		else if (auto* f = std::get_if<float>(&val))   pySelf[key.c_str()] = *f;
		else if (auto* b = std::get_if<bool>(&val))    pySelf[key.c_str()] = *b;
	}
	pySelf["id"] = self.id; pySelf["type"] = self.typeId;
	pySelf["x"] = self.position.x; pySelf["y"] = self.position.y; pySelf["z"] = self.position.z;
	pySelf["yaw"] = self.yaw; pySelf["hp"] = self.hp;
	pySelf["walk_speed"] = self.walkSpeed; pySelf["on_ground"] = self.onGround;
	pySelf["inventory_capacity"] = self.inventoryCapacity;
	py::dict pyInv;
	for (auto& [itemId, count] : self.inventory)
		pyInv[itemId.c_str()] = count;
	pySelf["inventory"] = pyInv;

	py::dict pyWorld;
	pyWorld["nearby"] = pyNearby;
	pyWorld["blocks"] = py::list();
	pyWorld["dt"] = dt; pyWorld["time"] = timeOfDay;
	pyWorld["goal"] = py::none();
	pyWorld["last_outcome"] = lastOutcome;
	pyWorld["last_goal"]    = lastGoal;
	pyWorld["last_reason"]  = lastReason;

	py::object& LocalWorldCls = *static_cast<py::object*>(localWorldClsRaw);
	py::object& SelfEntityCls = *static_cast<py::object*>(selfEntityClsRaw);
	outLocalWorld = LocalWorldCls.attr("_from_raw")(pyWorld);
	outSelfEntity = SelfEntityCls.attr("_from_raw")(pySelf);
}

// Converts a Python (action_or_plan, goal[, duration]) tuple into a Plan.
// Returns false on malformed result (errorOut populated). firstIsNone
// outputs whether tup[0] was Python None — callers distinguish "Idle plan"
// (decide) from "ignore signal" (react) by inspecting this.
static bool parsePyResult(const py::object& result, Plan& outPlan,
                          std::string& goalOut, std::string& errorOut,
                          bool& firstIsNone);

// Old-format PyAction → PlanStep.
static PlanStep pyActionToPlanStep(const PyAction& pa) {
	if (pa.type == "move") {
		PlanStep s = PlanStep::move({pa.x, pa.y, pa.z}, pa.speed);
		s.anchorEntityId = pa.anchor_entity_id;
		s.keepWithin     = pa.keep_within;
		s.keepAway       = pa.keep_away;
		return s;
	}
	if (pa.type == "convert") {
		// Block pos carried in convert_from (kind=Block); fall back to (x,y,z).
		glm::vec3 pos = (pa.convert_from.kind == 3)
			? glm::vec3(pa.convert_from.x, pa.convert_from.y, pa.convert_from.z)
			: glm::vec3(pa.x, pa.y, pa.z);
		PlanStep s = PlanStep::harvest(pos);
		// Empty to_item = destroy.
		s.itemId    = pa.to_item;
		s.itemCount = pa.to_count;
		return s;
	}
	if (pa.type == "relocate")
		return PlanStep::relocate(pyContainerToC(pa.relocate_from),
		                          pyContainerToC(pa.relocate_to),
		                          pa.item_id, pa.item_count);
	if (pa.type == "interact")
		return PlanStep::interact({pa.x, pa.y, pa.z},
		                          (int16_t)pa.appearance_idx);
	// idle / unknown → stand still.
	return PlanStep::move({pa.x, pa.y, pa.z}, 0.0f);
}

Plan PythonBridge::callDecide(BehaviorHandle handle,
                               const EntitySnapshot& self,
                               const std::vector<NearbyEntity>& nearby,
                               float dt, float timeOfDay,
                               std::string& goalOut,
                               std::string& errorOut,
                               BlockQueryFn blockQueryFn,
                               ScanBlocksFn scanBlocksFn,
                               ScanEntitiesFn scanEntitiesFn,
                               ScanAnnotationsFn scanAnnotationsFn,
                               const std::string& lastOutcome,
                               const std::string& lastGoal,
                               const std::string& lastReason,
                               AppearanceQueryFn appearanceQueryFn) {
	// Runs on DecideWorker's thread.
	py::gil_scoped_acquire gil;

	// Per-call callbacks; GIL serializes callDecide so static state is safe.
	s_blockQueryFn   = blockQueryFn ? std::move(blockQueryFn)
	                                : [](int,int,int){ return std::string("air"); };
	s_appearanceQueryFn = appearanceQueryFn ? std::move(appearanceQueryFn)
	                                        : [](int,int,int){ return 0; };
	s_scanBlocksFn   = std::move(scanBlocksFn);
	s_scanEntitiesFn = std::move(scanEntitiesFn);
	s_scanAnnotationsFn = std::move(scanAnnotationsFn);
	s_selfPos        = self.position;
	struct Cleanup {
		~Cleanup() {
			s_blockQueryFn = nullptr;
			s_appearanceQueryFn = nullptr;
			s_scanBlocksFn = nullptr;
			s_scanEntitiesFn = nullptr;
			s_scanAnnotationsFn = nullptr;
		}
	} _cleanup;

	auto it = m_behaviors.find(handle);
	if (it == m_behaviors.end()) {
		errorOut = "Invalid behavior handle";
		return {};
	}

	try {
		py::object& instance = *static_cast<py::object*>(it->second.instanceObj);

		py::object pySelfEntity, pyLocalWorld;
		buildPyWorld(self, nearby, dt, timeOfDay,
		             lastOutcome, lastGoal, lastReason,
		             m_localWorldClass, m_selfEntityClass,
		             pySelfEntity, pyLocalWorld);

		py::object result = instance.attr("decide")(pySelfEntity, pyLocalWorld);

		Plan plan; bool firstIsNone = false;
		if (!parsePyResult(result, plan, goalOut, errorOut, firstIsNone))
			return {};
		return plan;

	} catch (const py::error_already_set& e) {
		errorOut = e.what();
		fprintf(stderr, "[PythonBridge] decide() exception: %s\n", e.what());
		fflush(stderr);
		return {};
	}
}

bool PythonBridge::callReact(BehaviorHandle handle,
                              const EntitySnapshot& self,
                              const std::vector<NearbyEntity>& nearby,
                              float dt, float timeOfDay,
                              const std::string& signalKind,
                              const std::vector<std::pair<std::string, std::string>>& signalPayload,
                              Plan& outPlan,
                              std::string& goalOut,
                              std::string& errorOut,
                              BlockQueryFn blockQueryFn,
                              ScanBlocksFn scanBlocksFn,
                              ScanEntitiesFn scanEntitiesFn,
                              ScanAnnotationsFn scanAnnotationsFn,
                              AppearanceQueryFn appearanceQueryFn) {
	// Runs on DecideWorker's thread.
	py::gil_scoped_acquire gil;

	// Same static-state protocol as callDecide — GIL serializes this.
	s_blockQueryFn   = blockQueryFn ? std::move(blockQueryFn)
	                                : [](int,int,int){ return std::string("air"); };
	s_appearanceQueryFn = appearanceQueryFn ? std::move(appearanceQueryFn)
	                                        : [](int,int,int){ return 0; };
	s_scanBlocksFn   = std::move(scanBlocksFn);
	s_scanEntitiesFn = std::move(scanEntitiesFn);
	s_scanAnnotationsFn = std::move(scanAnnotationsFn);
	s_selfPos        = self.position;
	struct Cleanup {
		~Cleanup() {
			s_blockQueryFn = nullptr;
			s_appearanceQueryFn = nullptr;
			s_scanBlocksFn = nullptr;
			s_scanEntitiesFn = nullptr;
			s_scanAnnotationsFn = nullptr;
		}
	} _cleanup;

	auto it = m_behaviors.find(handle);
	if (it == m_behaviors.end()) {
		errorOut = "Invalid behavior handle";
		return false;
	}

	try {
		py::object& instance = *static_cast<py::object*>(it->second.instanceObj);

		py::object pySelfEntity, pyLocalWorld;
		buildPyWorld(self, nearby, dt, timeOfDay,
		             "none", "", "signal:" + signalKind,
		             m_localWorldClass, m_selfEntityClass,
		             pySelfEntity, pyLocalWorld);

		// Build signal as types.SimpleNamespace so Python sees .kind + .payload
		// attribute access (matches the documented react() contract).
		py::dict pyPayload;
		for (auto& [k, v] : signalPayload) pyPayload[k.c_str()] = v;
		py::object types = py::module_::import("types");
		py::object pySignal = types.attr("SimpleNamespace")(
			py::arg("kind") = signalKind,
			py::arg("payload") = pyPayload);

		py::object result = instance.attr("react")(pySelfEntity, pyLocalWorld, pySignal);

		// None from react() = ignore the signal, keep current plan.
		if (result.is_none()) return false;

		Plan plan; bool firstIsNone = false;
		if (!parsePyResult(result, plan, goalOut, errorOut, firstIsNone))
			return false;
		// A tuple with (None, goal) also means "no change" — don't install an
		// empty plan over a live one.
		if (firstIsNone && plan.empty()) return false;

		outPlan = std::move(plan);
		return true;

	} catch (const py::error_already_set& e) {
		errorOut = e.what();
		fprintf(stderr, "[PythonBridge] react() exception: %s\n", e.what());
		fflush(stderr);
		return false;
	}
}

static bool parsePyResult(const py::object& result, Plan& outPlan,
                          std::string& goalOut, std::string& errorOut,
                          bool& firstIsNone) {
	firstIsNone = false;

	// Expect (action_or_plan, goal_str[, hold_seconds]).
	if (!py::isinstance<py::tuple>(result)) {
		errorOut = "behavior must return a tuple";
		return false;
	}
	py::tuple tup = result.cast<py::tuple>();
	if (tup.size() < 2) {
		errorOut = "behavior must return at least (action/plan, goal_str)";
		return false;
	}

	goalOut = tup[1].cast<std::string>();
	if (goalOut.empty()) goalOut = "Active";

	// Legacy single-action tuples carry the behavior's commit duration in
	// tup[2]; dict-list plans carry it per-step via d["hold"].
	float legacyHold = 0.0f;
	if (tup.size() >= 3 && !tup[2].is_none()) {
		legacyHold = tup[2].cast<float>();
	}

	// tup[0]: old-format PyAction | new-format list of PlanStep dicts | None.
	py::object first = tup[0];

	if (first.is_none()) {
		firstIsNone = true;
		return true;  // Idle.
	}
	if (py::isinstance<py::list>(first)) {
		// PlanStep dict schema:
		//   move:     {type, x, y, z, speed, hold?}
		//   harvest:  {type, x, y, z}
		//   attack:   {type, entity_id}
		//   relocate: {type, from, to, item, count}
		for (auto& item : first.cast<py::list>()) {
			py::dict d = item.cast<py::dict>();
			std::string stype = d["type"].cast<std::string>();
			float hold = d.contains("hold") && !d["hold"].is_none()
				? d["hold"].cast<float>() : 0.0f;
			if (stype == "move") {
				PlanStep step = PlanStep::move(
					{d["x"].cast<float>(), d["y"].cast<float>(), d["z"].cast<float>()},
					 d["speed"].cast<float>(), hold);
				if (d.contains("anchor") && !d["anchor"].is_none())
					step.anchorEntityId = d["anchor"].cast<EntityId>();
				if (d.contains("keep_within") && !d["keep_within"].is_none())
					step.keepWithin = d["keep_within"].cast<float>();
				if (d.contains("keep_away") && !d["keep_away"].is_none())
					step.keepAway = d["keep_away"].cast<float>();
				outPlan.push_back(step);
			} else if (stype == "harvest") {
				PlanStep step = PlanStep::harvest(
					{d["x"].cast<float>(), d["y"].cast<float>(), d["z"].cast<float>()});
				// Priority-ordered gather types (index 0 = highest). The
				// executor scans the local volume each tick and chops the
				// first tier that has a hit in gatherRadius.
				if (d.contains("gather_types") && !d["gather_types"].is_none()) {
					for (auto& t : d["gather_types"].cast<py::list>())
						step.gatherTypes.push_back(t.cast<std::string>());
				}
				if (d.contains("gather_radius") && !d["gather_radius"].is_none())
					step.gatherRadius = d["gather_radius"].cast<float>();
				if (d.contains("chop_cooldown") && !d["chop_cooldown"].is_none())
					step.chopCooldown = d["chop_cooldown"].cast<float>();
				if (d.contains("item") && !d["item"].is_none())
					step.itemId = d["item"].cast<std::string>();
				outPlan.push_back(step);
			} else if (stype == "attack") {
				outPlan.push_back(PlanStep::attack(d["entity_id"].cast<EntityId>()));
			} else if (stype == "relocate") {
				outPlan.push_back(PlanStep::relocate(
					Container::self(), Container::self(),
					d["item"].cast<std::string>(),
					d["count"].cast<int>()));
			} else {
				errorOut = "Unknown PlanStep type: " + stype;
				return false;
			}
		}
		return true;
	}
	// Backward compat: single PyAction → single-step Plan.
	// Legacy (action, goal, duration) tuple carries duration in tup[2];
	// applied to the single step's holdTime.
	PyAction pa = first.cast<PyAction>();
	PlanStep step = pyActionToPlanStep(pa);
	if (step.type == PlanStep::Move && legacyHold > 0.0f)
		step.holdTime = legacyHold;
	outPlan.push_back(step);
	return true;
}

std::string PythonBridge::getSource(BehaviorHandle handle) const {
	auto it = m_behaviors.find(handle);
	return it != m_behaviors.end() ? it->second.source : "";
}

void PythonBridge::unloadBehavior(BehaviorHandle handle) {
	py::gil_scoped_acquire gil;
	auto it = m_behaviors.find(handle);
	if (it != m_behaviors.end()) {
		if (it->second.moduleObj)
			delete static_cast<py::dict*>(it->second.moduleObj);
		if (it->second.instanceObj)
			delete static_cast<py::object*>(it->second.instanceObj);
		m_behaviors.erase(it);
	}
}

// PythonBehavior::decide — REMOVED
// The old Behavior class hierarchy (PythonBehavior, IdleFallbackBehavior) has been
// deleted. AgentClient now calls Python decide() directly via PythonBridge.

// Loads artifacts/worlds/*.py via pybind11.
bool loadWorldConfig(const std::string& filePath, WorldPyConfig& out) {
	auto& bridge = pythonBridge();
	if (!bridge.isInitialized()) return false;

	std::ifstream f(filePath);
	if (!f.is_open()) {
		printf("[WorldConfig] File not found: %s\n", filePath.c_str());
		return false;
	}
	std::string src((std::istreambuf_iterator<char>(f)), {});

	py::gil_scoped_acquire gil;

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

		out.name        = getStr(w, "name", "");
		out.description = getStr(w, "description", "");

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

		if (w.contains("trees") && !w["trees"].is_none()) {
			py::dict t = w["trees"].cast<py::dict>();
			out.treeDensity    = getFloat(t, "density",          out.treeDensity);
			out.trunkHeightMin = getInt(t,   "trunk_height_min", out.trunkHeightMin);
			out.trunkHeightMax = getInt(t,   "trunk_height_max", out.trunkHeightMax);
			out.leafRadius     = getInt(t,   "leaf_radius",      out.leafRadius);
		}

		if (w.contains("spawn") && !w["spawn"].is_none()) {
			py::dict s = w["spawn"].cast<py::dict>();
			out.spawnSearchX = getFloat(s, "search_x",   out.spawnSearchX);
			out.spawnSearchZ = getFloat(s, "search_z",   out.spawnSearchZ);
			out.spawnMinH    = getFloat(s, "min_height", out.spawnMinH);
			out.spawnMaxH    = getFloat(s, "max_height", out.spawnMaxH);
			// Flat-world fixed spawn.
			if (s.contains("x")) out.spawnSearchX = getFloat(s, "x", out.spawnSearchX);
			if (s.contains("z")) out.spawnSearchZ = getFloat(s, "z", out.spawnSearchZ);
		}

		if (w.contains("chest") && !w["chest"].is_none()) {
			py::dict c = w["chest"].cast<py::dict>();
			out.chestOffsetX = getFloat(c, "offset_x", out.chestOffsetX);
			out.chestOffsetZ = getFloat(c, "offset_z", out.chestOffsetZ);
		}

		// Clamp to sane bounds — typo can't stall server with 10k-chunk grid.
		out.preloadRadiusChunks = getInt(w, "preload_radius_chunks", out.preloadRadiusChunks);
		if (out.preloadRadiusChunks < 1)  out.preloadRadiusChunks = 1;
		if (out.preloadRadiusChunks > 24) out.preloadRadiusChunks = 24;

		// Clamp away from 0 to avoid div-by-zero / 10 days/sec.
		out.dayLengthTicks = getInt(w, "day_length_ticks", out.dayLengthTicks);
		if (out.dayLengthTicks < 30)     out.dayLengthTicks = 30;
		if (out.dayLengthTicks > 86400)  out.dayLengthTicks = 86400;

		out.startingDay = getInt(w, "starting_day", out.startingDay);
		if (out.startingDay < 0) out.startingDay = 0;

		// Path relative to src/artifacts/; loaded separately.
		out.weatherSchedule = getStr(w, "weather_schedule", out.weatherSchedule);

		// "portal": False disables temple arch.
		if (w.contains("portal") && !w["portal"].is_none()) {
			py::object p = w["portal"];
			out.hasPortal = p.cast<bool>();
		}

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
						// {"cx":0,"cz":0,"w":14,"d":14,"stories":2,"wall":"wood"}
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
						// [cx, cz, w, d, stories?]
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

		if (w.contains("mobs")) {
			out.mobs.clear();
			for (auto& m : w["mobs"].cast<py::list>()) {
				py::dict md = m.cast<py::dict>();
				WorldPyConfig::MobConfig mc;
				mc.type   = md["type"].cast<std::string>();
				mc.count  = md["count"].cast<int>();
				mc.radius = getFloat(md, "radius", 20.0f);
				if (md.contains("spawn_at"))
					mc.spawnAt = md["spawn_at"].cast<std::string>();
				mc.yOffset = getFloat(md, "y_offset", 0.0f);
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

// Markov chain + wind model. Schema: artifacts/worlds/base/weather/temperate.py.
bool loadWeatherSchedule(const std::string& filePath, WeatherPyConfig& out) {
	auto& bridge = pythonBridge();
	if (!bridge.isInitialized()) return false;

	std::ifstream f(filePath);
	if (!f.is_open()) {
		printf("[WeatherSchedule] File not found: %s\n", filePath.c_str());
		return false;
	}
	std::string src((std::istreambuf_iterator<char>(f)), {});

	py::gil_scoped_acquire gil;

	try {
		py::dict ns;
		py::exec(src.c_str(), ns);
		if (!ns.contains("schedule")) {
			printf("[WeatherSchedule] %s: no `schedule` dict\n", filePath.c_str());
			return false;
		}

		py::dict s = ns["schedule"].cast<py::dict>();
		WeatherPyConfig cfg;

		if (s.contains("kinds")) {
			cfg.kinds.clear();
			for (auto& k : s["kinds"].cast<py::list>()) {
				py::dict kd = k.cast<py::dict>();
				WeatherPyConfig::Kind kind;
				kind.name = kd.contains("name") ? kd["name"].cast<std::string>() : "clear";
				kind.meanSeconds = kd.contains("mean_s") ? (float)py::float_(kd["mean_s"]) : 300.0f;
				if (kd.contains("intensity")) {
					py::list iv = kd["intensity"].cast<py::list>();
					kind.minIntensity = (float)py::float_(iv[0]);
					kind.maxIntensity = (float)py::float_(iv[1]);
				}
				if (kd.contains("next")) {
					for (auto& [nk, nv] : kd["next"].cast<py::dict>()) {
						kind.next.push_back({nk.cast<std::string>(),
						                     nv.cast<float>()});
					}
				}
				cfg.kinds.push_back(std::move(kind));
			}
		}

		if (s.contains("wind")) {
			py::dict wd = s["wind"].cast<py::dict>();
			if (wd.contains("base")) {
				py::list bv = wd["base"].cast<py::list>();
				cfg.baseWindX = (float)py::float_(bv[0]);
				cfg.baseWindZ = (float)py::float_(bv[1]);
			}
			if (wd.contains("noise_amp"))
				cfg.windNoiseAmp = (float)py::float_(wd["noise_amp"]);
			if (wd.contains("noise_scale_s"))
				cfg.windNoiseScale = (float)py::float_(wd["noise_scale_s"]);
		}

		if (s.contains("initial_kind"))
			cfg.initialKind = s["initial_kind"].cast<std::string>();

		out = std::move(cfg);
		printf("[WeatherSchedule] Loaded %zu kinds from %s\n",
		       out.kinds.size(), filePath.c_str());
		return true;

	} catch (const std::exception& e) {
		printf("[WeatherSchedule] Error loading %s: %s\n", filePath.c_str(), e.what());
		return false;
	}
}

// Reads artifacts/structures/*.py; same pattern as loadWorldConfig().
bool loadStructureBlueprint(const std::string& filePath, StructureBlueprint& out) {
	auto& bridge = pythonBridge();
	if (!bridge.isInitialized()) return false;

	std::ifstream f(filePath);
	if (!f.is_open()) {
		printf("[StructureBlueprint] File not found: %s\n", filePath.c_str());
		return false;
	}
	std::string src((std::istreambuf_iterator<char>(f)), {});

	py::gil_scoped_acquire gil;

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

		if (bp.contains("anchor")) {
			py::dict a = bp["anchor"].cast<py::dict>();
			out.anchor.block_type = getStr(a, "block_type", "root");
			out.anchor.hardness   = getInt(a, "hardness", 3);
			if (a.contains("offset")) {
				py::list off = a["offset"].cast<py::list>();
				out.anchor.offset = {off[0].cast<int>(), off[1].cast<int>(), off[2].cast<int>()};
			}
		}

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

		// Optional: decorators that the server applies to each spawned instance.
		// Schema: features: [{"type": "seasonal_leaves",
		//                     "spring_variants": [...], "summer_variants": [...],
		//                     "autumn_variants": [...], "winter_variants": [...],
		//                     "per_tick_prob": 0.02, "scan_radius": 5}, ...]
		if (bp.contains("features")) {
			for (auto& item : bp["features"].cast<py::list>()) {
				py::dict fd = item.cast<py::dict>();
				std::string t = getStr(fd, "type", "");
				StructureFeature f;
				if (t == "seasonal_leaves") {
					f.type = StructureFeature::Type::SeasonalLeaves;
					// Legacy string block-ids map to indices in the Leaves
					// block's appearance_palette (see builtin.cpp). Unknown
					// strings are skipped with a warning.
					static const std::unordered_map<std::string, uint8_t> legacyMap = {
						{"leaves",         0},
						{"base:leaves",    0},
						{"leaves_spring",  1}, {"base:leaves_spring",  1},
						{"leaves_summer",  2}, {"base:leaves_summer",  2},
						{"leaves_gold",    3}, {"base:leaves_gold",    3},
						{"leaves_orange",  4}, {"base:leaves_orange",  4},
						{"leaves_red",     5}, {"base:leaves_red",     5},
						{"leaves_bare",    6}, {"base:leaves_bare",    6},
						{"leaves_snow",    7}, {"base:leaves_snow",    7},
					};
					auto readList = [&](const char* key, int seasonIdx) {
						if (!fd.contains(key)) return;
						for (auto& v : fd[key].cast<py::list>()) {
							if (py::isinstance<py::int_>(v)) {
								f.seasonVariants[seasonIdx]
									.push_back((uint8_t)v.cast<int>());
							} else {
								auto s = v.cast<std::string>();
								auto it = legacyMap.find(s);
								if (it != legacyMap.end())
									f.seasonVariants[seasonIdx].push_back(it->second);
								else
									printf("[StructureBlueprint] unknown seasonal-leaves variant '%s'; skipping\n",
									       s.c_str());
							}
						}
					};
					readList("spring_variants", 0);
					readList("summer_variants", 1);
					readList("autumn_variants", 2);
					readList("winter_variants", 3);
					f.perTickProb           = getFloat(fd, "per_tick_prob",           f.perTickProb);
					f.spawnTransitionChance = getFloat(fd, "spawn_transition_chance", f.spawnTransitionChance);
					f.scanRadius            = getInt(fd,   "scan_radius",             f.scanRadius);
					out.features.push_back(std::move(f));
				} else {
					printf("[StructureBlueprint] Unknown feature type '%s' in %s — skipping.\n",
					       t.c_str(), filePath.c_str());
				}
			}
		}

		printf("[StructureBlueprint] Loaded %s from %s (%zu blocks, %zu features)\n",
		       out.id.c_str(), filePath.c_str(), out.blocks.size(), out.features.size());
		return !out.id.empty();

	} catch (const std::exception& e) {
		printf("[StructureBlueprint] Error loading %s: %s\n", filePath.c_str(), e.what());
		return false;
	}
}

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

} // namespace civcraft
#endif // __EMSCRIPTEN__
