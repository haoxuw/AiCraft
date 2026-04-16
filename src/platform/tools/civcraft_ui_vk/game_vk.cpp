#include "game_vk.h"

#include <GLFW/glfw3.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <unistd.h>     // access() for screenshot trigger file

namespace civcraft::vk {

Tuning kTune;

namespace {

// ── Hash / noise helpers ─────────────────────────────────────────────────
// Small deterministic hashes so world generation and NPC spawns are
// reproducible across runs for a given seed.
float bhash(int x, int z) {
	unsigned h = (unsigned)(x * 73856093 ^ z * 19349663);
	return (float)(h & 0xFFFF) / 65536.0f;
}
float noise(float x, float z) {
	return std::sin(x * 0.10f) * std::cos(z * 0.13f) * 3.0f
	     + std::sin(x * 0.22f + 1.5f) * std::cos(z * 0.28f - 0.7f) * 2.0f
	     + std::sin(x * 0.05f + z * 0.06f) * 4.5f;
}

bool isPath(int x, int z) {
	return (std::abs(x) <= 1 && z >= -20 && z <= 20) ||
	       (std::abs(z) <= 1 && x >= -20 && x <= 20) ||
	       (std::abs(x - z) <= 1 && std::abs(x) < 12) ||
	       (std::abs(x + z) <= 1 && std::abs(x) < 12);
}
bool isVillage(int x, int z) {
	return std::abs(x) < 15 && std::abs(z) < 15;
}

int heightAt(int x, int z) {
	if (isVillage(x, z)) return 7;
	float v = noise((float)x, (float)z) + 7.0f;
	int h = std::max(1, (int)std::round(v));
	return h;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────
// World
// ─────────────────────────────────────────────────────────────────────────

void World::generate(int /*seed*/) {
	m_R = kTune.worldRadius;
	int W = m_R * 2;
	m_heightMap.assign(W * W, 0);
	m_instances.clear();
	m_instances.reserve(W * W * 12 * 6);

	auto push = [&](float x, float y, float z, float r, float g, float b) {
		m_instances.push_back(x); m_instances.push_back(y); m_instances.push_back(z);
		m_instances.push_back(r); m_instances.push_back(g); m_instances.push_back(b);
	};

	for (int z = -m_R; z < m_R; z++) {
		for (int x = -m_R; x < m_R; x++) {
			int top = heightAt(x, z);
			m_heightMap[(z + m_R) * W + (x + m_R)] = top;

			float jitter = ((x*73 + z*131) & 0xF) / 500.0f;

			for (int y = 0; y <= top; y++) {
				float r, g, b;
				if (y == top && isPath(x, z) && isVillage(x, z)) {
					float pj = bhash(x, z) * 0.08f;
					r = 0.48f + pj; g = 0.40f + pj; b = 0.32f + pj;  // cobble
				} else if (y == top && isVillage(x, z)) {
					r = 0.22f; g = 0.52f; b = 0.18f;                  // village grass
				} else if (y == top && top >= 13) {
					r = 0.50f; g = 0.48f; b = 0.44f;                  // mountain stone
				} else if (y == top && top >= 5) {
					float gv = bhash(x, z);
					if (gv > 0.7f) { r=0.28f; g=0.55f; b=0.20f; }
					else if (gv > 0.3f) { r=0.22f; g=0.48f; b=0.16f; }
					else { r=0.18f; g=0.42f; b=0.14f; }
				} else if (y == top) {
					r = 0.52f; g = 0.42f; b = 0.28f;
				} else if (y >= top - 2) {
					r = 0.40f; g = 0.26f; b = 0.12f;
				} else {
					r = 0.38f; g = 0.36f; b = 0.34f;
				}
				r += jitter; g += jitter; b += jitter;
				push((float)x, (float)y, (float)z, r, g, b);
			}

			// Walls perimeter — stone
			if ((std::abs(x) >= 13 && std::abs(x) <= 14 && std::abs(z) <= 15) ||
			    (std::abs(z) >= 13 && std::abs(z) <= 14 && std::abs(x) <= 15)) {
				if (!(std::abs(x) <= 2 || std::abs(z) <= 2)) {
					for (int wy = top+1; wy <= top+3; wy++) {
						float wj = bhash(x*3+wy, z*7) * 0.06f;
						push((float)x, (float)wy, (float)z, 0.52f+wj, 0.48f+wj, 0.42f+wj);
						if (wy > m_heightMap[(z + m_R) * W + (x + m_R)])
							m_heightMap[(z + m_R) * W + (x + m_R)] = wy;
					}
				}
			}

			// Houses
			auto houseAt = [&](int cx, int cz, int hw, int hd, float cr, float cg, float cb) {
				if (x >= cx-hw && x <= cx+hw && z >= cz-hd && z <= cz+hd) {
					bool isWall = x == cx-hw || x == cx+hw || z == cz-hd || z == cz+hd;
					bool isDoor = (x == cx) && (z == cz-hd) && isVillage(x,z);
					if (isWall && !isDoor) {
						for (int hy = top+1; hy <= top+4; hy++) {
							float hj = bhash(x*5+hy, z*11) * 0.04f;
							push((float)x, (float)hy, (float)z, cr+hj, cg+hj, cb+hj);
							if (hy > m_heightMap[(z + m_R) * W + (x + m_R)])
								m_heightMap[(z + m_R) * W + (x + m_R)] = hy;
						}
					}
					if (!isDoor) {
						push((float)x, (float)(top+5), (float)z, 0.55f, 0.25f, 0.10f);
					}
				}
			};
			houseAt(-8, -8, 2, 2, 0.60f, 0.50f, 0.35f);
			houseAt( 7,  5, 2, 3, 0.55f, 0.52f, 0.48f);
			houseAt(-5,  8, 3, 2, 0.58f, 0.45f, 0.30f);
			houseAt( 8, -7, 2, 2, 0.50f, 0.48f, 0.44f);

			// Trees outside village
			bool outsideVillage = !isVillage(x, z);
			float treeDensity = outsideVillage ? 0.88f : 0.99f;
			if (top >= 5 && top < 13 && bhash(x, z) > treeDensity && !isPath(x,z)) {
				int trunkH = 3 + (int)(bhash(x+1, z+1) * 2.5f);
				for (int ty = top+1; ty <= top+trunkH; ty++)
					push((float)x, (float)ty, (float)z, 0.32f, 0.18f, 0.08f);
				int canopyR = trunkH > 4 ? 2 : 1;
				float lr = 0.14f, lg = 0.42f, lb = 0.10f;
				for (int dy = -canopyR; dy <= canopyR; dy++) {
					for (int dx = -canopyR; dx <= canopyR; dx++) {
						if (std::abs(dx)+std::abs(dy) <= canopyR+1) {
							float cj = bhash(x+dx*7, z+dy*13) * 0.06f;
							push((float)(x+dx), (float)(top+trunkH+1), (float)(z+dy),
							     lr+cj, lg+cj, lb+cj);
						}
					}
				}
				push((float)x, (float)(top+trunkH+2), (float)z,
				     lr+0.02f, lg+0.04f, lb+0.01f);
			}

			// Torch posts along paths
			if (isPath(x, z) && isVillage(x, z) &&
			    (x % 6 == 0) && (z % 6 == 0) && (x != 0 || z != 0)) {
				push((float)x, (float)(top+1), (float)z, 0.35f, 0.22f, 0.10f);
				push((float)x, (float)(top+2), (float)z, 0.35f, 0.22f, 0.10f);
				push((float)x, (float)(top+3), (float)z, 0.95f, 0.70f, 0.20f);
			}
		}
	}
	m_count = (uint32_t)(m_instances.size() / 6);
}

float World::terrainTop(float x, float z) const {
	int ix = (int)std::floor(x);
	int iz = (int)std::floor(z);
	int W = m_R * 2;
	int mx = ix + m_R;
	int mz = iz + m_R;
	if (mx < 0 || mx >= W || mz < 0 || mz >= W) return 7.0f;
	// height-map stores the TOP block index; "top of block" surface is h+1.
	return (float)m_heightMap[mz * W + mx] + 1.0f;
}

// ─────────────────────────────────────────────────────────────────────────
// NPC helpers
// ─────────────────────────────────────────────────────────────────────────

std::string Npc::goalText() const {
	switch (state) {
	case State::Wander: return "Wandering";
	case State::Chase:  return "Chasing player!";
	case State::Flee:   return "Fleeing!";
	case State::Dying:  return "...";
	}
	return "";
}

glm::vec4 Npc::tint() const {
	switch (state) {
	case State::Wander: return {1.00f, 0.88f, 0.30f, 1.0f}; // warm gold
	case State::Chase:  return {1.00f, 0.55f, 0.20f, 1.0f}; // hot orange
	case State::Flee:   return {1.00f, 0.30f, 0.25f, 1.0f}; // alarm red
	case State::Dying:  return {0.55f, 0.55f, 0.55f, 0.9f}; // gray
	}
	return {1,1,1,1};
}

// ─────────────────────────────────────────────────────────────────────────
// Player
// ─────────────────────────────────────────────────────────────────────────

glm::vec3 Player::forward() const {
	return glm::vec3(std::cos(yaw), 0, std::sin(yaw));
}

// ─────────────────────────────────────────────────────────────────────────
// Game — init / shutdown / state transitions
// ─────────────────────────────────────────────────────────────────────────

bool Game::init(rhi::IRhi* rhi, GLFWwindow* window) {
	m_rhi = rhi;
	m_window = window;
	m_world.generate();
	enterMenu();
	return true;
}

void Game::shutdown() {}

void Game::enterMenu() {
	m_state = GameState::Menu;
	// Release mouse so the menu can be clicked with ImGui.
	if (m_window) {
		glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
	}
	m_mouseCaptured = false;
}

void Game::enterPlaying() {
	m_state = GameState::Playing;
	// Respawn fresh player at origin on village grass.
	m_player = Player{};
	m_player.pos = glm::vec3(0.5f, m_world.terrainTop(0.5f, 0.5f), 0.5f);
	m_player.hp = (float)kTune.playerMaxHP;
	m_player.yaw = 0.0f;       // facing +X
	m_camYaw = -90.0f * 3.14159f / 180.0f;
	m_camPitch = -0.25f;
	m_coins = 0;
	m_slashes.clear();
	m_floaters.clear();
	spawnNpcs();
	// Capture mouse for look-around.
	if (m_window) {
		glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
		m_mouseCaptured = true;
		m_firstMouse = true;
	}
}

void Game::enterDead(const char* cause) {
	m_state = GameState::Dead;
	m_lastDeathReason = cause ? cause : "You died.";
	if (m_window) {
		glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		m_mouseCaptured = false;
	}
}

void Game::respawn() { enterPlaying(); }

void Game::spawnNpcs() {
	m_npcs.clear();
	m_npcs.reserve(kTune.npcCount);
	// Color palettes so the crowd reads as distinct characters.
	struct Palette { glm::vec3 skin, shirt, pants; };
	Palette pals[] = {
		{ {0.95f,0.80f,0.65f}, {0.55f,0.25f,0.25f}, {0.30f,0.20f,0.12f} },
		{ {0.90f,0.75f,0.60f}, {0.20f,0.45f,0.55f}, {0.15f,0.12f,0.25f} },
		{ {0.85f,0.68f,0.50f}, {0.65f,0.55f,0.20f}, {0.30f,0.25f,0.15f} },
		{ {0.80f,0.65f,0.55f}, {0.45f,0.50f,0.30f}, {0.20f,0.20f,0.15f} },
		{ {0.78f,0.60f,0.45f}, {0.70f,0.30f,0.55f}, {0.22f,0.15f,0.20f} },
		{ {0.70f,0.56f,0.42f}, {0.35f,0.35f,0.35f}, {0.15f,0.15f,0.15f} },
	};
	for (int i = 0; i < kTune.npcCount; i++) {
		Npc n;
		n.id = i + 1;
		// Distribute around the village perimeter, avoid the exact center so
		// the player can spawn into open space.
		float ang = (float)i / (float)kTune.npcCount * 6.28318f;
		float rad = 5.0f + bhash(i, i*7) * 5.0f;
		n.pos = glm::vec3(std::cos(ang) * rad, 0, std::sin(ang) * rad);
		n.pos.y = m_world.terrainTop(n.pos.x, n.pos.z);
		n.roamTarget = n.pos;
		n.maxHp = kTune.npcMaxHP;
		n.hp = n.maxHp;
		n.yaw = ang;
		n.phase = (float)i * 0.4f;
		Palette p = pals[i % (int)(sizeof(pals)/sizeof(pals[0]))];
		n.skin = p.skin; n.shirt = p.shirt; n.pants = p.pants;
		m_npcs.push_back(n);
	}
}

// ─────────────────────────────────────────────────────────────────────────
// Camera helpers
// ─────────────────────────────────────────────────────────────────────────

glm::vec3 Game::cameraEye() const {
	// Third-person: orbit behind the player using m_camYaw + m_camPitch.
	glm::vec3 head = m_player.pos + glm::vec3(0, kTune.camHeight, 0);
	glm::vec3 dir(
		std::cos(m_camPitch) * std::cos(m_camYaw),
		std::sin(m_camPitch),
		std::cos(m_camPitch) * std::sin(m_camYaw));
	return head - dir * kTune.camDistance;
}

glm::mat4 Game::viewMatrix() const {
	glm::vec3 eye = cameraEye();
	glm::vec3 head = m_player.pos + glm::vec3(0, kTune.camHeight, 0);
	return glm::lookAt(eye, head, glm::vec3(0,1,0));
}

glm::mat4 Game::viewProj() const {
	glm::mat4 proj = glm::perspective(glm::radians(60.0f), m_aspect, 0.1f, 300.0f);
	proj[1][1] *= -1.0f;
	return proj * viewMatrix();
}

bool Game::projectWorld(const glm::vec3& world, glm::vec3& out) const {
	glm::vec4 clip = viewProj() * glm::vec4(world, 1.0f);
	if (clip.w <= 0.01f) return false;
	float ndcX = clip.x / clip.w;
	float ndcY = -clip.y / clip.w;  // Y already flipped in proj — un-flip
	// ^ but our UI is in OpenGL NDC (+y up) while VP flipped — rhi_ui.cpp
	// takes +y up directly. Internal VP has proj[1][1] *= -1 to handle VK
	// image coords, so re-flip here for UI consumption.
	if (ndcX < -1.2f || ndcX > 1.2f || ndcY < -1.2f || ndcY > 1.2f) return false;
	out = glm::vec3(ndcX, ndcY, clip.z / clip.w);
	return true;
}

// ─────────────────────────────────────────────────────────────────────────
// Input
// ─────────────────────────────────────────────────────────────────────────

void Game::processInput(float dt) {
	(void)dt;
	if (!m_window) return;

	// ESC toggles menu-capture (pause ↔ play).
	bool esc = glfwGetKey(m_window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
	if (esc && !m_escLast) {
		enterMenu();
	}
	m_escLast = esc;

	if (m_state != GameState::Playing) return;

	// Mouse look. We use raw GLFW cursor; FPS-style re-centering happens
	// via GLFW_CURSOR_DISABLED (set on enterPlaying).
	double mx, my;
	glfwGetCursorPos(m_window, &mx, &my);
	if (m_firstMouse) { m_lastMouseX = mx; m_lastMouseY = my; m_firstMouse = false; }
	double dx = mx - m_lastMouseX;
	double dy = my - m_lastMouseY;
	m_lastMouseX = mx; m_lastMouseY = my;

	m_camYaw   += (float)dx * kTune.camSensYaw;
	m_camPitch -= (float)dy * kTune.camSensPitch;
	const float lim = 1.40f;
	m_camPitch = glm::clamp(m_camPitch, -lim, lim);

	// Player body yaw follows the camera yaw so the character always faces
	// where the camera looks (TPS-style controller).
	m_player.yaw = m_camYaw;
}

// ─────────────────────────────────────────────────────────────────────────
// Player tick — WASD + jump + gravity + ground-clamp + regen
// ─────────────────────────────────────────────────────────────────────────

void Game::tickPlayer(float dt) {
	if (!m_window) return;

	// Intent vector in camera space.
	glm::vec3 fwd = m_player.forward();
	glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0,1,0)));
	glm::vec3 mv(0);
	if (glfwGetKey(m_window, GLFW_KEY_W) == GLFW_PRESS) mv += fwd;
	if (glfwGetKey(m_window, GLFW_KEY_S) == GLFW_PRESS) mv -= fwd;
	if (glfwGetKey(m_window, GLFW_KEY_A) == GLFW_PRESS) mv -= right;
	if (glfwGetKey(m_window, GLFW_KEY_D) == GLFW_PRESS) mv += right;

	float moveLen = glm::length(mv);
	glm::vec3 moveDir = moveLen > 0.001f ? mv / moveLen : glm::vec3(0);
	bool boost = glfwGetKey(m_window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;

	// Jump
	bool space = glfwGetKey(m_window, GLFW_KEY_SPACE) == GLFW_PRESS;
	if (space && !m_spaceLast && m_player.onGround) {
		m_player.vel.y = kTune.playerJumpV;
		m_player.onGround = false;
	}
	m_spaceLast = space;

	// Horizontal velocity: direct (no acceleration model — arcade feel).
	float speed = kTune.playerSpeed * (boost ? 1.6f : 1.0f);
	m_player.vel.x = moveDir.x * speed;
	m_player.vel.z = moveDir.z * speed;

	// Gravity
	m_player.vel.y += kTune.gravity * dt;

	// Step position
	m_player.pos += m_player.vel * dt;

	// Ground clamp
	float ground = m_world.terrainTop(m_player.pos.x, m_player.pos.z);
	if (m_player.pos.y <= ground) {
		m_player.pos.y = ground;
		m_player.vel.y = 0;
		m_player.onGround = true;
	} else {
		m_player.onGround = false;
	}

	// Out-of-bounds clamp
	float bound = (float)kTune.worldRadius - 2.0f;
	m_player.pos.x = glm::clamp(m_player.pos.x, -bound, bound);
	m_player.pos.z = glm::clamp(m_player.pos.z, -bound, bound);

	// Walk distance fuels arm/leg swing
	if (m_player.onGround && moveLen > 0.001f)
		m_player.walkDist += speed * dt;

	// HP regen
	m_player.regenIdle += dt;
	if (m_player.regenIdle > kTune.hpRegenDelay) {
		m_player.hp = std::min((float)kTune.playerMaxHP,
			m_player.hp + kTune.playerHPRegen * dt);
	}

	if (m_player.attackCD > 0) m_player.attackCD -= dt;

	// Left-click attack
	int lmb = glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_LEFT);
	bool lmbNow = (lmb == GLFW_PRESS);
	if (lmbNow && !m_lmbLast && m_player.attackCD <= 0 && m_mouseCaptured) {
		m_player.attackCD = kTune.attackCD;
		// Emit a slash ribbon effect regardless of hit.
		glm::vec3 from = m_player.pos + glm::vec3(0, kTune.playerHeight * 0.6f, 0);
		Slash sw;
		sw.center = from;
		sw.dir    = m_player.forward();
		m_slashes.push_back(sw);
		// Hit-check nearest NPC in front-cone.
		if (Npc* target = nearestNpcInCone(from, m_player.forward(),
				kTune.attackRange, kTune.attackCone)) {
			damageNpc(*target, kTune.attackDmg, m_player.forward());
		}
	}
	m_lmbLast = lmbNow;
}

// ─────────────────────────────────────────────────────────────────────────
// NPC tick — Wander / Chase / Flee state machine
// ─────────────────────────────────────────────────────────────────────────

void Game::tickNpcs(float dt) {
	std::mt19937 rng{ (uint32_t)(m_wallTime * 1000.0f) };
	std::uniform_real_distribution<float> u(-1, 1);

	for (auto& n : m_npcs) {
		n.stateT += dt;
		n.hitFlash = std::max(0.0f, n.hitFlash - dt * 4.0f);
		if (n.hitstop > 0) { n.hitstop -= dt; continue; }
		if (n.state == Npc::State::Dying) {
			n.deathT += dt * 1.2f;
			continue;
		}

		float distToPlayer = glm::length(m_player.pos - n.pos);
		float hpFrac = (float)n.hp / (float)n.maxHp;

		// State transitions
		if (hpFrac < kTune.npcFleeHpFrac && n.state != Npc::State::Flee) {
			n.state = Npc::State::Flee;
			n.stateT = 0;
		} else if (n.state == Npc::State::Wander && distToPlayer < kTune.npcAggroRange) {
			n.state = Npc::State::Chase;
			n.stateT = 0;
		} else if (n.state == Npc::State::Chase && distToPlayer > kTune.npcAggroRange * 1.8f) {
			n.state = Npc::State::Wander;
			n.stateT = 0;
		}

		glm::vec3 toPlayer = m_player.pos - n.pos;
		glm::vec3 desired(0);
		switch (n.state) {
		case Npc::State::Wander: {
			// Pick a new roam target every 4-6s
			if (n.stateT > 4.0f + bhash(n.id, (int)(m_wallTime)) * 2.0f) {
				float ang = u(rng) * 3.14159f;
				float rad = 3.0f + u(rng) * 4.0f;
				glm::vec3 cand = n.pos + glm::vec3(std::cos(ang)*rad, 0, std::sin(ang)*rad);
				float bound = (float)kTune.worldRadius - 3.0f;
				cand.x = glm::clamp(cand.x, -bound, bound);
				cand.z = glm::clamp(cand.z, -bound, bound);
				n.roamTarget = cand;
				n.stateT = 0;
			}
			glm::vec3 toTarget = n.roamTarget - n.pos;
			toTarget.y = 0;
			if (glm::length(toTarget) > 0.4f)
				desired = glm::normalize(toTarget) * (kTune.npcSpeed * 0.5f);
			break;
		}
		case Npc::State::Chase: {
			glm::vec3 tp = toPlayer; tp.y = 0;
			if (glm::length(tp) > 0.5f)
				desired = glm::normalize(tp) * kTune.npcSpeed;
			break;
		}
		case Npc::State::Flee: {
			glm::vec3 away = -toPlayer; away.y = 0;
			if (glm::length(away) > 0.01f)
				desired = glm::normalize(away) * (kTune.npcSpeed * 1.2f);
			break;
		}
		default: break;
		}

		n.vel.x = desired.x;
		n.vel.z = desired.z;
		n.vel.y += kTune.gravity * dt;

		n.pos += n.vel * dt;

		float ground = m_world.terrainTop(n.pos.x, n.pos.z);
		if (n.pos.y <= ground) { n.pos.y = ground; n.vel.y = 0; }

		// Orient body toward movement (or player when chasing/fleeing)
		glm::vec3 faceDir = (n.state == Npc::State::Chase) ? toPlayer : desired;
		faceDir.y = 0;
		if (glm::length(faceDir) > 0.05f)
			n.yaw = std::atan2(faceDir.z, faceDir.x);

		// Animation phase
		if (glm::length(glm::vec2(desired.x, desired.z)) > 0.05f)
			n.phase += dt * 4.0f;
	}

	// Purge dying NPCs
	m_npcs.erase(
		std::remove_if(m_npcs.begin(), m_npcs.end(),
			[](const Npc& n){ return n.state == Npc::State::Dying && n.deathT >= 1.0f; }),
		m_npcs.end());
}

// ─────────────────────────────────────────────────────────────────────────
// Combat — NPC melee touch damage + slash decay + cone-picking + kills
// ─────────────────────────────────────────────────────────────────────────

Npc* Game::nearestNpcInCone(const glm::vec3& from, const glm::vec3& fwd,
                             float range, float coneRad) {
	Npc* best = nullptr;
	float bestDist = range + 0.01f;
	for (auto& n : m_npcs) {
		if (n.state == Npc::State::Dying) continue;
		glm::vec3 to = n.pos - from;
		to.y = 0;
		float d = glm::length(to);
		if (d > range || d < 0.01f) continue;
		glm::vec3 dir = to / d;
		float cosA = glm::dot(dir, glm::vec3(fwd.x, 0, fwd.z));
		if (cosA < std::cos(coneRad)) continue;
		if (d < bestDist) { bestDist = d; best = &n; }
	}
	return best;
}

void Game::damageNpc(Npc& n, int dmg, const glm::vec3& /*fromDir*/) {
	n.hp -= dmg;
	n.hitFlash = 1.0f;
	n.hitstop  = 0.08f;
	// Floating damage number above the head.
	FloatText ft;
	ft.worldPos = n.pos + glm::vec3(0, 2.4f, 0);
	ft.color = glm::vec3(1.0f, 0.85f, 0.30f);  // gold crit-ish
	char buf[16]; std::snprintf(buf, sizeof(buf), "-%d", dmg);
	ft.text = buf;
	ft.lifetime = 1.0f;
	m_floaters.push_back(ft);

	if (n.hp <= 0) {
		n.hp = 0;
		n.state = Npc::State::Dying;
		n.stateT = 0;
		n.deathT = 0;
		m_coins += 1;
		// Death popup
		FloatText kill;
		kill.worldPos = n.pos + glm::vec3(0, 3.0f, 0);
		kill.color = glm::vec3(1.0f, 0.70f, 0.20f);
		kill.text = "+1 COIN";
		kill.lifetime = 1.6f;
		m_floaters.push_back(kill);
	}
}

void Game::tickCombat(float dt) {
	// Slash ribbons age out.
	for (auto& s : m_slashes) s.t += dt;
	m_slashes.erase(
		std::remove_if(m_slashes.begin(), m_slashes.end(),
			[](const Slash& s){ return s.t >= s.duration; }),
		m_slashes.end());

	// NPC melee-touch damage: Chase-state NPCs that are in range drip damage.
	for (auto& n : m_npcs) {
		if (n.state != Npc::State::Chase) continue;
		glm::vec3 d = m_player.pos - n.pos; d.y = 0;
		if (glm::length(d) < kTune.npcTouchRange) {
			float dmg = kTune.npcTouchDmg * dt;
			if (dmg > 0) {
				m_player.hp -= dmg;
				m_player.regenIdle = 0;
				if (m_player.hp <= 0) {
					m_player.hp = 0;
					enterDead("Slain by a villager.");
					return;
				}
			}
		}
	}
}

void Game::tickFloaters(float dt) {
	for (auto& f : m_floaters) f.t += dt;
	m_floaters.erase(
		std::remove_if(m_floaters.begin(), m_floaters.end(),
			[](const FloatText& f){ return f.t >= f.lifetime; }),
		m_floaters.end());
}

// ─────────────────────────────────────────────────────────────────────────
// Rendering — world, entities, effects, HUD
// ─────────────────────────────────────────────────────────────────────────

namespace {
// Pack box-model humanoid into {pos[3], size[3], color[3]} stream — 9 floats
// per box — matching IRhi::drawBoxModel's layout. Animation reuses the
// player/npc phase so arms/legs swing.
void appendHumanoid(std::vector<float>& out,
                    glm::vec3 pos, float yaw, float phase,
                    glm::vec3 skin, glm::vec3 shirt, glm::vec3 pants,
                    float hitFlash, float dyingT) {
	float swing = std::sin(phase) * 0.25f;
	float bob   = std::fabs(std::sin(phase)) * 0.04f;
	// Dying: character flops by rotating body forward about its feet. Sim-
	// ulated here by shrinking the torso/head vertical extents and tilting
	// arm positions — quick-and-cheap animation without a skeletal system.
	float alive = 1.0f - std::clamp(dyingT, 0.0f, 1.0f);
	bob *= alive;
	swing *= alive;

	auto push = [&](glm::vec3 center, glm::vec3 size, glm::vec3 col) {
		glm::vec3 c = col * (1.0f + hitFlash * 1.5f);  // brighten on hit
		if (hitFlash > 0.1f) {
			// Punch color toward white for a flash
			c = glm::mix(col, glm::vec3(1.2f, 1.2f, 1.0f), hitFlash);
		}
		// Rotate (pos - 0) around Y by yaw, offset by world pos.
		float cs = std::cos(yaw), sn = std::sin(yaw);
		// yaw=0 → face +X. Our anims place arms ±X so rotation is correct.
		glm::vec3 local = center - pos;
		glm::vec3 rotated(local.x * cs - local.z * sn,
		                  local.y,
		                  local.x * sn + local.z * cs);
		glm::vec3 wc = rotated + pos;
		glm::vec3 corner = wc - size * 0.5f;
		out.push_back(corner.x); out.push_back(corner.y); out.push_back(corner.z);
		out.push_back(size.x);   out.push_back(size.y);   out.push_back(size.z);
		out.push_back(c.x);      out.push_back(c.y);      out.push_back(c.z);
	};

	// Body is centered at feet+1.05. Shrink when dying so character flops.
	float bodyY = 1.05f * alive + 0.50f * (1 - alive);
	float headY = 1.75f * alive + 0.80f * (1 - alive);
	push(pos + glm::vec3(0, bodyY + bob, 0), glm::vec3(0.55f, 0.70f*alive+0.2f, 0.35f), shirt);
	push(pos + glm::vec3(0, headY + bob, 0), glm::vec3(0.45f, 0.45f, 0.45f), skin);
	// Arms: swing along the facing axis (local +X). Sides (local ±Z).
	push(pos + glm::vec3( swing, 1.05f + bob, -0.38f), glm::vec3(0.20f, 0.65f, 0.20f), shirt);
	push(pos + glm::vec3(-swing, 1.05f + bob,  0.38f), glm::vec3(0.20f, 0.65f, 0.20f), shirt);
	// Legs (opposite swing of arms for walking anim)
	push(pos + glm::vec3( swing, 0.40f, -0.15f), glm::vec3(0.22f, 0.70f, 0.22f), pants);
	push(pos + glm::vec3(-swing, 0.40f,  0.15f), glm::vec3(0.22f, 0.70f, 0.22f), pants);
}
} // namespace

void Game::renderWorld(float wallTime) {
	// Sun trajectory — same warm golden hour as before.
	float sunAngle = 0.55f + wallTime * 0.008f;
	glm::vec3 sunDir = glm::normalize(glm::vec3(
		std::cos(sunAngle) * 0.6f,
		0.30f + std::sin(sunAngle) * 0.35f,
		0.4f));
	float sunStr = glm::clamp(sunDir.y * 1.4f, 0.15f, 0.75f);

	// Shadow pass (terrain + entities share the same depth map).
	glm::vec3 shadowCenter(m_player.pos.x, 6.0f, m_player.pos.z);
	glm::vec3 lightPos = shadowCenter + sunDir * 80.0f;
	glm::mat4 lightView = glm::lookAt(lightPos, shadowCenter,
		std::abs(sunDir.y) > 0.98f ? glm::vec3(0,0,1) : glm::vec3(0,1,0));
	glm::mat4 lightProj = glm::ortho(-60.0f, 60.0f, -60.0f, 60.0f, 1.0f, 200.0f);
	glm::mat4 shadowVP = lightProj * lightView;

	// Build entity box stream once per frame; used by shadow and main pass.
	std::vector<float> charBoxes;
	charBoxes.reserve((m_npcs.size() + 1) * 6 * 9);
	// Player
	{
		float ph = m_player.walkDist * 2.5f;
		appendHumanoid(charBoxes, m_player.pos, m_player.yaw, ph,
			glm::vec3(0.95f,0.82f,0.68f),
			glm::vec3(0.25f,0.45f,0.80f),    // blue hero shirt
			glm::vec3(0.30f,0.22f,0.14f),
			0.0f, 0.0f);
	}
	for (const auto& n : m_npcs) {
		appendHumanoid(charBoxes, n.pos, n.yaw, n.phase,
			n.skin, n.shirt, n.pants,
			n.hitFlash,
			n.state == Npc::State::Dying ? n.deathT : 0.0f);
	}
	uint32_t charBoxCount = (uint32_t)(charBoxes.size() / 9);

	m_rhi->renderShadows(&shadowVP[0][0], m_world.instances(), m_world.instanceCount());
	m_rhi->renderBoxShadows(&shadowVP[0][0], charBoxes.data(), charBoxCount);

	// Sky
	glm::mat4 vp = viewProj();
	glm::mat4 invVP = glm::inverse(vp);
	m_rhi->drawSky(&invVP[0][0], &sunDir.x, sunStr);

	// Terrain
	rhi::IRhi::SceneParams scene{};
	std::memcpy(scene.viewProj, &vp[0][0], sizeof(float)*16);
	glm::vec3 eye = cameraEye();
	scene.camPos[0] = eye.x; scene.camPos[1] = eye.y; scene.camPos[2] = eye.z;
	scene.time = wallTime;
	scene.sunDir[0] = sunDir.x; scene.sunDir[1] = sunDir.y; scene.sunDir[2] = sunDir.z;
	scene.sunStr = sunStr;
	m_rhi->drawVoxels(scene, m_world.instances(), m_world.instanceCount());

	// Entities
	m_rhi->drawBoxModel(scene, charBoxes.data(), charBoxCount);
}

void Game::renderEntities(float /*wallTime*/) {
	// Box-model rendering already happens inside renderWorld so terrain +
	// entities share scene params with no extra state bookkeeping.
}

void Game::renderEffects(float wallTime) {
	rhi::IRhi::SceneParams scene{};
	glm::mat4 vp = viewProj();
	std::memcpy(scene.viewProj, &vp[0][0], sizeof(float)*16);
	glm::vec3 eye = cameraEye();
	scene.camPos[0] = eye.x; scene.camPos[1] = eye.y; scene.camPos[2] = eye.z;
	scene.time = wallTime;

	// ── Torch flames, embers, fireflies (reused from demo) ─────────────
	std::vector<float> particles;
	particles.reserve(512 * 8);
	auto pushP = [&](glm::vec3 p, float size, glm::vec3 rgb, float a) {
		particles.push_back(p.x); particles.push_back(p.y); particles.push_back(p.z);
		particles.push_back(size);
		particles.push_back(rgb.x); particles.push_back(rgb.y); particles.push_back(rgb.z);
		particles.push_back(a);
	};
	auto fract = [](float x) { return x - std::floor(x); };

	int torchIdx = 0;
	for (int tz = -18; tz <= 18; tz += 6) {
		for (int tx = -18; tx <= 18; tx += 6) {
			if (tx == 0 && tz == 0) continue;
			bool onPath = (std::abs(tx) <= 1 || std::abs(tz) <= 1
			            || std::abs(tx - tz) <= 1 || std::abs(tx + tz) <= 1);
			if (!onPath) continue;
			glm::vec3 torch((float)tx, 10.4f, (float)tz);
			for (int k = 0; k < 4; k++) {
				float ph = wallTime * 4.0f + (float)(torchIdx + k * 7) * 0.5f;
				float pulse = 0.75f + 0.35f * std::sin(ph) + 0.15f * std::sin(ph * 2.3f);
				glm::vec3 p = torch + glm::vec3(
					0.08f * std::sin(ph * 1.3f + k),
					0.15f * std::sin(ph * 0.7f) + 0.14f * k,
					0.08f * std::cos(ph * 1.7f + k));
				pushP(p, 0.55f * pulse,
					glm::vec3(4.5f, 2.0f + k*0.2f, 0.35f), 1.0f);
			}
			{
				float ph = wallTime * 5.0f + (float)torchIdx * 0.3f;
				float pulse = 0.8f + 0.2f * std::sin(ph * 1.7f);
				pushP(torch + glm::vec3(0, 0.05f * std::sin(ph), 0),
					0.30f * pulse, glm::vec3(5.0f, 4.2f, 1.5f), 1.0f);
			}
			for (int k = 0; k < 6; k++) {
				float seed = (float)(torchIdx * 6 + k);
				float life = fract(wallTime * 0.5f + seed * 0.173f);
				float driftX = std::sin(seed * 17.1f + wallTime * 0.8f) * 0.45f;
				float driftZ = std::cos(seed * 13.7f + wallTime * 0.9f) * 0.45f;
				glm::vec3 p = torch + glm::vec3(
					driftX * life, 0.25f + life * 3.5f, driftZ * life);
				float fade = life < 0.3f ? (life / 0.3f) : (1.0f - (life - 0.3f) / 0.7f);
				float size = 0.18f + life * 0.15f;
				float cool = life;
				pushP(p, size,
					glm::vec3(3.5f - cool*0.8f, 1.4f - cool*1.0f, 0.15f), fade);
			}
			torchIdx++;
		}
	}

	// Firefly sparks — warm gold + cool cyan-white
	for (int k = 0; k < 80; k++) {
		float seed = (float)k;
		float bx = std::sin(seed * 17.3f) * 12.0f;
		float bz = std::cos(seed * 23.1f) * 12.0f;
		float ph = wallTime * (0.25f + 0.12f * fract(seed * 0.137f)) + seed;
		float rad = 0.5f + 0.6f * fract(seed * 0.091f);
		glm::vec3 p(
			bx + std::sin(ph) * rad,
			9.0f + 2.2f * std::sin(seed * 11.5f + wallTime * 0.4f)
			     + 0.9f * std::cos(ph * 0.7f),
			bz + std::cos(ph * 1.1f) * rad);
		float twinkle = 0.3f + 0.7f * std::fabs(std::sin(wallTime * 2.2f + seed * 3.3f));
		glm::vec3 col = ((int)k & 1) == 0
			? glm::vec3(3.0f, 3.2f, 1.8f)
			: glm::vec3(1.4f, 2.6f, 3.5f);
		pushP(p, 0.32f, col, 0.80f * twinkle);
	}

	uint32_t particleCount = (uint32_t)(particles.size() / 8);
	if (particleCount > 0) m_rhi->drawParticles(scene, particles.data(), particleCount);

	// ── Sword slash ribbons (one per active swing) ──────────────────────
	for (const auto& s : m_slashes) {
		float swingT = s.t / s.duration;    // 0..1
		// Arc from upper-right → forward → lower-left, relative to dir.
		constexpr int N = 16;
		std::vector<float> rbuf;
		rbuf.reserve(N * 8);
		glm::vec3 dir = s.dir;
		glm::vec3 right = glm::normalize(glm::cross(dir, glm::vec3(0,1,0)));
		for (int i = 0; i < N; i++) {
			float age = (float)i / (float)(N - 1);    // 0 = head, 1 = oldest
			float u = swingT - age * 0.5f;
			if (u < -0.05f) continue;
			u = glm::clamp(u, 0.0f, 1.0f);
			float ang = (u - 0.5f) * 2.4f;
			float r = 1.6f;
			glm::vec3 p = s.center
				+ dir * (std::cos(ang) * 0.9f - 0.3f)
				+ right * std::sin(ang) * r
				+ glm::vec3(0, std::cos(ang) * 0.6f, 0);
			float width = glm::mix(0.9f, 0.10f, age);
			float env = swingT < 0.15f ? swingT / 0.15f
				: swingT > 0.8f ? (1.0f - swingT) / 0.2f : 1.0f;
			env = glm::clamp(env, 0.0f, 1.0f);
			float headGlow = std::pow(1.0f - age, 1.6f);
			glm::vec3 col = glm::mix(s.late, s.early, headGlow);
			if (age < 0.12f) col = glm::mix(col, glm::vec3(5,5,4.5f), 1.0f - age / 0.12f);
			float alpha = env * (0.25f + 0.75f * (1.0f - age));
			rbuf.push_back(p.x); rbuf.push_back(p.y); rbuf.push_back(p.z);
			rbuf.push_back(width);
			rbuf.push_back(col.x); rbuf.push_back(col.y); rbuf.push_back(col.z);
			rbuf.push_back(alpha);
		}
		uint32_t rc = (uint32_t)(rbuf.size() / 8);
		if (rc >= 2) m_rhi->drawRibbon(scene, rbuf.data(), rc);
	}
}

// ─────────────────────────────────────────────────────────────────────────
// HUD — lightbulbs, HP bars, hotbar, player HP, crosshair
// ─────────────────────────────────────────────────────────────────────────

namespace {
// Character cell sizes in rhi_ui.cpp: kCharWNdc=0.018, kCharHNdc=0.032.
// Keep values in sync — if rhi_ui.cpp changes, mirror here.
constexpr float kCharWNdc = 0.018f;
constexpr float kCharHNdc = 0.032f;
}

void Game::renderHUD() {
	// Crosshair in the middle.
	const float crosshairCol[4] = { 1.0f, 1.0f, 1.0f, 0.75f };
	m_rhi->drawRect2D(-0.003f, -0.006f, 0.006f, 0.012f, crosshairCol);
	m_rhi->drawRect2D(-0.010f, -0.0015f, 0.020f, 0.003f, crosshairCol);

	// ── Per-NPC lightbulb indicator + HP bar ────────────────────────────
	for (const auto& n : m_npcs) {
		if (n.state == Npc::State::Dying) continue;
		glm::vec3 anchor = n.pos + glm::vec3(0, 2.1f, 0);
		glm::vec3 ndc;
		if (!projectWorld(anchor, ndc)) continue;

		// "!" lightbulb
		float t = m_wallTime;
		float pulse = 1.0f + 0.08f * std::sin(t * 3.2f + n.id * 0.9f);
		float scale = 1.7f * pulse;
		float gw = kCharWNdc * scale;
		float gh = kCharHNdc * scale;
		float gx = ndc.x - gw * 0.5f;
		glm::vec4 tint = n.tint();
		m_rhi->drawTitle2D("!", gx, ndc.y, scale, &tint.x);

		// Goal label above the "!"
		std::string label = n.goalText();
		float rawW = label.size() * kCharWNdc;
		float maxW = 0.50f;
		float lScale = rawW > maxW ? std::max(0.55f, maxW / rawW) : 0.95f;
		float lW = rawW * lScale;
		float lX = ndc.x - lW * 0.5f;
		float lY = ndc.y + gh + 0.020f;
		m_rhi->drawText2D(label.c_str(), lX, lY, lScale, &tint.x);

		// HP bar — red back, green fill, below the "!"
		float hpFrac = std::clamp((float)n.hp / (float)n.maxHp, 0.0f, 1.0f);
		float barW = 0.10f, barH = 0.010f;
		float barX = ndc.x - barW * 0.5f;
		float barY = ndc.y - 0.020f;
		const float bgCol[4] = {0.12f, 0.08f, 0.08f, 0.85f};
		const float fillCol[4] = {0.20f, 0.78f, 0.28f, 1.0f};
		const float dmgCol [4] = {0.78f, 0.20f, 0.18f, 1.0f};
		m_rhi->drawRect2D(barX, barY, barW, barH, bgCol);
		m_rhi->drawRect2D(barX, barY, barW * hpFrac, barH, hpFrac > 0.35f ? fillCol : dmgCol);
	}

	// ── Floating damage numbers ─────────────────────────────────────────
	for (const auto& f : m_floaters) {
		float u = f.t / f.lifetime;
		glm::vec3 world = f.worldPos + glm::vec3(0, u * f.rise, 0);
		glm::vec3 ndc;
		if (!projectWorld(world, ndc)) continue;
		float alpha = 1.0f - u;
		float scale = 1.0f + 0.4f * (1.0f - u);  // pop then shrink
		float rawW = f.text.size() * kCharWNdc * scale;
		float x = ndc.x - rawW * 0.5f;
		float rgba[4] = { f.color.x, f.color.y, f.color.z, alpha };
		m_rhi->drawTitle2D(f.text.c_str(), x, ndc.y, scale, rgba);
	}

	// ── Player HP bar (top-left) ────────────────────────────────────────
	{
		float x = -0.95f, y = 0.90f;
		float w = 0.40f, h = 0.035f;
		float hpFrac = std::clamp(m_player.hp / (float)kTune.playerMaxHP, 0.0f, 1.0f);
		const float bg[4]   = {0.08f, 0.06f, 0.08f, 0.75f};
		const float fill[4] = {0.85f, 0.25f, 0.25f, 0.95f};
		m_rhi->drawRect2D(x, y, w, h, bg);
		m_rhi->drawRect2D(x + 0.002f, y + 0.002f,
			(w - 0.004f) * hpFrac, h - 0.004f, fill);
		// Text overlay
		char buf[32]; std::snprintf(buf, sizeof(buf), "HP %d / %d",
			(int)std::round(m_player.hp), kTune.playerMaxHP);
		const float txt[4] = {1,1,1,1};
		m_rhi->drawText2D(buf, x + 0.01f, y + 0.008f, 0.9f, txt);
	}

	// ── Coin counter (top-right) ────────────────────────────────────────
	{
		char buf[32]; std::snprintf(buf, sizeof(buf), "COINS %d", m_coins);
		const float gold[4] = {1.0f, 0.72f, 0.25f, 1.0f};
		m_rhi->drawTitle2D(buf, 0.70f, 0.92f, 1.2f, gold);
	}

	// ── Hotbar (bottom-center, 10 slots) ────────────────────────────────
	{
		const int N = 10;
		float slotW = 0.08f, gap = 0.008f;
		float totalW = N * slotW + (N-1) * gap;
		float x0 = -totalW * 0.5f;
		float y0 = -0.95f;
		float h  = 0.09f;
		const float bg[4]    = {0.06f, 0.05f, 0.08f, 0.70f};
		const float sel[4]   = {0.95f, 0.75f, 0.25f, 0.95f};
		const float slot[4]  = {0.18f, 0.16f, 0.20f, 0.85f};
		m_rhi->drawRect2D(x0 - 0.01f, y0 - 0.01f, totalW + 0.02f, h + 0.02f, bg);
		for (int i = 0; i < N; i++) {
			float x = x0 + i * (slotW + gap);
			m_rhi->drawRect2D(x, y0, slotW, h, slot);
			// Highlight slot 1 ("sword") always
			if (i == 0) m_rhi->drawRect2D(x, y0, slotW, 0.004f, sel); // top line
			if (i == 0) m_rhi->drawRect2D(x, y0 + h - 0.004f, slotW, 0.004f, sel);
			if (i == 0) m_rhi->drawRect2D(x, y0, 0.004f, h, sel);
			if (i == 0) m_rhi->drawRect2D(x + slotW - 0.004f, y0, 0.004f, h, sel);
			// Label: slot number
			char lab[4]; std::snprintf(lab, sizeof(lab), "%d", (i + 1) % 10);
			const float dim[4] = {0.85f, 0.85f, 0.90f, 0.95f};
			m_rhi->drawText2D(lab, x + 0.006f, y0 + 0.065f, 0.7f, dim);
			// Item icon text (minimum viable)
			if (i == 0) {
				const float gold2[4] = {1.0f, 0.75f, 0.30f, 1.0f};
				m_rhi->drawTitle2D("SWORD", x + 0.006f, y0 + 0.03f, 0.65f, gold2);
			}
		}
		// Coins icon in slot 2
		{
			float x = x0 + 1 * (slotW + gap);
			const float gc[4] = {1.0f, 0.72f, 0.25f, 1.0f};
			char buf[16]; std::snprintf(buf, sizeof(buf), "%d", m_coins);
			m_rhi->drawTitle2D("$", x + 0.01f, y0 + 0.038f, 1.5f, gc);
			m_rhi->drawText2D(buf, x + 0.038f, y0 + 0.046f, 0.85f, gc);
		}
	}

	// ── FPS + position readout (bottom-left) ────────────────────────────
	{
		char buf[96];
		std::snprintf(buf, sizeof(buf), "FPS %3.0f  POS %5.1f %5.1f %5.1f  NPC %zu",
			ImGui::GetIO().Framerate,
			m_player.pos.x, m_player.pos.y, m_player.pos.z,
			m_npcs.size());
		const float dim[4] = {0.75f, 0.80f, 0.88f, 0.90f};
		m_rhi->drawText2D(buf, -0.95f, -0.98f, 0.75f, dim);
	}
}

void Game::renderMenu() {
	// Semi-opaque dusk scrim — lets the sky + rotating world preview bleed
	// through behind the menu so the screen isn't a flat black box.
	const float bg[4] = { 0.02f, 0.03f, 0.06f, 0.55f };
	m_rhi->drawRect2D(-1.2f, -1.2f, 2.4f, 2.4f, bg);

	// Decorative header band
	float pulse = 0.85f + 0.15f * std::sin(m_menuTitleT * 1.8f);
	float gold[4] = { 1.0f * pulse, 0.72f * pulse, 0.25f * pulse, 1.0f };
	m_rhi->drawTitle2D("CIVCRAFT  VULKAN", -0.42f, 0.55f, 2.8f, gold);

	const float tag[4] = {0.85f, 0.80f, 0.70f, 0.90f};
	m_rhi->drawText2D("A Vulkan-native playable slice.", -0.26f, 0.44f, 1.0f, tag);

	// ImGui button panel — centered. We use raw ImGui here because ImGui
	// already ships with both backends and handles hit-testing for free.
	ImGuiIO& io = ImGui::GetIO();
	ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.55f);
	ImGui::SetNextWindowPos(ImVec2(center.x - 160, center.y), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(320, 240), ImGuiCond_Always);
	ImGui::Begin("##menu", nullptr,
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground);
	ImGui::Spacing();
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.18f, 0.28f, 0.95f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.45f, 0.28f, 0.40f, 0.95f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.60f, 0.35f, 0.50f, 1.00f));
	// Also accept ENTER as "press PLAY" for keyboard-only / headless flows.
	bool enterPressed = (ImGui::IsKeyPressed(ImGuiKey_Enter, false)
	                  || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
	if (ImGui::Button("  PLAY  ", ImVec2(280, 52)) || enterPressed) enterPlaying();
	ImGui::Spacing();
	if (ImGui::Button("  HOW TO PLAY  ", ImVec2(280, 38))) {
		// Toggle help overlay via ImGui tooltip in-place.
		ImGui::OpenPopup("help");
	}
	if (ImGui::BeginPopup("help")) {
		ImGui::Text("Move      WASD");
		ImGui::Text("Jump      Space");
		ImGui::Text("Look      Mouse");
		ImGui::Text("Attack    Left Click");
		ImGui::Text("Sprint    Shift");
		ImGui::Text("Menu      Esc");
		if (ImGui::Button("OK", ImVec2(120, 28))) ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
	ImGui::Spacing();
	if (ImGui::Button("  QUIT  ", ImVec2(280, 38))) m_shouldQuit = true;
	ImGui::PopStyleColor(3);
	ImGui::PopStyleVar();
	ImGui::End();

	if (!m_lastDeathReason.empty()) {
		const float red[4] = {1.0f, 0.45f, 0.35f, 1.0f};
		m_rhi->drawText2D(m_lastDeathReason.c_str(), -0.25f, -0.55f, 1.0f, red);
	}
}

void Game::renderDeath() {
	const float veil[4] = {0.0f, 0.0f, 0.0f, 0.65f};
	m_rhi->drawRect2D(-1.2f, -1.2f, 2.4f, 2.4f, veil);
	const float red[4] = { 0.95f, 0.25f, 0.20f, 1.0f };
	m_rhi->drawTitle2D("YOU DIED", -0.25f, 0.15f, 3.0f, red);
	const float hint[4] = { 0.85f, 0.85f, 0.90f, 0.95f };
	m_rhi->drawText2D("Press  R  (or click Respawn)   |   Esc  for menu",
		-0.32f, -0.05f, 1.1f, hint);

	// Clickable Respawn / Menu buttons so the mouse-free flow works without
	// the keyboard. Mirrors the main-menu ImGui window.
	ImGuiIO& io = ImGui::GetIO();
	ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.60f);
	ImGui::SetNextWindowPos(ImVec2(center.x - 140, center.y), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(280, 120), ImGuiCond_Always);
	ImGui::Begin("##dead", nullptr,
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground);
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.15f, 0.15f, 0.95f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.25f, 0.25f, 0.95f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.85f, 0.35f, 0.35f, 1.00f));
	if (ImGui::Button("  RESPAWN  ", ImVec2(240, 44))) respawn();
	ImGui::Spacing();
	if (ImGui::Button("  MAIN MENU  ", ImVec2(240, 34))) enterMenu();
	ImGui::PopStyleColor(3);
	ImGui::PopStyleVar();
	ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────
// Main loop — state dispatch
// ─────────────────────────────────────────────────────────────────────────

void Game::runOneFrame(float dt, float wallTime) {
	m_wallTime = wallTime;
	m_menuTitleT += dt;

	int w = 0, h = 0;
	glfwGetFramebufferSize(m_window, &w, &h);
	m_fbW = w; m_fbH = h;
	m_aspect = h > 0 ? (float)w / (float)h : 1.0f;

	// ── Sim ────────────────────────────────────────────────────────────
	processInput(dt);
	if (m_state == GameState::Playing) {
		tickPlayer(dt);
		tickNpcs(dt);
		tickCombat(dt);
		tickFloaters(dt);
	} else if (m_state == GameState::Dead) {
		// 'R' respawns. Also check for a file trigger (used by headless
		// test harnesses that can't reliably inject keys).
		bool r = glfwGetKey(m_window, GLFW_KEY_R) == GLFW_PRESS;
		bool fileTrigger = (access("/tmp/civcraft_respawn_request", F_OK) == 0);
		if (r || fileTrigger) {
			if (fileTrigger) std::remove("/tmp/civcraft_respawn_request");
			respawn();
		}
	}

	// ── Render ─────────────────────────────────────────────────────────
	if (!m_rhi->beginFrame()) return;

	if (m_state == GameState::Menu) {
		// Render a calm ambient backdrop so the menu isn't just a
		// solid rect — reuse the sky + a faraway camera orbit.
		float menuAng = m_menuTitleT * 0.08f;
		m_player.pos = glm::vec3(std::sin(menuAng) * 2.0f, 7.0f, std::cos(menuAng) * 2.0f);
		m_camYaw = menuAng + 3.14f * 0.5f;
		m_camPitch = -0.15f;
		renderWorld(wallTime);
		renderEffects(wallTime);
		m_rhi->imguiNewFrame();
		renderMenu();
		m_rhi->imguiRender();
	} else if (m_state == GameState::Playing) {
		renderWorld(wallTime);
		renderEntities(wallTime);
		renderEffects(wallTime);
		m_rhi->imguiNewFrame();
		renderHUD();
		m_rhi->imguiRender();
	} else {  // Dead
		renderWorld(wallTime);
		renderEffects(wallTime);
		m_rhi->imguiNewFrame();
		renderHUD();       // still show world state behind the veil
		renderDeath();
		m_rhi->imguiRender();
	}

	// F2 / file-trigger screenshot. Has to happen while a frame is active —
	// VkRhi::screenshot ends the current render pass, copies the swap image
	// to a host buffer, and submits. Called right before endFrame so HUD and
	// ImGui are captured.
	static bool f2Held = false;
	bool f2Now = glfwGetKey(m_window, GLFW_KEY_F2) == GLFW_PRESS;
	bool fileTrigger = (access("/tmp/civcraft_screenshot_request", F_OK) == 0);
	if ((f2Now && !f2Held) || fileTrigger) {
		static int shotN = 0;
		char path[256];
		snprintf(path, sizeof(path), "/tmp/civcraft_vk_screenshot_%d.ppm", shotN++);
		if (m_rhi->screenshot(path))
			std::printf("[vk] wrote %s\n", path);
		else
			std::printf("[vk] screenshot failed: %s\n", path);
		if (fileTrigger) std::remove("/tmp/civcraft_screenshot_request");
	}
	f2Held = f2Now;

	m_rhi->endFrame();
}

} // namespace civcraft::vk
