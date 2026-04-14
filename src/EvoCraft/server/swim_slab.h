// EvoCraft Spore Cell Stage simulation — microbes + food in a 2D petri dish.
//
// Server owns positions; Python species modules decide headings (Rule 1 +
// Rule 4: the server runs no AI — it only integrates the (angle, speed)
// returned by decide_batch()).
//
// Layout: top-down on the XZ plane. Y is fixed at 0 (the petri dish is flat).
// Angle is the swim heading in the XZ plane, measured from +X around +Y.
//
// One cell — id PLAYER_CELL_ID — is the player. Its motion is driven by
// C_PLAYER_INPUT (vx, vz) instead of Python AI. Everything else (eating
// food, wall bouncing, snapshot generation) treats it identically to NPC
// microbes.

#pragma once

#include "net_protocol.h"
#include "part_table.h"
#include "python_bridge.h"

#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace evocraft {

class SwimSlab {
public:
	// Petri dish bounds — square XZ plate, generous room for the player to
	// roam before parts/biomes are added.
	static constexpr float HALF_W       = 50.0f;
	static constexpr float HALF_D       = 50.0f;
	static constexpr float BASE_SPD     =  2.4f;
	static constexpr float PLAYER_SPD   =  6.0f;
	static constexpr float EAT_DIST     =  0.7f;
	static constexpr int   FOOD_TARGET  = 60;
	static constexpr uint8_t PLAYER_SPECIES = 3;

	// HP scales with size — bigger cells take more hits. A unit-size cell
	// has 10 HP; double-size has 20. Keeps damage feel consistent across
	// the 10 size tiers Spore uses.
	static constexpr float HP_PER_SIZE     = 10.0f;
	// A larger cell can damage a smaller one if its size exceeds threshold
	// times the smaller's. Below threshold = peer (no contact damage).
	static constexpr float PRED_SIZE_RATIO = 1.20f;
	// Damage-per-second a predator deals while in contact with prey.
	static constexpr float PRED_DPS        = 12.0f;
	// DNA reward per food kind eaten (matches Spore-ish ratios).
	static constexpr uint32_t DNA_PLANT = 2;
	static constexpr uint32_t DNA_MEAT  = 4;
	static constexpr uint32_t DNA_EGG   = 6;

	void populate(int npcCount) {
		std::mt19937 rng(42);
		std::uniform_real_distribution<float> ux(-HALF_W + 1, HALF_W - 1);
		std::uniform_real_distribution<float> uz(-HALF_D + 1, HALF_D - 1);
		std::uniform_real_distribution<float> ua(-M_PI, M_PI);

		cells_.clear();
		nextCellId_ = net::PLAYER_CELL_ID + 1;
		playerDna_ = 0;
		playerParts_.clear();
		partsDirty_ = true;

		// Player cell — reserved id, green blob, center spawn. Starts as
		// carnivore (jaw mouth) per Spore default; editor will let player
		// switch later.
		Cell p{};
		p.id      = net::PLAYER_CELL_ID;
		p.x       = 0.0f;
		p.y       = 0.0f;
		p.z       = 0.0f;
		p.angle   = 0.0f;
		p.speed   = 0.0f;
		p.species = PLAYER_SPECIES;
		p.size    = 2.4f;
		p.maxHp   = p.size * HP_PER_SIZE;
		p.hp      = p.maxHp;
		p.diet    = net::DIET_CARNIVORE;
		cells_.push_back(p);

		// Per-species defaults: amoeba=carnivore (slow but bites), ciliate=
		// herbivore (radial filter ring), flagellate=omnivore (proboscis tail).
		for (int i = 0; i < npcCount; ++i) {
			uint8_t sp = (uint8_t)(i % 3);
			Cell c{};
			c.id      = nextCellId_++;
			c.x       = ux(rng);
			c.y       = 0.0f;
			c.z       = uz(rng);
			c.angle   = ua(rng);
			c.speed   = BASE_SPD;
			c.species = sp;
			c.size    = (sp == 0 ? 1.30f : (sp == 1 ? 1.55f : 1.15f));
			c.maxHp   = c.size * HP_PER_SIZE;
			c.hp      = c.maxHp;
			c.diet    = (sp == 0 ? net::DIET_CARNIVORE
			           : sp == 1 ? net::DIET_HERBIVORE
			           :           net::DIET_OMNIVORE);
			cells_.push_back(c);
		}

		nextFoodId_ = 1;
		food_.clear();
		for (int i = 0; i < FOOD_TARGET; ++i) spawnFood(rng);
	}

	uint32_t playerDna() const {
		return playerDna_;
	}

	// Test/debug helper — bypass the food grind so the editor can be exercised
	// from a fresh server. Wired to --seed-dna on main.cpp.
	void grantPlayerDna(uint32_t amount) {
		playerDna_ += amount;
	}

	const std::vector<net::PartRecord>& playerParts() const {
		return playerParts_;
	}

	// Part state changed (buy/reset). Server uses this flag to decide when
	// to broadcast S_PLAYER_PARTS — these changes are rare so broadcasting
	// every tick would waste bandwidth on a constant snapshot.
	bool consumePartsDirty() {
		bool d = partsDirty_;
		partsDirty_ = false;
		return d;
	}

	// Try to buy + attach a part. Returns true if accepted (DNA deducted,
	// part appended). Validation: kind in range, DNA sufficient, count cap not
	// exceeded, distance clamped to [0,1]. Mouths are mutually exclusive —
	// adding a new mouth removes the old one and refunds its DNA. The new
	// mouth also flips the player's diet.
	bool tryBuyPart(uint8_t kind, float angle, float distance) {
		if (kind >= net::PART_COUNT) return false;
		uint16_t cost = partCost(kind);

		// Mutually exclusive mouth: drop existing one first (refund), then
		// re-evaluate cost.
		if (isMouth(kind)) {
			for (auto it = playerParts_.begin(); it != playerParts_.end(); ) {
				if (isMouth(it->kind)) {
					playerDna_ += partCost(it->kind);
					it = playerParts_.erase(it);
				} else {
					++it;
				}
			}
		}

		if (playerDna_ < cost) return false;

		// Per-kind cap.
		uint8_t cap = partMaxCount(kind);
		uint8_t cnt = 0;
		for (const auto& p : playerParts_) if (p.kind == kind) cnt++;
		if (cnt >= cap) return false;

		float d = std::max(0.0f, std::min(1.0f, distance));
		playerParts_.push_back({kind, angle, d});
		playerDna_ -= cost;

		// Apply diet on mouth attach.
		if (isMouth(kind)) {
			net::Diet diet = partTable()[kind].implDiet;
			for (auto& c : cells_) {
				if (c.id == net::PLAYER_CELL_ID) {
					c.diet = (uint8_t)diet;
					break;
				}
			}
		}
		partsDirty_ = true;
		return true;
	}

	// Wipe all parts and refund every DNA point. Lets the player iterate
	// builds without grinding food again.
	void resetParts() {
		for (const auto& p : playerParts_) playerDna_ += partCost(p.kind);
		playerParts_.clear();
		partsDirty_ = true;
	}

	// Player HP/maxHP for the per-tick stats broadcast. Returns (0, 0) if
	// the player cell is dead/missing.
	std::pair<float, float> playerHp() const {
		for (const auto& c : cells_) {
			if (c.id == net::PLAYER_CELL_ID) {
				return {c.hp, c.maxHp};
			}
		}
		return {0.0f, 0.0f};
	}

	// Set desired velocity for the player cell. (vx, vz) is treated as a
	// direction; magnitude is clamped to [0, 1] then scaled by PLAYER_SPD.
	void setPlayerInput(float vx, float vz) {
		playerVx_ = vx;
		playerVz_ = vz;
	}

	void step(float dt, PythonBridge* bridge) {
		t_ += dt;

		// --- dispatch per NPC species --------------------------------------
		// Python sees the player as "another cell" via the others view so
		// behaviors can react to it (chase, flee). The player itself is
		// excluded from same-species batches.
		if (bridge) {
			for (uint8_t sp = 0; sp < 3; ++sp) {
				if (!bridge->hasSpecies(sp)) continue;
				std::vector<CellView> same;
				std::vector<CellView> others;
				std::vector<size_t> indices;
				same.reserve(cells_.size());
				indices.reserve(cells_.size());
				for (size_t i = 0; i < cells_.size(); ++i) {
					const auto& c = cells_[i];
					CellView v{c.id, c.x, c.y, c.z, c.angle, c.speed, c.species};
					if (c.species == sp && c.id != net::PLAYER_CELL_ID) {
						same.push_back(v);
						indices.push_back(i);
					} else {
						others.push_back(v);
					}
				}
				if (same.empty()) continue;

				std::vector<FoodView> foodView;
				foodView.reserve(food_.size());
				for (const auto& f : food_) foodView.push_back({f.id, f.x, f.y, f.z});

				std::vector<Decision> out;
				bridge->decideBatch(sp, same, foodView, others, out);
				for (size_t k = 0; k < indices.size(); ++k) {
					auto& c = cells_[indices[k]];
					float desired = out[k].newAngle;
					float da = wrapPi(desired - c.angle);
					c.angle += std::copysign(std::min(std::abs(da), 3.5f * dt),
						da);
					c.speed = BASE_SPD * (0.4f + 0.6f * out[k].speedMul);
				}
			}
		}

		// --- integrate motion (XZ plane) + wall bounce ---------------------
		for (auto& c : cells_) {
			if (c.id == net::PLAYER_CELL_ID) {
				// Player input is a direction vector; clamp magnitude to 1
				// so diagonal isn't faster than cardinal.
				float vx = playerVx_, vz = playerVz_;
				float m  = std::sqrt(vx * vx + vz * vz);
				if (m > 1.0f) { vx /= m; vz /= m; m = 1.0f; }
				c.x    += vx * PLAYER_SPD * dt;
				c.z    += vz * PLAYER_SPD * dt;
				c.speed = m * PLAYER_SPD;
				if (m > 0.01f) c.angle = std::atan2(vz, vx);
			} else {
				c.x += std::cos(c.angle) * c.speed * dt;
				c.z += std::sin(c.angle) * c.speed * dt;
			}
			c.y = 0.0f;

			if (c.x >  HALF_W) { c.x =  HALF_W; c.angle = (float)M_PI - c.angle; }
			if (c.x < -HALF_W) { c.x = -HALF_W; c.angle = (float)M_PI - c.angle; }
			if (c.z >  HALF_D) { c.z =  HALF_D; c.angle = -c.angle; }
			if (c.z < -HALF_D) { c.z = -HALF_D; c.angle = -c.angle; }
		}

		// --- eating: diet-gated pickup. Only cells whose diet allows the
		//     food kind can consume it. Player accrues DNA points.
		for (size_t fi = 0; fi < food_.size();) {
			auto& f = food_[fi];
			bool eaten = false;
			for (auto& c : cells_) {
				if (!canEat(c.diet, f.kind)) continue;
				float reach = EAT_DIST + c.size * 0.5f;
				float dx = c.x - f.x, dz = c.z - f.z;
				if (dx * dx + dz * dz < reach * reach) {
					// Heal eater + DNA reward (player only).
					float heal = (f.kind == net::FOOD_PLANT ? 2.0f
					            : f.kind == net::FOOD_MEAT  ? 4.0f
					            :                              6.0f);
					c.hp = std::min(c.maxHp, c.hp + heal);
					if (c.id == net::PLAYER_CELL_ID) {
						playerDna_ += (f.kind == net::FOOD_PLANT ? DNA_PLANT
						             : f.kind == net::FOOD_MEAT  ? DNA_MEAT
						             :                              DNA_EGG);
					}
					eaten = true;
					break;
				}
			}
			if (eaten) {
				food_[fi] = food_.back();
				food_.pop_back();
			} else {
				++fi;
			}
		}

		// --- top up food pool on a slow trickle. Spawn a mix of plant
		//     (most common) + occasional egg; meat only drops from kills.
		foodSpawnAcc_ += dt;
		while (foodSpawnAcc_ > 0.4f && (int)food_.size() < FOOD_TARGET) {
			foodSpawnAcc_ -= 0.4f;
			spawnFood(rng_);
		}

		// --- predation: O(n²) close-contact damage (n≈60, fine). When two
		//     cells are within touch range and one is at least PRED_SIZE_RATIO
		//     larger, the larger damages the smaller at PRED_DPS.
		for (size_t i = 0; i < cells_.size(); ++i) {
			for (size_t j = i + 1; j < cells_.size(); ++j) {
				auto& a = cells_[i];
				auto& b = cells_[j];
				float touch = (a.size + b.size) * 0.5f;
				float dx = a.x - b.x, dz = a.z - b.z;
				if (dx * dx + dz * dz > touch * touch) continue;
				if (a.size >= b.size * PRED_SIZE_RATIO) {
					b.hp -= PRED_DPS * dt;
				} else if (b.size >= a.size * PRED_SIZE_RATIO) {
					a.hp -= PRED_DPS * dt;
				}
			}
		}

		// --- death pass: any cell with hp ≤ 0 drops a meat chunk and is
		//     removed. Player respawns at center with full HP (DNA carries
		//     over so progress isn't punishingly lost).
		for (size_t i = 0; i < cells_.size();) {
			if (cells_[i].hp <= 0.0f) {
				if (cells_[i].id == net::PLAYER_CELL_ID) {
					cells_[i].x = 0.0f;
					cells_[i].z = 0.0f;
					cells_[i].hp = cells_[i].maxHp;
					++i;
					continue;
				}
				food_.push_back({nextFoodId_++,
					cells_[i].x, 0.0f, cells_[i].z, net::FOOD_MEAT});
				cells_[i] = cells_.back();
				cells_.pop_back();
			} else {
				++i;
			}
		}
	}

	// Diet ↔ food kind compatibility table. Omnivore eats anything; plant
	// goes to herbivore + omnivore; meat goes to carnivore + omnivore;
	// eggs are universal (anyone can eat them).
	static bool canEat(uint8_t diet, uint8_t foodKind) {
		if (diet == net::DIET_OMNIVORE) return true;
		if (foodKind == net::FOOD_EGG)  return true;
		if (foodKind == net::FOOD_PLANT) return diet == net::DIET_HERBIVORE;
		if (foodKind == net::FOOD_MEAT)  return diet == net::DIET_CARNIVORE;
		return false;
	}

	std::vector<net::CellRecord> cellSnapshot() const {
		std::vector<net::CellRecord> out;
		out.reserve(cells_.size());
		for (const auto& c : cells_) {
			out.push_back({c.id, c.x, c.y, c.z, c.angle, c.species,
				c.size, c.hp, c.maxHp, c.diet});
		}
		return out;
	}

	std::vector<net::FoodRecord> foodSnapshot() const {
		std::vector<net::FoodRecord> out;
		out.reserve(food_.size());
		for (const auto& f : food_) out.push_back({f.id, f.x, f.y, f.z, f.kind});
		return out;
	}

	size_t cellCount() const { return cells_.size(); }
	size_t foodCount() const { return food_.size(); }

private:
	struct Cell {
		uint32_t id;
		float x, y, z;
		float angle;
		float speed;
		uint8_t species;
		float size;
		float hp;
		float maxHp;
		uint8_t diet;
	};
	struct Food {
		uint32_t id;
		float x, y, z;
		uint8_t kind;
	};

	// Randomly placed food. ~80% plant (most common in Spore early-stage
	// pond), ~20% egg. Meat only spawns from cell deaths in step().
	void spawnFood(std::mt19937& rng) {
		std::uniform_real_distribution<float> ux(-HALF_W + 1, HALF_W - 1);
		std::uniform_real_distribution<float> uz(-HALF_D + 1, HALF_D - 1);
		std::uniform_real_distribution<float> uk(0.0f, 1.0f);
		uint8_t kind = (uk(rng) < 0.80f) ? net::FOOD_PLANT : net::FOOD_EGG;
		food_.push_back({nextFoodId_++, ux(rng), 0.0f, uz(rng), kind});
	}

	static float wrapPi(float a) {
		while (a >  M_PI) a -= 2.0f * (float)M_PI;
		while (a < -M_PI) a += 2.0f * (float)M_PI;
		return a;
	}

	std::vector<Cell>  cells_;
	std::vector<Food>  food_;
	std::mt19937       rng_{1337};
	float              t_ = 0.f;
	float              foodSpawnAcc_ = 0.f;
	uint32_t           nextCellId_ = net::PLAYER_CELL_ID + 1;
	uint32_t           nextFoodId_ = 1;
	float              playerVx_ = 0.f;
	float              playerVz_ = 0.f;
	uint32_t           playerDna_ = 0;
	std::vector<net::PartRecord> playerParts_;
	bool               partsDirty_ = true;  // ensure first-frame broadcast
};

} // namespace evocraft
