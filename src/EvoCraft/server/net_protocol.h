// EvoCraft TCP wire format — fresh protocol, not shared with ModCraft.
//
// Framing (all little-endian; we target x86-64 only for now):
//
//   u32 payloadLen         // bytes to follow, EXCLUDING this length field
//   u16 msgType            // first 2 bytes of the payload
//   u8  body[payloadLen-2] // msg-specific fields
//
// Message IDs are in one enum so modders can grep a single place to discover
// the protocol. Reserve [1..1000] for server→client, [1001..2000] for
// client→server, [2001..] for future broadcast/meta.

#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace evocraft::net {

enum MsgType : uint16_t {
	// --- server → client ---
	S_HELLO         = 1,  // proto + server name + u32 playerCellId (0 = none)
	S_TICK          = 2,  // heartbeat: u64 tick, f32 simTime
	S_CELLS         = 3,  // snapshot: cell pose + species + size + hp + diet
	S_FOOD          = 4,  // snapshot: food pose + kind
	S_PLAYER_STATS  = 5,  // player-only: f32 hp, f32 maxHp, u32 dna
	S_PLAYER_PARTS  = 6,  // player-only: u16 count + count × {u8,f32,f32}

	// --- client → server ---
	C_HELLO         = 1001, // client version + desired playable species id
	C_PLAYER_INPUT  = 1002, // f32 vx, f32 vz — desired velocity direction (normalized)
	C_BUY_PART      = 1003, // u8 kind, f32 angle, f32 distance — editor commit
	C_RESET_PARTS   = 1004, // no body — clear all parts (refunds DNA)
};

constexpr uint32_t PROTO_VERSION = 6;  // bumped: parts

// Diet enum — drives what food a cell can eat and what mouth it has.
enum Diet : uint8_t {
	DIET_CARNIVORE = 0,  // jaw — eats meat (drops from killed cells)
	DIET_HERBIVORE = 1,  // filter mouth — eats green plants
	DIET_OMNIVORE  = 2,  // proboscis OR jaw + filter — eats anything
};

// Food kind enum — matched against eater's diet.
enum FoodKind : uint8_t {
	FOOD_PLANT = 0,  // green pellet — herbivores + omnivores
	FOOD_MEAT  = 1,  // red chunk dropped on cell death — carnivores + omnivores
	FOOD_EGG   = 2,  // white sphere — anyone can eat; spawns later in stage
};

// Reserved cell ID for the player. Single-player scope: there's exactly one.
constexpr uint32_t PLAYER_CELL_ID = 1;

// Spore canonical part kinds. Costs/effects authoritative on the server in
// part_table.h; client only needs the kind enum to render and label.
enum PartKind : uint8_t {
	PART_FILTER     = 0,   // herbivore mouth, 15 DNA
	PART_JAW        = 1,   // carnivore mouth, 15 DNA
	PART_PROBOSCIS  = 2,   // omnivore mouth, 25 DNA
	PART_SPIKE      = 3,   // melee, 10 DNA, max 1
	PART_POISON     = 4,   // proximity DoT, 15 DNA, max 6
	PART_ELECTRIC   = 5,   // burst, 25 DNA, max 6
	PART_FLAGELLA   = 6,   // movement, 15 DNA, +1 spd
	PART_CILIA      = 7,   // movement, 15 DNA, +fast turn
	PART_JET        = 8,   // movement, 25 DNA, +2 spd
	PART_EYE_BEADY  = 9,   // sight, 5 DNA
	PART_EYE_STALK  = 10,  // sight, 5 DNA
	PART_EYE_BUTTON = 11,  // sight, 5 DNA
	PART_COUNT      = 12,
};

struct PartRecord {
	uint8_t kind;
	float   angle;     // radians around cell center
	float   distance;  // 0..1, fraction of cell radius
};

// --- little-endian pack helpers ---------------------------------------------

inline void pack_u16(std::vector<uint8_t>& out, uint16_t v) {
	out.push_back((uint8_t)(v & 0xff));
	out.push_back((uint8_t)(v >> 8));
}
inline void pack_u32(std::vector<uint8_t>& out, uint32_t v) {
	for (int i = 0; i < 4; ++i) out.push_back((uint8_t)(v >> (i * 8)));
}
inline void pack_u64(std::vector<uint8_t>& out, uint64_t v) {
	for (int i = 0; i < 8; ++i) out.push_back((uint8_t)(v >> (i * 8)));
}
inline void pack_f32(std::vector<uint8_t>& out, float v) {
	uint32_t bits;
	std::memcpy(&bits, &v, sizeof(bits));
	pack_u32(out, bits);
}
inline void pack_str(std::vector<uint8_t>& out, const std::string& s) {
	pack_u16(out, (uint16_t)s.size());
	out.insert(out.end(), s.begin(), s.end());
}

// --- little-endian unpack helpers (inbound) ---------------------------------

inline uint16_t unpack_u16(const uint8_t* p) {
	return (uint16_t)(p[0] | (p[1] << 8));
}
inline uint32_t unpack_u32(const uint8_t* p) {
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
inline float unpack_f32(const uint8_t* p) {
	uint32_t bits = unpack_u32(p);
	float v;
	std::memcpy(&v, &bits, sizeof(v));
	return v;
}

// Frame a body (starting with u16 msgType) by prepending its length.
inline std::vector<uint8_t> frame(const std::vector<uint8_t>& body) {
	std::vector<uint8_t> out;
	out.reserve(4 + body.size());
	pack_u32(out, (uint32_t)body.size());
	out.insert(out.end(), body.begin(), body.end());
	return out;
}

// --- message builders -------------------------------------------------------

inline std::vector<uint8_t> build_s_hello(const std::string& serverName,
		uint32_t playerCellId) {
	std::vector<uint8_t> body;
	pack_u16(body, S_HELLO);
	pack_u32(body, PROTO_VERSION);
	pack_str(body, serverName);
	pack_u32(body, playerCellId);
	return frame(body);
}

inline std::vector<uint8_t> build_s_tick(uint64_t tick, float simTime) {
	std::vector<uint8_t> body;
	pack_u16(body, S_TICK);
	pack_u64(body, tick);
	pack_f32(body, simTime);
	return frame(body);
}

// Compact cell snapshot — one record per live cell. Spore Cell Stage runs on
// the XZ plane (top-down petri dish), so Y stays ≈ 0; angle is the heading
// in radians around +Y. hp/maxHp drive the client's HP-bar overlay; diet
// drives mouth/eye color tinting.
struct CellRecord {
	uint32_t id;
	float    x, y, z;
	float    angle;
	uint8_t  species;
	float    size;
	float    hp;
	float    maxHp;
	uint8_t  diet;      // Diet enum
};

struct FoodRecord {
	uint32_t id;
	float    x, y, z;
	uint8_t  kind;      // FoodKind enum
};

inline std::vector<uint8_t> build_s_food(
		uint64_t tick, const std::vector<FoodRecord>& food) {
	std::vector<uint8_t> body;
	pack_u16(body, S_FOOD);
	pack_u64(body, tick);
	pack_u16(body, (uint16_t)food.size());
	for (const auto& f : food) {
		pack_u32(body, f.id);
		pack_f32(body, f.x);
		pack_f32(body, f.y);
		pack_f32(body, f.z);
		body.push_back(f.kind);
	}
	return frame(body);
}

inline std::vector<uint8_t> build_s_cells(
		uint64_t tick, const std::vector<CellRecord>& cells) {
	std::vector<uint8_t> body;
	pack_u16(body, S_CELLS);
	pack_u64(body, tick);
	pack_u16(body, (uint16_t)cells.size());
	for (const auto& c : cells) {
		pack_u32(body, c.id);
		pack_f32(body, c.x);
		pack_f32(body, c.y);
		pack_f32(body, c.z);
		pack_f32(body, c.angle);
		body.push_back(c.species);
		pack_f32(body, c.size);
		pack_f32(body, c.hp);
		pack_f32(body, c.maxHp);
		body.push_back(c.diet);
	}
	return frame(body);
}

// Player-only stats broadcast — the player's own HP bar + DNA wallet. Sent
// after every sim tick (cheap: ~12 bytes) so HUD stays in lockstep with
// the simulation, no client-side prediction needed.
inline std::vector<uint8_t> build_s_player_stats(
		float hp, float maxHp, uint32_t dna) {
	std::vector<uint8_t> body;
	pack_u16(body, S_PLAYER_STATS);
	pack_f32(body, hp);
	pack_f32(body, maxHp);
	pack_u32(body, dna);
	return frame(body);
}

// Player parts snapshot. Sent only when the part list changes (rare event:
// editor commit / reset / death). Total size scales with count, but max 24
// parts × 9 bytes = ~220 B is trivial.
inline std::vector<uint8_t> build_s_player_parts(
		const std::vector<PartRecord>& parts) {
	std::vector<uint8_t> body;
	pack_u16(body, S_PLAYER_PARTS);
	pack_u16(body, (uint16_t)parts.size());
	for (const auto& p : parts) {
		body.push_back(p.kind);
		pack_f32(body, p.angle);
		pack_f32(body, p.distance);
	}
	return frame(body);
}

// --- inbound parse ----------------------------------------------------------

struct PlayerInput {
	float vx;
	float vz;
};

struct BuyPart {
	uint8_t kind;
	float   angle;
	float   distance;
};

// Parse a C_PLAYER_INPUT body (msgType already consumed). Returns true on
// success. Body is exactly 8 bytes after the u16 msgType: { f32 vx, f32 vz }.
inline bool parse_c_player_input(const uint8_t* body, size_t n,
		PlayerInput& out) {
	if (n < 8) return false;
	out.vx = unpack_f32(body);
	out.vz = unpack_f32(body + 4);
	return true;
}

// Parse a C_BUY_PART body (msgType already consumed): { u8 kind, f32 angle,
// f32 distance } = 9 bytes total.
inline bool parse_c_buy_part(const uint8_t* body, size_t n, BuyPart& out) {
	if (n < 9) return false;
	out.kind     = body[0];
	out.angle    = unpack_f32(body + 1);
	out.distance = unpack_f32(body + 5);
	return true;
}

} // namespace evocraft::net
