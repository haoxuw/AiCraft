#pragma once

// Save format: saves/{world}/ with world.json, chunks/{x}_{y}_{z}.chunk,
// entities.bin, blockstates.bin, inventories.bin, owned_entities.bin.
// All loaded chunks saved; generated-only chunks regenerate from seed on load.

#include "server/server.h"
#include "net/net_protocol.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <ctime>

namespace civcraft {

struct WorldMetadata {
	std::string name;
	int seed = 42;
	int templateIndex = 0;
	std::string templateName = "Village";
	std::string gameMode = "playing";
	float worldTime = 0.30f;
	glm::vec3 spawnPos = {30, 5, 30};
	std::string lastPlayed;
	// v1: entities only (typeId/pos/vel/yaw/props). v2: + per-entity inventory.
	int version = 2;
};

inline bool saveWorld(GameServer& server, const std::string& savePath, const WorldMetadata& meta) {
	namespace fs = std::filesystem;
	fs::create_directories(savePath + "/chunks");

	// world.json
	{
		std::ofstream f(savePath + "/world.json");
		if (!f.is_open()) {
			printf("[WorldSave] Failed to create %s/world.json\n", savePath.c_str());
			return false;
		}

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

	// chunks/ — save all loaded chunks; load skips regeneration for saved ones.
	int chunkCount = 0;
	auto& world = server.world();
	world.forEachChunk([&](ChunkPos pos, const Chunk& chunk) {
		char filename[128];
		snprintf(filename, sizeof(filename), "%s/chunks/%d_%d_%d.chunk",
		         savePath.c_str(), pos.x, pos.y, pos.z);
		std::ofstream cf(filename, std::ios::binary);
		if (cf.is_open()) {
			// Format v2: u8 mode + payload.
			//   Lite (mode=0): u16 bid + u8 appearance = 3 byte payload (4 B total).
			//   Full (mode=1): 4096×u16 blocks + 4096×u8 param2 + 4096×u8 app
			//                  = 16384 byte payload (16385 B total).
			// Legacy headerless v1: 8192 / 12288 / 16384 raw bytes — see load.
			if (chunk.isLite()) {
				uint8_t mode = 0;
				cf.write(reinterpret_cast<const char*>(&mode), 1);
				BlockId bid = chunk.liteBid();
				uint8_t app = chunk.liteAppearance();
				cf.write(reinterpret_cast<const char*>(&bid), sizeof(BlockId));
				cf.write(reinterpret_cast<const char*>(&app), 1);
			} else {
				uint8_t mode = 1;
				cf.write(reinterpret_cast<const char*>(&mode), 1);
				std::array<BlockId, CHUNK_VOLUME> blocks;
				std::array<uint8_t, CHUNK_VOLUME> p2, app;
				for (int i = 0; i < CHUNK_VOLUME; ++i) {
					blocks[i] = chunk.getRaw(i);
					p2[i]     = chunk.getRawParam2(i);
					app[i]    = chunk.getRawAppearance(i);
				}
				cf.write(reinterpret_cast<const char*>(blocks.data()),
				         CHUNK_VOLUME * sizeof(BlockId));
				cf.write(reinterpret_cast<const char*>(p2.data()), CHUNK_VOLUME);
				cf.write(reinterpret_cast<const char*>(app.data()), CHUNK_VOLUME);
			}
			chunkCount++;
		}
	});

	// entities.bin — skip players (per-session, re-spawned on connect).
	{
		net::WriteBuffer wb;
		int entityCount = 0;
		world.entities.forEach([&](Entity& e) {
			// Playable creatures re-spawn on connect; skip so we don't persist them twice.
			if (e.def().playable) return;

			wb.writeString(e.typeId());
			wb.writeVec3(e.position);
			wb.writeVec3(e.velocity);
			wb.writeF32(e.yaw);

			auto& props = e.props();
			wb.writeU32((uint32_t)props.size());
			for (auto& [key, val] : props) {
				wb.writeString(key);
				// variant: 0=bool 1=int 2=float 3=string 4=vec3
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

			// v2+ inventory: [u8 hasInv] items + equipment
			if (e.inventory) {
				wb.writeU8(1);
				auto items = e.inventory->items();
				wb.writeU32((uint32_t)items.size());
				for (auto& [id, cnt] : items) {
					wb.writeString(id);
					wb.writeI32(cnt);
				}
				uint8_t equipCount = 0;
				for (int i = 0; i < WEAR_SLOT_COUNT; i++)
					if (!e.inventory->equipped((WearSlot)i).empty()) equipCount++;
				wb.writeU8(equipCount);
				for (int i = 0; i < WEAR_SLOT_COUNT; i++) {
					const auto& eq = e.inventory->equipped((WearSlot)i);
					if (!eq.empty()) {
						wb.writeString(equipSlotName((WearSlot)i));
						wb.writeString(eq);
					}
				}
			} else {
				wb.writeU8(0);
			}
			entityCount++;
		});

		std::ofstream ef(savePath + "/entities.bin", std::ios::binary);
		if (ef.is_open()) {
			uint32_t count = entityCount;
			ef.write(reinterpret_cast<const char*>(&count), 4);
			ef.write(reinterpret_cast<const char*>(wb.data().data()), wb.data().size());
		}
	}

	// blockstates.bin
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

	// inventories.bin — per-character inventories, keyed by character_skin.
	{
		net::WriteBuffer wb;
		uint32_t count = 0;
		world.entities.forEach([&](Entity& e) {
			if (!e.def().playable) return;
			if (!e.inventory) return;
			std::string skin = e.getProp<std::string>("character_skin", "default");
			wb.writeString(skin);
			auto items = e.inventory->items();
			wb.writeU32((uint32_t)items.size());
			for (auto& [id, cnt] : items) {
				wb.writeString(id);
				wb.writeI32(cnt);
			}
			// equipment: [u8 count][{slot, id}...]
			uint8_t equipCount = 0;
			for (int i = 0; i < WEAR_SLOT_COUNT; i++)
				if (!e.inventory->equipped((WearSlot)i).empty()) equipCount++;
			wb.writeU8(equipCount);
			for (int i = 0; i < WEAR_SLOT_COUNT; i++) {
				const auto& eq = e.inventory->equipped((WearSlot)i);
				if (!eq.empty()) {
					wb.writeString(equipSlotName((WearSlot)i));
					wb.writeString(eq);
				}
			}
			count++;
		});
		// Also flush saved inventories for offline characters.
		for (auto& [skin, inv] : server.savedInventories()) {
			bool online = false;
			world.entities.forEach([&](Entity& e) {
				if (e.def().playable &&
				    e.getProp<std::string>("character_skin", "default") == skin)
					online = true;
			});
			if (online) continue;
			wb.writeString(skin);
			auto items = inv.items();
			wb.writeU32((uint32_t)items.size());
			for (auto& [id, cnt] : items) {
				wb.writeString(id);
				wb.writeI32(cnt);
			}
			uint8_t eqCount = 0;
			for (int i = 0; i < WEAR_SLOT_COUNT; i++)
				if (!inv.equipped((WearSlot)i).empty()) eqCount++;
			wb.writeU8(eqCount);
			for (int i = 0; i < WEAR_SLOT_COUNT; i++) {
				const auto& eq = inv.equipped((WearSlot)i);
				if (!eq.empty()) {
					wb.writeString(equipSlotName((WearSlot)i));
					wb.writeString(eq);
				}
			}
			count++;
		}

		std::ofstream inf(savePath + "/inventories.bin", std::ios::binary);
		if (inf.is_open()) {
			inf.write(reinterpret_cast<const char*>(&count), 4);
			inf.write(reinterpret_cast<const char*>(wb.data().data()), wb.data().size());
			printf("[WorldSave] Saved %d character inventories\n", count);
		}
	}

	// owned_entities.bin — NPC snapshots for logged-out players. Shape mirrors
	// entities.bin: per-seat groups of { typeId, pos, vel, yaw, props, inv, nav }.
	// Phase 5: keyed by SeatId (was character_skin); old saves from the
	// pre-Phase-5 layout are rejected by load. Format:
	//   u32 seatCount, then seatCount × { u32 seatId, u32 entCount, entCount × snap }.
	{
		net::WriteBuffer wb;
		uint32_t seatCount = 0;
		auto writeSnap = [&](const OwnedEntitySnapshot& s) {
			wb.writeString(s.typeId);
			wb.writeVec3(s.position);
			wb.writeVec3(s.velocity);
			wb.writeF32(s.yaw);
			wb.writeU32((uint32_t)s.props.size());
			for (auto& [key, val] : s.props) {
				wb.writeString(key);
				uint8_t idx = (uint8_t)val.index();
				wb.writeU8(idx);
				switch (idx) {
				case 0: wb.writeBool(std::get<bool>(val));       break;
				case 1: wb.writeI32 (std::get<int>(val));        break;
				case 2: wb.writeF32 (std::get<float>(val));      break;
				case 3: wb.writeString(std::get<std::string>(val)); break;
				case 4: wb.writeVec3(std::get<glm::vec3>(val));  break;
				}
			}
			wb.writeU32((uint32_t)s.items.size());
			for (auto& [id, cnt] : s.items) { wb.writeString(id); wb.writeI32(cnt); }
			wb.writeU8((uint8_t)s.equipment.size());
			for (auto& [slot, id] : s.equipment) { wb.writeString(slot); wb.writeString(id); }
			// v1 trailer kept for on-disk back-compat: nav state was [bool][vec3];
			// always write zeros now so older builds can still load new saves.
			wb.writeBool(false);
			wb.writeVec3(glm::vec3(0));
		};
		for (auto& [seatId, snaps] : server.ownedEntities().all()) {
			if (snaps.empty() || seatId == SEAT_NONE) continue;
			wb.writeU32(seatId);
			wb.writeU32((uint32_t)snaps.size());
			for (auto& s : snaps) writeSnap(s);
			seatCount++;
		}
		std::ofstream of(savePath + "/owned_entities.bin", std::ios::binary);
		if (of.is_open()) {
			of.write(reinterpret_cast<const char*>(&seatCount), 4);
			of.write(reinterpret_cast<const char*>(wb.data().data()), wb.data().size());
			printf("[WorldSave] Saved owned-entity snapshots for %u seat(s)\n", seatCount);
		}
	}

	// seats.bin — persistent uuid → SeatId map. Tiny file; simple layout:
	//   u32 count, then count × { string uuid, u32 seatId }.
	{
		net::WriteBuffer wb;
		uint32_t count = 0;
		for (auto& [uuid, seatId] : server.seats().all()) {
			if (uuid.empty() || seatId == SEAT_NONE) continue;
			wb.writeString(uuid);
			wb.writeU32(seatId);
			count++;
		}
		std::ofstream sf(savePath + "/seats.bin", std::ios::binary);
		if (sf.is_open()) {
			sf.write(reinterpret_cast<const char*>(&count), 4);
			sf.write(reinterpret_cast<const char*>(wb.data().data()), wb.data().size());
			printf("[WorldSave] Saved %u seat(s)\n", count);
		}
	}

	// villages.bin — per-seat village records (Phase 4). Layout:
	//   u32 count, then count × { u32 id, u32 ownerSeat, i32 cx, i32 cz, u8 status }.
	{
		net::WriteBuffer wb;
		uint32_t count = 0;
		for (auto& r : server.villages().all()) {
			if (r.id == VILLAGE_NONE) continue;
			wb.writeU32(r.id);
			wb.writeU32(r.ownerSeat);
			wb.writeI32(r.centerXZ.x);
			wb.writeI32(r.centerXZ.y);
			wb.writeU8((uint8_t)r.status);
			count++;
		}
		std::ofstream vf(savePath + "/villages.bin", std::ios::binary);
		if (vf.is_open()) {
			vf.write(reinterpret_cast<const char*>(&count), 4);
			vf.write(reinterpret_cast<const char*>(wb.data().data()), wb.data().size());
			printf("[WorldSave] Saved %u village(s)\n", count);
		}
	}

	printf("[WorldSave] Saved to %s (%d chunks)\n", savePath.c_str(), chunkCount);
	return true;
}

inline WorldMetadata loadWorldMetadata(const std::string& savePath) {
	WorldMetadata meta;
	std::ifstream f(savePath + "/world.json");
	if (!f.is_open()) return meta;

	// Hand-rolled JSON parse — avoids a dependency.
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

	// initWorld only — save has its own entities; no default spawn.
	ServerConfig config;
	config.seed = meta.seed;
	config.templateIndex = meta.templateIndex;
	server.initWorld(config, templates);

	server.setSpawnPos(meta.spawnPos);
	server.setWorldTime(meta.worldTime);

	auto& world = server.world();

	// chunks
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

			// File-size discriminator:
			//   4         = v2 Lite (mode=0, u16 bid, u8 app)
			//   16385     = v2 Full (mode=1, then 4096×u16 + 4096×u8 + 4096×u8)
			//   8192      = v1 legacy blocks-only
			//   12288     = v1 legacy blocks + param2
			//   16384     = v1 legacy blocks + param2 + appearance
			cf.seekg(0, std::ios::end);
			auto fileSize = cf.tellg();
			cf.seekg(0, std::ios::beg);

			ChunkPos pos = {cx, cy, cz};
			Chunk* chunk = world.getChunk(pos);
			if (!chunk) continue;

			if (fileSize == (std::streampos)4) {
				uint8_t mode; cf.read(reinterpret_cast<char*>(&mode), 1);
				BlockId bid; cf.read(reinterpret_cast<char*>(&bid), sizeof(BlockId));
				uint8_t app; cf.read(reinterpret_cast<char*>(&app), 1);
				chunk->resetLite(bid, app);
				chunkCount++;
				continue;
			}

			std::array<BlockId, CHUNK_VOLUME> blocks;
			std::array<uint8_t, CHUNK_VOLUME> param2; param2.fill(0);
			std::array<uint8_t, CHUNK_VOLUME> appearance; appearance.fill(0);

			if (fileSize == (std::streampos)16385) {
				// v2 Full — skip 1-byte mode prefix.
				uint8_t mode; cf.read(reinterpret_cast<char*>(&mode), 1);
				cf.read(reinterpret_cast<char*>(blocks.data()), CHUNK_VOLUME * sizeof(BlockId));
				cf.read(reinterpret_cast<char*>(param2.data()), CHUNK_VOLUME);
				cf.read(reinterpret_cast<char*>(appearance.data()), CHUNK_VOLUME);
			} else {
				// v1 legacy formats — headerless.
				cf.read(reinterpret_cast<char*>(blocks.data()), CHUNK_VOLUME * sizeof(BlockId));
				if (fileSize >= (std::streampos)(CHUNK_VOLUME * sizeof(BlockId) + CHUNK_VOLUME))
					cf.read(reinterpret_cast<char*>(param2.data()), CHUNK_VOLUME);
				if (fileSize >= (std::streampos)(CHUNK_VOLUME * sizeof(BlockId) + 2 * CHUNK_VOLUME))
					cf.read(reinterpret_cast<char*>(appearance.data()), CHUNK_VOLUME);
			}

			{
				chunk->setRawBlocks(blocks);
				chunk->setRawParam2Array(param2);
				chunk->setRawAppearanceArray(appearance);
				chunkCount++;
			}
		}
	}

	// entities
	{
		std::ifstream ef(savePath + "/entities.bin", std::ios::binary);
		if (ef.is_open()) {
			uint32_t count;
			ef.read(reinterpret_cast<char*>(&count), 4);

			std::vector<uint8_t> data((std::istreambuf_iterator<char>(ef)),
			                           std::istreambuf_iterator<char>());
			net::ReadBuffer rb(data.data(), data.size());

			// v1: no per-entity inventory. v2+: has it.
			bool hasInventoryData = (meta.version >= 2);

			for (uint32_t i = 0; i < count && rb.hasMore(); i++) {
				std::string typeId = rb.readString();
				glm::vec3 pos = rb.readVec3();
				glm::vec3 vel = rb.readVec3();
				float yaw = rb.readF32();

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

				// v2+ inventory block.
				if (hasInventoryData && rb.hasMore()) {
					uint8_t hasInv = rb.readU8();
					if (hasInv) {
						uint32_t itemCount = rb.readU32();
						std::vector<std::pair<std::string, int>> items;
						items.reserve(itemCount);
						for (uint32_t j = 0; j < itemCount && rb.hasMore(); j++) {
							std::string id = rb.readString();
							int cnt = rb.readI32();
							items.push_back({id, cnt});
						}
						uint8_t equipCount = rb.hasMore() ? rb.readU8() : 0;
						std::vector<std::pair<std::string, std::string>> equips;
						equips.reserve(equipCount);
						for (uint8_t q = 0; q < equipCount && rb.hasMore(); q++) {
							std::string slotName = rb.readString();
							std::string eqId = rb.readString();
							equips.push_back({slotName, eqId});
						}
						if (e && e->inventory) {
							e->inventory->clear();
							for (auto& [id, cnt] : items)
								if (cnt > 0) e->inventory->add(id, cnt);
							for (auto& [slotName, eqId] : equips) {
								WearSlot ws;
								if (!eqId.empty() && wearSlotFromString(slotName, ws)) {
									e->inventory->add(eqId, 1);
									e->inventory->equip(ws, eqId);
								}
							}
						} else if (hasInv) {
							// Def lost its inventory — warn, don't silently drop.
							printf("[WorldSave] Warning: entity type '%s' had saved inventory (%u items) "
							       "but current def has no inventory; discarded.\n",
							       typeId.c_str(), itemCount);
						}
					}
				}
			}
		}
	}

	// blockstates
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

	// inventories.bin — per-character saved inventories
	{
		std::ifstream inf(savePath + "/inventories.bin", std::ios::binary);
		if (inf.is_open()) {
			uint32_t count;
			inf.read(reinterpret_cast<char*>(&count), 4);

			std::vector<uint8_t> data((std::istreambuf_iterator<char>(inf)),
			                           std::istreambuf_iterator<char>());
			net::ReadBuffer rb(data.data(), data.size());

			for (uint32_t i = 0; i < count && rb.hasMore(); i++) {
				std::string skin = rb.readString();
				Inventory inv;
				uint32_t itemCount = rb.readU32();
				for (uint32_t j = 0; j < itemCount && rb.hasMore(); j++) {
					std::string id = rb.readString();
					int cnt = rb.readI32();
					if (cnt > 0) inv.add(id, cnt);
				}
				if (rb.hasMore()) {
					uint8_t equipCount = rb.readU8();
					for (uint8_t e = 0; e < equipCount && rb.hasMore(); e++) {
						std::string slotName = rb.readString();
						std::string eqId = rb.readString();
						WearSlot ws;
						if (!eqId.empty() && wearSlotFromString(slotName, ws)) {
							inv.add(eqId, 1);
							inv.equip(ws, eqId);
						}
					}
				}
				server.savedInventories()[skin] = std::move(inv);
			}
			printf("[WorldSave] Loaded %d saved character inventories\n", count);
		}
	}

	// owned_entities.bin — per-character NPC snapshots
	{
		std::ifstream of(savePath + "/owned_entities.bin", std::ios::binary);
		if (of.is_open()) {
			uint32_t seatCount;
			of.read(reinterpret_cast<char*>(&seatCount), 4);
			std::vector<uint8_t> data((std::istreambuf_iterator<char>(of)),
			                           std::istreambuf_iterator<char>());
			net::ReadBuffer rb(data.data(), data.size());
			uint32_t totalSnaps = 0;
			for (uint32_t i = 0; i < seatCount && rb.hasMore(); i++) {
				SeatId seatId = rb.readU32();
				uint32_t entCount = rb.readU32();
				std::vector<OwnedEntitySnapshot> snaps;
				snaps.reserve(entCount);
				for (uint32_t j = 0; j < entCount && rb.hasMore(); j++) {
					OwnedEntitySnapshot s;
					s.typeId   = rb.readString();
					s.position = rb.readVec3();
					s.velocity = rb.readVec3();
					s.yaw      = rb.readF32();
					uint32_t propCount = rb.readU32();
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
						s.props[key] = val;
					}
					uint32_t itemCount = rb.readU32();
					for (uint32_t k = 0; k < itemCount && rb.hasMore(); k++) {
						std::string id = rb.readString();
						int cnt = rb.readI32();
						s.items.push_back({id, cnt});
					}
					uint8_t eqCount = rb.hasMore() ? rb.readU8() : 0;
					for (uint8_t k = 0; k < eqCount && rb.hasMore(); k++) {
						std::string slot = rb.readString();
						std::string id   = rb.readString();
						s.equipment.push_back({slot, id});
					}
					// Skip legacy nav trailer (bool + vec3) if present; navigation
					// is client-side now, nothing to restore from it.
					if (rb.hasMore()) (void)rb.readBool();
					if (rb.hasMore()) (void)rb.readVec3();
					snaps.push_back(std::move(s));
				}
				totalSnaps += (uint32_t)snaps.size();
				server.ownedEntities().loadFromDisk(seatId, std::move(snaps));
			}
			if (totalSnaps)
				printf("[WorldSave] Loaded %u owned-entity snapshots for %u seat(s)\n",
					totalSnaps, seatCount);
		}
	}

	// seats.bin — inverse of the save step above. Absent on pre-seat worlds;
	// that's fine — the registry starts empty and allocates from 1.
	{
		std::ifstream sf(savePath + "/seats.bin", std::ios::binary);
		if (sf.is_open()) {
			uint32_t count;
			sf.read(reinterpret_cast<char*>(&count), 4);
			std::vector<uint8_t> data((std::istreambuf_iterator<char>(sf)),
			                           std::istreambuf_iterator<char>());
			net::ReadBuffer rb(data.data(), data.size());
			uint32_t loaded = 0;
			for (uint32_t i = 0; i < count && rb.hasMore(); i++) {
				std::string uuid = rb.readString();
				SeatId id = (SeatId)rb.readU32();
				server.seats().loadEntry(uuid, id);
				loaded++;
			}
			if (loaded) printf("[WorldSave] Loaded %u seat(s)\n", loaded);
		}
	}

	// villages.bin — inverse of save above. Absent on pre-Phase-4 worlds;
	// registry starts empty, ids allocate from 1.
	{
		std::ifstream vf(savePath + "/villages.bin", std::ios::binary);
		if (vf.is_open()) {
			uint32_t count;
			vf.read(reinterpret_cast<char*>(&count), 4);
			std::vector<uint8_t> data((std::istreambuf_iterator<char>(vf)),
			                           std::istreambuf_iterator<char>());
			net::ReadBuffer rb(data.data(), data.size());
			uint32_t loaded = 0;
			for (uint32_t i = 0; i < count && rb.hasMore(); i++) {
				VillageRecord r;
				r.id         = (VillageId)rb.readU32();
				r.ownerSeat  = (SeatId)rb.readU32();
				r.centerXZ.x = rb.readI32();
				r.centerXZ.y = rb.readI32();
				r.status     = (VillageRecord::Status)rb.readU8();
				server.villages().loadEntry(r);
				loaded++;
			}
			if (loaded) printf("[WorldSave] Loaded %u village(s)\n", loaded);
		}
	}

	printf("[WorldSave] Loaded %s (%d chunks, seed=%d, template=%s)\n",
	       meta.name.c_str(), chunkCount, meta.seed, meta.templateName.c_str());
	return true;
}

} // namespace civcraft
