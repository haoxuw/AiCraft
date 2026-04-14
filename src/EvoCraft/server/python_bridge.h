// Python species dispatcher — pybind11 embed.
//
// Each species is a .py file under evocraft_artifacts/species/<name>.py with a
// top-level function:
//
//     def decide_batch(cells, food, others) -> list[(angle, speed)]
//
// cells:  list of {id, x, y, z, angle, speed} (same-species)
// food:   list of {id, x, y, z}
// others: list of {id, x, y, z, species}   (every other cell, cross-species)
// return: list of (new_angle_rad, speed_multiplier_0_to_1) — one entry per
//         cell, in the same order as `cells`.
//
// The bridge is pure dispatch — all behavior lives in Python. Server-side
// positions/food/eating logic stays in C++ (Rule: server owns world state).

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pybind11 { class scoped_interpreter; class object; }

namespace evocraft {

struct CellView {
	uint32_t id;
	float    x, y, z;
	float    angle;
	float    speed;
	uint8_t  species;
};

struct FoodView {
	uint32_t id;
	float    x, y, z;
};

struct Decision {
	float newAngle;
	float speedMul;   // 0..1 multiplier applied to the species' base speed
};

class PythonBridge {
public:
	PythonBridge();
	~PythonBridge();

	// Register species name → file path (relative to CWD, typically
	// "evocraft_artifacts/species/<name>.py"). speciesId must match the
	// uint8_t used on the wire.
	bool registerSpecies(uint8_t speciesId, const std::string& name,
	                     const std::string& pyPath);

	// Run decide_batch for one species; cellsSameSpecies[i] maps to out[i].
	// Any Python error is logged and the output is padded with identity
	// decisions so the sim keeps running.
	void decideBatch(uint8_t speciesId,
	                 const std::vector<CellView>& cellsSameSpecies,
	                 const std::vector<FoodView>& food,
	                 const std::vector<CellView>& allOthers,
	                 std::vector<Decision>& out);

	bool hasSpecies(uint8_t id) const;

private:
	struct Impl;
	Impl* impl_;
};

} // namespace evocraft
