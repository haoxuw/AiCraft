// CellCraft — Sim implementation. See sim.h for surface, tuning.h for
// numbers, docs/00_OVERVIEW.md § Action types / § Shape → physics for
// design intent.

#include "CellCraft/sim/sim.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "CellCraft/sim/part_stats.h"
#include "CellCraft/sim/polygon_util.h"
#include "CellCraft/sim/tuning.h"

namespace civcraft::cellcraft::sim {

namespace {
constexpr float TWO_PI = 6.28318530718f;

// Shortest signed angular difference b - a, wrapped to [-pi, pi].
float wrap_angle_diff(float a, float b) {
	float d = std::fmod(b - a + 3.14159265359f + TWO_PI, TWO_PI) - 3.14159265359f;
	return d;
}

glm::vec2 heading_vec(float h) {
	return glm::vec2(std::cos(h), std::sin(h));
}
} // namespace

void Sim::tick(float dt, const std::unordered_map<uint32_t, ActionProposal>& actions) {
	last_damager_.clear();

	// 1) MOVE + CONVERT — read-only over actions, mutate matching monster.
	for (auto& [id, m] : world_->monsters) {
		if (!m.alive) continue;
		auto it = actions.find(id);
		if (it == actions.end()) {
			// Coast: decay thrust to zero via velocity damping.
			m.velocity *= 0.9f;
			continue;
		}
		const ActionProposal& a = it->second;
		switch (a.type) {
		case TYPE_MOVE:
			apply_move_(m, a, dt);
			break;
		case TYPE_CONVERT:
			apply_convert_(m, a);
			// After a convert the monster still coasts this tick; velocity unchanged.
			break;
		case TYPE_RELOCATE:
			// Food pickup happens implicitly via polygon containment in pickup_food_();
			// a RELOCATE action is accepted as a hint but does not bypass the geometry check.
			break;
		case TYPE_INTERACT:
			// Bites are automatic from contact. Reserved for future drawn switches.
			break;
		}
	}

	// 2) Integrate positions and clamp to arena.
	for (auto& [id, m] : world_->monsters) {
		if (!m.alive) continue;
		integrate_and_bound_(m, dt);
	}

	// 3) Food pickup.
	pickup_food_();

	// 4) Monster-monster contact: damage, impulse, bite events.
	resolve_contacts_(dt);

	// 5) Passive poison auras.
	apply_poison_auras_(dt);

	// 6) Status effects (venom DoT) + passive regen.
	apply_status_and_regen_(dt);

	// 7) Kill book-keeping.
	finalize_deaths_();
}

void Sim::apply_move_(Monster& m, const ActionProposal& a, float dt) {
	float diff = wrap_angle_diff(m.heading, a.target_heading);
	float max_step = m.turn_speed * dt;
	if (diff > max_step)  diff = max_step;
	if (diff < -max_step) diff = -max_step;
	m.heading += diff;
	float thrust = std::clamp(a.thrust, 0.0f, 1.0f);
	m.velocity = heading_vec(m.heading) * (thrust * m.move_speed);
}

void Sim::apply_convert_(Monster& m, const ActionProposal& a) {
	switch (a.convert_kind) {
	case CONVERT_SPLIT: {
		// Spend biomass to spawn a child monster copying our shape at reduced scale.
		float cost = std::max(0.0f, a.convert_amount);
		if (cost <= 0.0f || cost > m.biomass) break;
		m.biomass -= cost;
		m.hp_max = std::max(1.0f, m.biomass * HP_PER_BIOMASS);
		if (m.hp > m.hp_max) m.hp = m.hp_max;

		Monster child = m; // copy
		child.id = 0;
		child.biomass = cost;
		child.hp = 0.0f; // World::spawn_monster will fill hp from hp_max
		child.scale_shape(SPLIT_SCALE);
		// Offset child so it does not spawn inside parent.
		glm::vec2 off = heading_vec(m.heading + 3.14159f) * (m.max_core_radius + child.max_core_radius + 4.0f);
		child.core_pos = m.core_pos + off;
		child.velocity = glm::vec2(0.0f);
		uint32_t new_id = world_->spawn_monster(std::move(child));
		Event e{EventKind::SPAWN, new_id, m.id, cost, m.core_pos};
		events_.push_back(e);
		break;
	}
	case CONVERT_GROW: {
		float scale = std::clamp(a.convert_amount, 1.0f, GROW_MAX_SCALE);
		if (scale <= 1.0f) break;
		float old_area = m.area;
		m.scale_shape(scale);
		float added_area = m.area - old_area;
		float cost = std::max(0.0f, added_area * DENSITY);
		if (cost > m.biomass) {
			// Not enough biomass — revert.
			m.scale_shape(1.0f / scale);
			break;
		}
		m.biomass -= cost;
		m.hp_max = std::max(1.0f, m.biomass * HP_PER_BIOMASS);
		if (m.hp > m.hp_max) m.hp = m.hp_max;
		Event e{EventKind::GROW, m.id, 0, scale, m.core_pos};
		events_.push_back(e);
		break;
	}
	case CONVERT_NONE:
	default: break;
	}
}

void Sim::integrate_and_bound_(Monster& m, float dt) {
	m.core_pos += m.velocity * dt;

	float r = glm::length(m.core_pos);
	if (r > world_->map_radius && r > 1e-3f) {
		glm::vec2 inward = -m.core_pos / r;
		float over = r - world_->map_radius;
		m.velocity += inward * (BOUNDARY_K * (over / world_->map_radius)) * dt;
		// Hard clamp so we don't fly off with large dt.
		m.core_pos = (m.core_pos / r) * world_->map_radius;
	}
}

void Sim::pickup_food_() {
	if (world_->food.empty()) return;
	auto& food = world_->food;
	for (auto& [id, m] : world_->monsters) {
		if (!m.alive) continue;
		// Rule: no MOUTH part, no pickup. Monsters must equip a mouth to
		// eat anything at all. Diet then governs yield efficiency.
		if (!m.part_effect.has_mouth) continue;
		auto world_poly = transform_to_world(m.shape, m.core_pos, m.heading);
		// MOUTH widens the effective grab zone using a scaled bounding-circle
		// check around the body — cheap and reads "mouth is bigger" visually.
		float pickup_r = (m.part_effect.pickup_radius_mult > 1.0f)
			? (m.max_core_radius * m.part_effect.pickup_radius_mult)
			: 0.0f;
		float pickup_r2 = pickup_r * pickup_r;
		for (size_t i = 0; i < food.size();) {
			bool hit = point_in_polygon(food[i].pos, world_poly);
			if (!hit && pickup_r2 > 0.0f) {
				glm::vec2 d = food[i].pos - m.core_pos;
				if (d.x * d.x + d.y * d.y <= pickup_r2) hit = true;
			}
			if (hit) {
				float yield = yieldMultiplier(m.part_effect.diet, food[i].type);
				float gained = food[i].biomass * yield;
				m.biomass += gained;
				m.hp_max = std::max(1.0f, m.biomass * HP_PER_BIOMASS);
				m.hp = std::min(m.hp_max, m.hp + gained * 0.25f);
				Event e{EventKind::PICKUP, m.id, food[i].id, gained, food[i].pos};
				events_.push_back(e);
				food[i] = food.back();
				food.pop_back();
			} else {
				++i;
			}
		}
	}
}

void Sim::resolve_contacts_(float dt) {
	(void)dt;
	// Snapshot ids so we can iterate pairs deterministically and without
	// iterator invalidation from any hypothetical mutations.
	std::vector<uint32_t> ids;
	ids.reserve(world_->monsters.size());
	for (auto& [id, m] : world_->monsters) if (m.alive) ids.push_back(id);

	// Cache world-space polygons once per monster per tick.
	std::unordered_map<uint32_t, std::vector<glm::vec2>> world_polys;
	world_polys.reserve(ids.size());
	for (uint32_t id : ids) {
		Monster* m = world_->get(id);
		world_polys[id] = transform_to_world(m->shape, m->core_pos, m->heading);
	}

	for (size_t i = 0; i < ids.size(); ++i) {
		for (size_t j = i + 1; j < ids.size(); ++j) {
			Monster* A = world_->get(ids[i]);
			Monster* B = world_->get(ids[j]);
			if (!A || !B || !A->alive || !B->alive) continue;

			const auto& pa = world_polys[ids[i]];
			const auto& pb = world_polys[ids[j]];
			if (!polygons_overlap(pa, pb)) continue;

			// Rough contact + which vertex of A / B is inside the other.
			glm::vec2 contact_ab, contact_ba;
			size_t a_vert = 0, b_vert = 0;
			bool a_has = rough_contact_point(pa, pb, contact_ab, a_vert);
			bool b_has = rough_contact_point(pb, pa, contact_ba, b_vert);
			glm::vec2 contact = A->core_pos * 0.5f + B->core_pos * 0.5f;
			if (a_has && b_has)      contact = (contact_ab + contact_ba) * 0.5f;
			else if (a_has)          contact = contact_ab;
			else if (b_has)          contact = contact_ba;

			glm::vec2 rel_v = A->velocity - B->velocity;
			glm::vec2 dir_ab = B->core_pos - A->core_pos;
			float dir_len = glm::length(dir_ab);
			if (dir_len < 1e-4f) dir_ab = glm::vec2(1.0f, 0.0f);
			else                 dir_ab /= dir_len;

			float rel_speed = glm::length(rel_v);

			// ---- Part-gated damage (Spore rule) -------------------------
			// Damage requires a damaging part on the attacker whose world-
			// space anchor lies within the contact region. No damaging part
			// within range → zero damage (attacker just bumps the defender).
			auto is_damaging = [](PartType t) {
				return t == PartType::SPIKE || t == PartType::TEETH
				    || t == PartType::HORN  || t == PartType::VENOM_SPIKE;
			};
			auto part_base_dmg = [](PartType t) -> float {
				switch (t) {
				case PartType::SPIKE:       return PART_DMG_BASE_SPIKE;
				case PartType::TEETH:       return PART_DMG_BASE_TEETH;
				case PartType::HORN:        return PART_DMG_BASE_HORN;
				case PartType::VENOM_SPIKE: return PART_DMG_BASE_VENOM_SPIKE;
				default: return 0.0f;
				}
			};
			auto has_damaging_part = [&](const Monster& m) {
				for (const auto& p : m.parts) if (is_damaging(p.type)) return true;
				return false;
			};

			// Compute damage that `atk` deals to `def` through parts that
			// connect with the contact region. Returns (damage, venom_connected).
			auto compute_part_damage = [&](const Monster& atk, const Monster& def,
			                               glm::vec2 dir_world_into_def,
			                               bool& venom_connected) -> float {
				venom_connected = false;
				if (!has_damaging_part(atk)) return 0.0f;
				const float c = std::cos(atk.heading);
				const float s = std::sin(atk.heading);
				const float speed_mag = std::max(rel_speed * PART_DMG_K_SPEED, PART_DMG_K_MIN);
				float total = 0.0f;
				for (const auto& p : atk.parts) {
					if (!is_damaging(p.type)) continue;
					// World-space anchor = atk.core_pos + R(heading) * p.anchor_local
					glm::vec2 wpos(atk.core_pos.x + c * p.anchor_local.x - s * p.anchor_local.y,
					               atk.core_pos.y + s * p.anchor_local.x + c * p.anchor_local.y);
					float reach = PART_CONTACT_RADIUS_K * std::max(0.1f, p.scale);
					glm::vec2 dp = wpos - contact;
					if (dp.x * dp.x + dp.y * dp.y > reach * reach) continue;
					float dmg = part_base_dmg(p.type) * std::max(0.1f, p.scale) * speed_mag;
					// HORN ±15° forward-cone bonus (keeps existing behavior).
					if (p.type == PartType::HORN) {
						glm::vec2 horn_dir_local = p.anchor_local;
						float hl = std::sqrt(horn_dir_local.x * horn_dir_local.x
						                   + horn_dir_local.y * horn_dir_local.y);
						if (hl > 1e-3f) horn_dir_local /= hl;
						else            horn_dir_local = glm::vec2(1.0f, 0.0f);
						glm::vec2 wdir(c * horn_dir_local.x - s * horn_dir_local.y,
						               s * horn_dir_local.x + c * horn_dir_local.y);
						float cd = glm::dot(wdir, dir_world_into_def);
						if (cd >= PART_HORN_CONE_COS) dmg *= (1.0f + PART_HORN_DMG_MULT);
						else                          dmg *= (1.0f + PART_HORN_DMG_SIDE);
					}
					if (p.type == PartType::VENOM_SPIKE) venom_connected = true;
					total += dmg;
				}
				// Defender ARMOR: flat DR.
				total *= (1.0f - def.part_effect.armor_dr);
				if (total < 0.0f) total = 0.0f;
				return total;
			};

			bool venom_a_connected = false, venom_b_connected = false;
			float dmg_to_b = compute_part_damage(*A, *B,  dir_ab, venom_a_connected);
			float dmg_to_a = compute_part_damage(*B, *A, -dir_ab, venom_b_connected);

			auto apply_venom_on_bite = [](Monster* attacker, Monster* victim) {
				int n = attacker->part_effect.venom_stacks;
				for (int i = 0; i < n; ++i) {
					StatusEffect se;
					se.type = StatusEffect::VENOM;
					se.remaining = PART_VENOM_DURATION;
					se.magnitude = PART_VENOM_DPS;
					se.source = attacker->id;
					victim->status.push_back(se);
				}
			};
			if (dmg_to_b > 0.0f) {
				B->hp -= dmg_to_b;
				last_damager_[B->id] = A->id;
				Event e{EventKind::BITE, A->id, B->id, dmg_to_b, contact};
				events_.push_back(e);
				if (venom_a_connected) apply_venom_on_bite(A, B);
			}
			if (dmg_to_a > 0.0f) {
				A->hp -= dmg_to_a;
				last_damager_[A->id] = B->id;
				Event e{EventKind::BITE, B->id, A->id, dmg_to_a, contact};
				events_.push_back(e);
				if (venom_b_connected) apply_venom_on_bite(B, A);
			}

			// Soft separating impulse — push both apart along dir_ab.
			glm::vec2 push = dir_ab * SEPARATION_IMPULSE;
			A->velocity -= push;
			B->velocity += push;

			// ---- Hard overlap resolution ------------------------------
			// Push the pair apart so their polygons no longer overlap.
			// Penetration depth is the worst interpenetration of vertices
			// projected along dir_ab: for A's vertices inside B, how far
			// past B's near edge (along dir_ab) they sit, and vice versa.
			// This is cheap (reuses the world polys we already built) and
			// avoids the overestimate of using sum-of-max-radii.
			// SAT-style penetration along dir_ab: how far A's leading edge
			// projects past B's trailing edge along the push axis.
			float a_max = -1e30f, b_min = 1e30f;
			for (const auto& v : pa) {
				float d = glm::dot(v, dir_ab);
				if (d > a_max) a_max = d;
			}
			for (const auto& v : pb) {
				float d = glm::dot(v, dir_ab);
				if (d < b_min) b_min = d;
			}
			float penetration = a_max - b_min;
			if (penetration <= 0.0f) penetration = OVERLAP_PUSH_PAD;
			{
				float push_mag = std::min(penetration + OVERLAP_PUSH_PAD, OVERLAP_MAX_PUSH);
				float ma = std::max(A->mass, 1e-3f);
				float mb = std::max(B->mass, 1e-3f);
				float total_m = ma + mb;
				float a_share = mb / total_m; // A moves proportional to B's mass
				float b_share = ma / total_m;
				A->core_pos -= dir_ab * (push_mag * a_share);
				B->core_pos += dir_ab * (push_mag * b_share);
				// Clamp both back inside the arena.
				auto clamp_inside = [&](Monster* m) {
					float r = glm::length(m->core_pos);
					if (r > world_->map_radius && r > 1e-3f) {
						m->core_pos = (m->core_pos / r) * world_->map_radius;
					}
				};
				clamp_inside(A);
				clamp_inside(B);
				// Refresh A/B's world polygons since positions moved —
				// prevents the next pair-iteration from using stale data.
				world_polys[ids[i]] = transform_to_world(A->shape, A->core_pos, A->heading);
				world_polys[ids[j]] = transform_to_world(B->shape, B->core_pos, B->heading);
			}
		}
	}
}

void Sim::apply_poison_auras_(float dt) {
	// Emitters = monsters with POISON parts. Snapshot first to avoid
	// mid-loop mutation surprises.
	struct Emitter { uint32_t id; glm::vec2 pos; uint32_t owner; float dps; float r2; };
	std::vector<Emitter> emitters;
	for (auto& [id, m] : world_->monsters) {
		if (!m.alive) continue;
		if (m.part_effect.poison_dps <= 0.0f) continue;
		emitters.push_back({id, m.core_pos, m.owner_id, m.part_effect.poison_dps,
			m.part_effect.poison_radius * m.part_effect.poison_radius});
	}
	if (emitters.empty()) return;
	for (auto& [id, m] : world_->monsters) {
		if (!m.alive) continue;
		for (const auto& e : emitters) {
			if (e.id == id) continue;
			if (e.owner == m.owner_id) continue; // friendly
			glm::vec2 d = m.core_pos - e.pos;
			if (d.x * d.x + d.y * d.y > e.r2) continue;
			float amt = e.dps * dt * (1.0f - m.part_effect.armor_dr);
			if (amt <= 0.0f) continue;
			m.hp -= amt;
			last_damager_[id] = e.id;
			events_.push_back({EventKind::POISON_HIT, e.id, id, amt, m.core_pos});
		}
	}
}

void Sim::apply_status_and_regen_(float dt) {
	for (auto& [id, m] : world_->monsters) {
		if (!m.alive) continue;

		// Venom DoT — additive across stacks, each ticks independently.
		for (auto& se : m.status) {
			if (se.remaining <= 0.0f) continue;
			float amt = se.magnitude * dt * (1.0f - m.part_effect.armor_dr);
			if (amt < 0.0f) amt = 0.0f;
			if (amt > 0.0f) {
				m.hp -= amt;
				last_damager_[id] = se.source;
				events_.push_back({EventKind::VENOM_HIT, se.source, id, amt, m.core_pos});
			}
			se.remaining -= dt;
		}
		m.status.erase(std::remove_if(m.status.begin(), m.status.end(),
			[](const StatusEffect& s){ return s.remaining <= 0.0f; }), m.status.end());

		// Passive regen (does not over-heal past hp_max; no effect on dead).
		if (m.part_effect.regen_hps > 0.0f && m.hp > 0.0f && m.hp < m.hp_max) {
			m.hp = std::min(m.hp_max, m.hp + m.part_effect.regen_hps * dt);
		}
	}
}

void Sim::finalize_deaths_() {
	std::vector<uint32_t> to_remove;
	for (auto& [id, m] : world_->monsters) {
		if (m.alive && m.hp <= 0.0f) {
			m.alive = false;
			to_remove.push_back(id);
		}
	}
	for (uint32_t vid : to_remove) {
		Monster* victim = world_->get(vid);
		if (!victim) continue;
		float total = victim->biomass;
		uint32_t killer = 0;
		auto it = last_damager_.find(vid);
		if (it != last_damager_.end()) killer = it->second;

		if (killer != 0) {
			Monster* k = world_->get(killer);
			if (k && k->alive) {
				float to_killer = total * DEATH_BIOMASS_FRAC;
				k->biomass += to_killer;
				k->hp_max = std::max(1.0f, k->biomass * HP_PER_BIOMASS);
				k->hp = std::min(k->hp_max, k->hp + to_killer * 0.5f);
				Event e{EventKind::KILL, killer, vid, to_killer, victim->core_pos};
				events_.push_back(e);
				total -= to_killer;
			} else {
				killer = 0; // killer is gone — drop everything as food
			}
		}
		if (killer == 0) {
			Event e{EventKind::DEATH, 0, vid, total, victim->core_pos};
			events_.push_back(e);
		}
		// Remainder → food pickups at the corpse location.
		float remaining = std::max(0.0f, total);
		if (remaining > 0.0f) {
			// Split into ~3 pellets for visual interest.
			int pellets = 3;
			float each = remaining / float(pellets);
			for (int p = 0; p < pellets; ++p) {
				float angle = TWO_PI * float(p) / float(pellets);
				glm::vec2 off(std::cos(angle) * 6.0f, std::sin(angle) * 6.0f);
				// Corpse chunks are meat.
				world_->add_food(victim->core_pos + off, each, FoodType::MEAT);
			}
		}
		world_->remove_monster(vid);
	}
}

std::vector<Event> Sim::drain_events() {
	std::vector<Event> out;
	out.swap(events_);
	return out;
}

} // namespace civcraft::cellcraft::sim
