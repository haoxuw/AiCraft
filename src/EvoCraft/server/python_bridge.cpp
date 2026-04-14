#include "python_bridge.h"

#include <pybind11/embed.h>
#include <pybind11/stl.h>

#include <cstdio>
#include <filesystem>
#include <unordered_map>

namespace py = pybind11;

namespace evocraft {

struct PythonBridge::Impl {
	// Must be constructed first, destroyed last — owns the GIL lifecycle.
	py::scoped_interpreter interp;
	std::unordered_map<uint8_t, py::object> decideBatchFn;
	std::unordered_map<uint8_t, std::string> speciesName;
};

// --- helpers ---------------------------------------------------------------

namespace {

py::dict cellToDict(const CellView& c) {
	py::dict d;
	d["id"]      = c.id;
	d["x"]       = c.x;
	d["y"]       = c.y;
	d["z"]       = c.z;
	d["angle"]   = c.angle;
	d["speed"]   = c.speed;
	d["species"] = c.species;
	return d;
}

py::dict foodToDict(const FoodView& f) {
	py::dict d;
	d["id"] = f.id;
	d["x"]  = f.x;
	d["y"]  = f.y;
	d["z"]  = f.z;
	return d;
}

py::list toList(const std::vector<CellView>& v) {
	py::list out;
	for (const auto& c : v) out.append(cellToDict(c));
	return out;
}

py::list toList(const std::vector<FoodView>& v) {
	py::list out;
	for (const auto& f : v) out.append(foodToDict(f));
	return out;
}

} // namespace

// --- PythonBridge ----------------------------------------------------------

PythonBridge::PythonBridge() : impl_(new Impl()) {
	// Make "evocraft_artifacts" importable as a package-less module dir so
	// the species files can import helper modules alongside them.
	try {
		py::module_ sys = py::module_::import("sys");
		py::list path = sys.attr("path");
		std::string root = (std::filesystem::current_path()
			/ "evocraft_artifacts").string();
		path.insert(0, root);
		// Also add species/ for "import <speciesname>" shortcuts.
		path.insert(0, root + "/species");
	} catch (const std::exception& e) {
		std::fprintf(stderr, "[python_bridge] sys.path setup failed: %s\n",
			e.what());
	}
}

PythonBridge::~PythonBridge() {
	delete impl_;
}

bool PythonBridge::registerSpecies(uint8_t speciesId, const std::string& name,
                                   const std::string& pyPath) {
	try {
		// pyPath is expected to be "<name>" (no .py), module lookup via
		// species/ on sys.path above.
		py::module_ mod = py::module_::import(pyPath.c_str());
		if (!py::hasattr(mod, "decide_batch")) {
			std::fprintf(stderr,
				"[python_bridge] species %s (%s): missing decide_batch()\n",
				name.c_str(), pyPath.c_str());
			return false;
		}
		impl_->decideBatchFn[speciesId] = mod.attr("decide_batch");
		impl_->speciesName[speciesId] = name;
		std::fprintf(stderr,
			"[python_bridge] loaded species id=%u name=%s module=%s\n",
			(unsigned)speciesId, name.c_str(), pyPath.c_str());
		return true;
	} catch (const std::exception& e) {
		std::fprintf(stderr,
			"[python_bridge] failed to load species %s (%s): %s\n",
			name.c_str(), pyPath.c_str(), e.what());
		return false;
	}
}

bool PythonBridge::hasSpecies(uint8_t id) const {
	return impl_->decideBatchFn.find(id) != impl_->decideBatchFn.end();
}

void PythonBridge::decideBatch(uint8_t speciesId,
                               const std::vector<CellView>& cellsSame,
                               const std::vector<FoodView>& food,
                               const std::vector<CellView>& others,
                               std::vector<Decision>& out) {
	out.clear();
	out.reserve(cellsSame.size());
	auto it = impl_->decideBatchFn.find(speciesId);
	if (it == impl_->decideBatchFn.end() || cellsSame.empty()) {
		for (const auto& c : cellsSame) out.push_back({c.angle, 1.0f});
		return;
	}
	try {
		py::object result = it->second(
			toList(cellsSame), toList(food), toList(others));
		py::list lst = result.cast<py::list>();
		size_t n = std::min(lst.size(), cellsSame.size());
		for (size_t i = 0; i < n; ++i) {
			py::tuple t = lst[i].cast<py::tuple>();
			float a = t[0].cast<float>();
			float s = t[1].cast<float>();
			if (s < 0.f) s = 0.f;
			if (s > 1.f) s = 1.f;
			out.push_back({a, s});
		}
		// Pad any remainder with identity so the sim stays deterministic.
		for (size_t i = n; i < cellsSame.size(); ++i) {
			out.push_back({cellsSame[i].angle, 1.0f});
		}
	} catch (const std::exception& e) {
		std::fprintf(stderr,
			"[python_bridge] decide_batch(species=%u) threw: %s\n",
			(unsigned)speciesId, e.what());
		for (const auto& c : cellsSame) out.push_back({c.angle, 1.0f});
	}
}

} // namespace evocraft
