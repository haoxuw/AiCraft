#pragma once

/**
 * World persistence — save/load world state to disk.
 *
 * Save format:
 *   saves/{world_name}/
 *     world.json           — metadata (name, seed, template, time, spawn)
 *     chunks/
 *       {x}_{y}_{z}.chunk  — binary: 4096 × uint16_t block IDs (8KB each)
 *     entities.bin          — all entity state (position, velocity, props)
 *     blockstates.bin       — active block state map
 *
 * Only chunks that have been modified are saved. Generated-only chunks
 * are regenerated from seed + template on load.
 */

#include "server/server.h"
#include "shared/net_protocol.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <ctime>

namespace agentworld {

struct WorldMetadata {
	std::string name;
	int seed = 42;
	int templateIndex = 1;
	std::string templateName = "Village";
	std::string gameMode = "playing";
	float worldTime = 0.30f;
	glm::vec3 spawnPos = {30, 5, 30};
	std::string lastPlayed;
	int version = 1;
};

// ================================================================
// Save
// ================================================================

inline bool saveWorld(GameServer& server, const std::string& savePath, const WorldMetadata& meta) {
	namespace fs = std::filesystem;
	fs::create_directories(savePath + "/chunks");

	// --- world.json ---
	{
		std::ofstream f(savePath + "/world.json");
		if (!f.is_open()) {
			printf("[WorldSave] Failed to create %s/world.json\n", savePath.c_str());
			return false;
		}

		// Get current time
		time_t now = time(nullptr);
		char timeBuf[64];
		strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%dT%H:%M:%S", localtime(&now));

		f << "{\n";
		f << "  \"name\": \"" << meta.name << "\",\n";
		f << "  \"seed\": " << meta.seed << ",\n";
		f << "  \"templateIndex\": " << meta.templateIndex << ",\n";
		f << "  \"templateName\": \"" << meta.templateName << "\",\n";
		f << "  \"gameMode\": \"" << meta.gameMode << "\",\n";
		f << "  \"worldTime\": " << server.worldTime() << ",\n";
		f << "  \"spawnPos\": [" << server.spawnPos().x << ", "
		  << server.spawnPos().y << ", " << server.spawnPos().z << "],\n";
		f << "  \"lastPlayed\": \"" << timeBuf << "\",\n";
		f << "  \"version\": " << meta.version << "\n";
		f << "}\n";
	}

	// --- chunks/ ---
	int chunkCount = 0;
	auto& world = server.world();
	// Iterate all loaded chunks and save them
	// We save ALL loaded chunks (they were either generated+modified or just generated).
	// On load, we skip regeneration for saved chunks.
	world.forEachChunk([&](ChunkPos pos, const Chunk& chunk) {
		char filename[128];
		snprintf(filename, sizeof(filename), "%s/chunks/%d_%d_%d.chunk",
		         savePath.c_str(), pos.x, pos.y, pos.z);
		std::ofstream cf(filename, std::ios::binary);
		if (cf.is_open()) {
			auto& blocks = chunk.rawBlocks();
			cf.write(reinterpret_cast<const char*>(blocks.data()),
			         CHUNK_VOLUME * sizeof(BlockId));
			chunkCount++;
		}
	});

	// --- entities.bin ---
	{
		net::WriteBuffer wb;
		int entityCount = 0;
		world.entities.forEach([&](Entity& e) {
			// Skip player entities (they're per-session, re-spawned on connect)
			if (e.typeId() == EntityType::Player) return;

			wb.writeString(e.typeId());
			wb.writeVec3(e.position);
			wb.writeVec3(e.velocity);
			wb.writeF32(e.yaw);

			// Serialize props
			auto& props = e.props();
			wb.writeU32((uint32_t)props.size());
			for (auto& [key, val] : props) {
				wb.writeString(key);
				// Variant index: 0=bool, 1=int, 2=float, 3=string, 4=vec3
				uint8_t idx = (uint8_t)val.index();
				wb.writeU8(idx);
				switch (idx) {
				case 0: wb.writeBool(std::get<bool>(val)); break;
				case 1: wb.writeI32(std::get<int>(val)); break;
				case 2: wb.writeF32(std::get<float>(val)); break;
				case 3: wb.writeString(std::get<std::string>(val)); break;
				case 4: wb.writeVec3(std::get<glm::vec3>(val)); break;
				}
			}
			entityCount++;
		});

		// Write count header + data
		std::ofstream ef(savePath + "/entities.bin", std::ios::binary);
		if (ef.is_open()) {
			uint32_t count = entityCount;
			ef.write(reinterpret_cast<const char*>(&count), 4);
			ef.write(reinterpret_cast<const char*>(wb.data().data()), wb.data().size());
		}
	}

	// --- blockstates.bin ---
	{
		net::WriteBuffer wb;
		uint32_t count = 0;
		for (auto& [pos, state] : world.activeBlocks) {
			wb.writeIVec3({pos.x, pos.y, pos.z});
			wb.writeU32((uint32_t)state.size());
			for (auto& [key, val] : state) {
				wb.writeString(key);
				wb.writeI32(val);
			}
			count++;
		}

		std::ofstream bf(savePath + "/blockstates.bin", std::ios::binary);
		if (bf.is_open()) {
			bf.write(reinterpret_cast<const char*>(&count), 4);
			bf.write(reinterpret_cast<const char*>(wb.data().data()), wb.data().size());
		}
	}

	printf("[WorldSave] Saved to %s (%d chunks)\n", savePath.c_str(), chunkCount);
	return true;
}

// ================================================================
// Load
// ================================================================

inline WorldMetadata loadWorldMetadata(const std::string& savePath) {
	WorldMetadata meta;
	std::ifstream f(savePath + "/world.json");
	if (!f.is_open()) return meta;

	// Simple JSON parsing (no dependency on a JSON library)
	std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

	auto extractStr = [&](const std::string& key) -> std::string {
		std::string pattern = "\"" + key + "\": \"";
		auto pos = content.find(pattern);
		if (pos == std::string::npos) return "";
		auto start = pos + pattern.size();
		auto end = content.find('"', start);
		return (end != std::string::npos) ? content.substr(start, end - start) : "";
	};
	auto extractInt = [&](const std::string& key) -> int {
		std::string pattern = "\"" + key + "\": ";
		auto pos = content.find(pattern);
		if (pos == std::string::npos) return 0;
		return atoi(content.c_str() + pos + pattern.size());
	};
	auto extractFloat = [&](const std::string& key) -> float {
		std::string pattern = "\"" + key + "\": ";
		auto pos = content.find(pattern);
		if (pos == std::string::npos) return 0;
		return (float)atof(content.c_str() + pos + pattern.size());
	};

	meta.name = extractStr("name");
	meta.seed = extractInt("seed");
	meta.templateIndex = extractInt("templateIndex");
	meta.templateName = extractStr("templateName");
	meta.gameMode = extractStr("gameMode");
	meta.worldTime = extractFloat("worldTime");
	meta.lastPlayed = extractStr("lastPlayed");
	meta.version = extractInt("version");

	// Parse spawnPos array
	auto spawnPos = content.find("\"spawnPos\"");
	if (spawnPos != std::string::npos) {
		auto bracket = content.find('[', spawnPos);
		if (bracket != std::string::npos) {
			float x, y, z;
			if (sscanf(content.c_str() + bracket, "[%f, %f, %f]", &x, &y, &z) == 3) {
				meta.spawnPos = {x, y, z};
			}
		}
	}

	return meta;
}

inline bool loadWorld(GameServer& server, const std::string& savePath,
                      const std::vector<std::shared_ptr<WorldTemplate>>& templates) {
	namespace fs = std::filesystem;

	WorldMetadata meta = loadWorldMetadata(savePath);
	if (meta.name.empty()) {
		printf("[WorldSave] Failed to load metadata from %s\n", savePath.c_str());
		return false;
	}

	// Initialize world only (no default entity spawning — save has its own entities)
	ServerConfig config;
	config.seed = meta.seed;
	config.templateIndex = meta.templateIndex;
	server.initWorld(config, templates);

	// Restore saved spawn position and world time
	server.setSpawnPos(meta.spawnPos);
	server.setWorldTime(meta.worldTime);

	auto& world = server.world();

	// --- Load chunks ---
	int chunkCount = 0;
	std::string chunksDir = savePath + "/chunks";
	if (fs::exists(chunksDir)) {
		for (auto& entry : fs::directory_iterator(chunksDir)) {
			if (entry.path().extension() != ".chunk") continue;
			std::string stem = entry.path().stem().string();
			int cx, cy, cz;
			if (sscanf(stem.c_str(), "%d_%d_%d", &cx, &cy, &cz) != 3) continue;

			std::ifstream cf(entry.path(), std::ios::binary);
			if (!cf.is_open()) continue;

			std::array<BlockId, CHUNK_VOLUME> blocks;
			cf.read(reinterpret_cast<char*>(blocks.data()), CHUNK_VOLUME * sizeof(BlockId));

			// Load the chunk (this generates it first, then we overwrite with saved data)
			ChunkPos pos = {cx, cy, cz};
			Chunk* chunk = world.getChunk(pos);
			if (chunk) {
				chunk->setRawBlocks(blocks);
				chunkCount++;
			}
		}
	}

	// --- Load entities ---
	{
		std::ifstream ef(savePath + "/entities.bin", std::ios::binary);
		if (ef.is_open()) {
			uint32_t count;
			ef.read(reinterpret_cast<char*>(&count), 4);

			std::vector<uint8_t> data((std::istreambuf_iterator<char>(ef)),
			                           std::istreambuf_iterator<char>());
			net::ReadBuffer rb(data.data(), data.size());

			for (uint32_t i = 0; i < count && rb.hasMore(); i++) {
				std::string typeId = rb.readString();
				glm::vec3 pos = rb.readVec3();
				glm::vec3 vel = rb.readVec3();
				float yaw = rb.readF32();

				// Read props
				uint32_t propCount = rb.readU32();
				std::unordered_map<std::string, PropValue> props;
				for (uint32_t p = 0; p < propCount && rb.hasMore(); p++) {
					std::string key = rb.readString();
					uint8_t idx = rb.readU8();

					PropValue val;
					switch (idx) {
					case 0: val = rb.readBool(); break;
					case 1: val = (int)rb.readI32(); break;
					case 2: val = rb.readF32(); break;
					case 3: val = rb.readString(); break;
					case 4: val = rb.readVec3(); break;
					default: val = 0; break;
					}
					props[key] = val;
				}

				EntityId eid = world.entities.spawn(typeId, pos, props);
				Entity* e = world.entities.get(eid);
				if (e) {
					e->velocity = vel;
					e->yaw = yaw;
				}
			}
		}
	}

	// --- Load block states ---
	{
		std::ifstream bf(savePath + "/blockstates.bin", std::ios::binary);
		if (bf.is_open()) {
			uint32_t count;
			bf.read(reinterpret_cast<char*>(&count), 4);

			std::vector<uint8_t> data((std::istreambuf_iterator<char>(bf)),
			                           std::istreambuf_iterator<char>());
			net::ReadBuffer rb(data.data(), data.size());

			for (uint32_t i = 0; i < count && rb.hasMore(); i++) {
				glm::ivec3 pos = rb.readIVec3();
				uint32_t stateCount = rb.readU32();
				BlockStateMap state;
				for (uint32_t s = 0; s < stateCount && rb.hasMore(); s++) {
					std::string key = rb.readString();
					int val = rb.readI32();
					state[key] = val;
				}
				world.setBlockState(pos.x, pos.y, pos.z, state);
			}
		}
	}

	printf("[WorldSave] Loaded %s (%d chunks, seed=%d, template=%s)\n",
	       meta.name.c_str(), chunkCount, meta.seed, meta.templateName.c_str());
	return true;
}

} // namespace agentworld
