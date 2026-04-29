#pragma once

// Canonical world-template registration. Single source of truth for the
// integer templateIndex the server passes to ConfigurableWorldTemplate
// AND the artifact-id the client world picker shows. Adding a new world
// is one entry here plus the .py artifact file.
//
// Why a header (not a .py registry): both server (server/main.cpp) and
// client (CEF world picker, lazy server spawn) need to convert between
// `id` and `templateIndex` without booting Python. The mapping rarely
// changes; a 7-entry constexpr table beats a runtime lookup.

#include <cstring>
#include <string>

namespace solarium {

struct WorldTemplateInfo {
	const char* id;            // matches the artifact file stem
	const char* artifactPath;  // relative to exec dir
	const char* fallbackName;  // shown if the artifact lookup fails
};

// Order is the contract: kWorldTemplates[i].id is the i-th templateIndex.
// Server's buildWorldTemplates() iterates this; client world picker
// resolves a picked id to its index by linear scan.
inline constexpr WorldTemplateInfo kWorldTemplates[] = {
	{ "village",         "artifacts/worlds/base/village.py",         "Village" },
	{ "test_behaviors",  "artifacts/worlds/base/test_behaviors.py",  "Test Behaviors" },
	{ "test_dog",        "artifacts/worlds/base/test_dog.py",        "Test Dog" },
	{ "test_villager",   "artifacts/worlds/base/test_villager.py",   "Test Villager" },
	{ "test_chicken",    "artifacts/worlds/base/test_chicken.py",    "Test Chicken" },
	{ "perf_stress",     "artifacts/worlds/base/perf_stress.py",     "Perf Stress" },
	{ "toronto",         "artifacts/worlds/base/toronto.py",         "Toronto (Voxel Earth)" },
	// Dynamic voxel-earth world driven by SOLARIUM_VOXEL_* env vars; used
	// by `make world LAT=… LNG=… RADIUS=…` (and aliases like `make wonderland`)
	// to load any location Google has 3D Tiles for without adding a new
	// hardcoded artifact each time. toronto.py stays as-is so existing
	// saves keep their templateIndex.
	{ "voxel_earth",     "artifacts/worlds/base/voxel_earth_dynamic.py", "Voxel Earth" },
};

inline constexpr size_t kWorldTemplateCount =
	sizeof(kWorldTemplates) / sizeof(kWorldTemplates[0]);

// Returns the template index for the given id, or -1 if unknown.
inline int worldTemplateIndexOf(const std::string& id) {
	for (size_t i = 0; i < kWorldTemplateCount; ++i)
		if (id == kWorldTemplates[i].id) return (int)i;
	return -1;
}

inline const char* worldTemplateIdAt(int index) {
	if (index < 0 || (size_t)index >= kWorldTemplateCount) return "";
	return kWorldTemplates[index].id;
}

} // namespace solarium
