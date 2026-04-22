#pragma once

/**
 * Network protocol for client-server communication.
 *
 * Wire format: [4 bytes: MsgType] [4 bytes: payload length] [payload bytes]
 *
 * Protocol versioning
 * ───────────────────
 * C_HELLO payload begins with [u32 version]. Servers that see version >= 2
 * send S_CHUNK_Z (zstd-compressed) instead of S_CHUNK (uncompressed).
 * WASM clients send version = 1 and always receive uncompressed S_CHUNK.
 *
 * Message index
 * ─────────────
 * C_ACTION        0x0001  ActionProposal (move / block / attack …)
 * C_HELLO         0x0003  GUI client hello  [u32 version][str uuid][str name][str skin]
 * C_SET_GOAL      0x0008  [u32 entityId][f32 x][f32 y][f32 z]
 * C_CANCEL_GOAL   0x0009  [u32 entityId]
 * C_SET_GOAL_GROUP 0x000B [f32 x][f32 y][f32 z][u32 count][u32 eid...]
 * C_GET_INVENTORY 0x000D  [u32 entityId]
 * C_QUIT          0x000E  []  — graceful disconnect; server runs full cleanup
 * C_HEARTBEAT     0x000F  []  — liveness ping; resets server-side idle timer
 *
 * S_WELCOME       0x1001  [u32 entityId][vec3 spawn]
 * S_ENTITY        0x1002  EntityState (see serializeEntityState)
 * S_CHUNK         0x1003  Uncompressed chunk [i32 cx][i32 cy][i32 cz][u32×4096]
 *                          [u8×4096 appearance]  (v5+)
 *                          [u32 annotCount]{[i32 dx][i32 dy][i32 dz][str typeId][u8 slot]}×N
 * S_REMOVE        0x1004  [u32 entityId]
 * S_TIME          0x1005  [f32 worldTime][u32 dayCount?]  (dayCount optional for back-compat)
 * S_BLOCK         0x1006  [i32 x][i32 y][i32 z][u32 blockId][u8 param2][u8 appearance?]  (appearance v5+)
 * S_INVENTORY     0x1007  [u32 eid][u32 n][str×n id][i32×n count][u8 equipN][str×equipN slot][str×equipN id]
 * S_ERROR         0x100B  [u32 entityId][str message]
 * S_CHUNK_EVICT   0x100E  Discard chunk [i32 cx][i32 cy][i32 cz]  (v2)
 * S_CHUNK_Z       0x100F  zstd-compressed chunk (v2); decompressed payload == S_CHUNK
 * S_NPC_INTERRUPT 0x1010  [u32 eid][str reason]                  (v3, TODO(decide-loop))
 * S_WORLD_EVENT   0x1011  [str kind][str payload]                (v3, TODO(decide-loop))
 * S_ANNOTATION_SET 0x1014 [i32 x][i32 y][i32 z][str typeId — empty = remove][u8 slot]
 * S_WEATHER       0x1015  [str kind][f32 intensity][f32 windX][f32 windZ][u32 seq]  (v3)
 */

#include "logic/entity.h"
#include "logic/action.h"
#include "logic/chunk.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>

namespace civcraft::net {

// C_HELLO version; server picks S_CHUNK vs S_CHUNK_Z. Bump on wire changes.
// v3: adds S_WEATHER.
// v4: adds S_ENTITY_DELTA — field-bitmap + quantized per-entity updates. Server
//     sends S_ENTITY_DELTA for v4+ clients, legacy S_ENTITY for v3.
// v5: S_CHUNK carries a trailing [u8 × CHUNK_VOLUME] appearance array after the
//     packed block data and before the annotation tail. S_BLOCK gains a trailing
//     u8 appearance index (read conditionally via hasMore for back-compat reads).
// v7: S_BLOCK_BATCH — zstd-compressed bundle of block changes. Used for
//     tick-driven environmental mutations (seasonal leaf repaint, structure
//     regen, wheat growth, etc.) so hundreds of per-block broadcasts collapse
//     into one coalesced packet per flush window. Individual player/NPC
//     actions keep using synchronous S_BLOCK for minimum latency.
// v8: S_REMOVE — trailing u8 `reason` (EntityRemoveReason). v7 clients still
//     parse the id-only prefix; newer clients branch on reason for FX/SFX
//     (puff particle + no death sound on owner_offline).
// v9: ActionProposal.placeParam2 (u8, after Convert.convertInto). Client picks
//     the orientation at placement time (Tab/MMB-scroll) instead of the
//     server hardcoding it. Mixed-version chatter isn't supported — the
//     field's position is load-bearing for rehydrating the Interact tail.
static constexpr uint32_t PROTOCOL_VERSION = 9;

// S_REMOVE trailing byte. Server writes it unconditionally (from v8); a v7
// client stops reading after the entity id, so appending a byte is safe.
// Wire values live on Entity itself (logic/entity.h::EntityRemovalReason)
// so the server tick loop can record a reason without depending on net/.
using EntityRemoveReason = EntityRemovalReason;

enum MsgType : uint32_t {
	// Client → Server
	C_ACTION          = 0x0001,
	C_HELLO           = 0x0003,  // [u32 version][str uuid][str displayName][str creatureType]
	C_SET_GOAL        = 0x0008,  // [u32 entityId][f32 x][f32 y][f32 z]
	C_CANCEL_GOAL     = 0x0009,  // [u32 entityId]
	C_SET_GOAL_GROUP  = 0x000B,  // [f32 x][f32 y][f32 z][u32 count][u32 eid...]
	C_GET_INVENTORY   = 0x000D,  // [u32 entityId] — request entity inventory snapshot
	C_QUIT            = 0x000E,  // []  — graceful disconnect (client leaving); server runs full cleanup
	C_HEARTBEAT       = 0x000F,  // []  — keepalive ping; resets server's per-client idle timer
	C_PING            = 0x0010,  // [u64 token] — RTT probe; server replies S_PONG echoing token

	// Server → Client
	S_WELCOME         = 0x1001,
	S_ENTITY          = 0x1002,
	S_CHUNK           = 0x1003,  // uncompressed chunk (v1 clients or fallback)
	S_REMOVE          = 0x1004,  // [u32 entityId][u8 reason (v8; EntityRemoveReason)]
	S_TIME            = 0x1005,
	S_BLOCK           = 0x1006,
	S_INVENTORY       = 0x1007,
	S_ERROR           = 0x100B,
	S_CHUNK_EVICT     = 0x100E,  // discard chunk from client cache: [i32 cx][i32 cy][i32 cz]  (v2)
	S_CHUNK_Z         = 0x100F,  // zstd-compressed chunk; decompresses to S_CHUNK layout  (v2)
	S_READY           = 0x1012,  // [] — sent after addClient finishes mob spawn. Client holds
	                             // the LOADING screen until this arrives.
	S_PREPARING       = 0x1013,  // [f32 progress 0..1] — emitted while the server
	                             // generates required chunks for this client. Client
	                             // shows a progress UI; heartbeat-timeout is suppressed
	                             // until S_READY arrives. No entities exist yet.

	// Event-driven decision-loop interrupts — see plans/cosmic-tinkering-forest.md
	// TODO(decide-loop): not yet emitted by server nor handled by client. Step 7
	// wires them to AgentClient::onInterrupt / onWorldEvent.
	S_NPC_INTERRUPT   = 0x1010,  // [u32 entityId][str reason]  ("proximity", ...)
	S_WORLD_EVENT     = 0x1011,  // [str kind][str payload]     ("time_of_day", "day"|"night")

	// Annotation (block decorator) streaming — single-cell updates; bulk
	// annotations piggyback on S_CHUNK / S_CHUNK_Z after the block data.
	S_ANNOTATION_SET  = 0x1014,  // [i32 x][i32 y][i32 z][str typeId][u8 slot]
	                             //   typeId=="" means remove annotation at pos

	// Weather state — broadcast on seq change + on join. Global (one kind
	// for the whole world). Server owns the state; client renders it.
	S_WEATHER         = 0x1015,  // [str kind][f32 intensity][f32 windX][f32 windZ][u32 seq]

	// Delta-encoded entity state. v4+ clients only.
	// [u32 eid][u32 fieldMask] + (for each set bit, the quantized field payload).
	// See EntityStateField + serializeEntityStateDelta() below for the exact layout.
	// Broadcasts where fieldMask == 0 are suppressed server-side; if the client
	// receives one (e.g. on newly-visible edge) it's a no-op.
	S_ENTITY_DELTA    = 0x1016,

	S_PONG            = 0x1017,  // [u64 token] — echo of C_PING; client times RTT

	// Batched block changes — zstd-compressed list of (i32 x,i32 y,i32 z,
	// u32 bid, u8 p2, u8 app) tuples, prefixed with a u32 entry count.
	// Server uses this for low-priority environmental sweeps that would
	// otherwise emit hundreds of per-block S_BLOCKs in one tick; per-entry
	// semantics are identical to S_BLOCK. Added in v7.
	S_BLOCK_BATCH     = 0x1018,
};

// 8-byte frame header.
struct MsgHeader {
	uint32_t type;
	uint32_t length;  // payload bytes (no header)
};

// memcpy-based binary encoding.
class WriteBuffer {
public:
	void writeU8(uint8_t v)   { write(&v, 1); }
	void writeU16(uint16_t v) { write(&v, 2); }
	void writeI16(int16_t v)  { write(&v, 2); }
	void writeU32(uint32_t v) { write(&v, 4); }
	void writeI32(int32_t v)  { write(&v, 4); }
	void writeU64(uint64_t v) { write(&v, 8); }
	void writeF32(float v)    { write(&v, 4); }
	void writeVec3(glm::vec3 v) { writeF32(v.x); writeF32(v.y); writeF32(v.z); }
	void writeIVec3(glm::ivec3 v) { writeI32(v.x); writeI32(v.y); writeI32(v.z); }
	void writeBool(bool v) { uint8_t b = v ? 1 : 0; write(&b, 1); }
	void writeString(const std::string& s) {
		writeU32((uint32_t)s.size());
		write(s.data(), s.size());
	}

	const std::vector<uint8_t>& data() const { return m_data; }
	size_t size() const { return m_data.size(); }

	// Append opaque bytes (e.g. a pre-compressed zstd payload).
	void writeBytes(const void* ptr, size_t n) { write(ptr, n); }

private:
	void write(const void* ptr, size_t n) {
		const uint8_t* p = (const uint8_t*)ptr;
		m_data.insert(m_data.end(), p, p + n);
	}
	std::vector<uint8_t> m_data;
};

class ReadBuffer {
public:
	ReadBuffer(const uint8_t* data, size_t size) : m_data(data), m_size(size) {}

	uint8_t  readU8()  { uint8_t  v; read(&v, 1); return v; }
	uint16_t readU16() { uint16_t v; read(&v, 2); return v; }
	int16_t  readI16() { int16_t  v; read(&v, 2); return v; }
	uint32_t readU32() { uint32_t v; read(&v, 4); return v; }
	int32_t  readI32() { int32_t  v; read(&v, 4); return v; }
	uint64_t readU64() { uint64_t v; read(&v, 8); return v; }
	float    readF32() { float    v; read(&v, 4); return v; }
	glm::vec3 readVec3() { float x = readF32(), y = readF32(), z = readF32(); return {x,y,z}; }
	glm::ivec3 readIVec3() { int x = readI32(), y = readI32(), z = readI32(); return {x,y,z}; }
	bool readBool() { uint8_t b; read(&b, 1); return b != 0; }
	std::string readString() {
		uint32_t len = readU32();
		if (m_pos + len > m_size) len = 0; // bounds check
		std::string s((const char*)(m_data + m_pos), len);
		m_pos += len;
		return s;
	}

	bool hasMore() const { return m_pos < m_size; }
	size_t remaining() const { return (m_pos < m_size) ? m_size - m_pos : 0; }

	// For external decoders (zstd).
	const uint8_t* remainingData() const { return m_data + m_pos; }

private:
	void read(void* ptr, size_t n) {
		if (m_pos + n <= m_size)
			memcpy(ptr, m_data + m_pos, n);
		m_pos += n;
	}
	const uint8_t* m_data;
	size_t m_size;
	size_t m_pos = 0;
};

// --- ActionProposal ---

inline void writeContainer(WriteBuffer& buf, const Container& c) {
	buf.writeU8((uint8_t)c.kind);
	buf.writeU32(c.entityId);
	buf.writeIVec3(c.pos);
}

inline Container readContainer(ReadBuffer& buf) {
	Container c;
	c.kind     = (Container::Kind)buf.readU8();
	c.entityId = buf.readU32();
	c.pos      = buf.readIVec3();
	return c;
}

inline void serializeAction(WriteBuffer& buf, const ActionProposal& a) {
	buf.writeU32((uint32_t)a.type);
	buf.writeU32(a.actorId);
	// Move
	buf.writeVec3(a.desiredVel);
	buf.writeBool(a.jump);
	buf.writeBool(a.sprint);
	buf.writeBool(a.fly);
	buf.writeF32(a.jumpVelocity);
	buf.writeF32(a.lookPitch);
	buf.writeF32(a.lookYaw);
	buf.writeString(a.goalText);
	buf.writeVec3(a.clientPos);
	buf.writeBool(a.hasClientPos);
	// Relocate
	writeContainer(buf, a.relocateFrom);
	writeContainer(buf, a.relocateTo);
	buf.writeString(a.itemId);
	buf.writeI32(a.itemCount);
	buf.writeString(a.equipSlot);
	// Convert
	buf.writeString(a.fromItem);
	buf.writeI32(a.fromCount);
	buf.writeString(a.toItem);
	buf.writeI32(a.toCount);
	writeContainer(buf, a.convertFrom);
	writeContainer(buf, a.convertInto);
	buf.writeU8(a.placeParam2);
	// Interact
	buf.writeIVec3(a.blockPos);
	buf.writeI16(a.appearanceIdx);
	// Hot-reload side-channel
	buf.writeString(a.behaviorSource);
}

inline ActionProposal deserializeAction(ReadBuffer& buf) {
	ActionProposal a;
	a.type    = (ActionProposal::Type)buf.readU32();
	a.actorId = buf.readU32();
	// Move
	a.desiredVel  = buf.readVec3();
	a.jump        = buf.readBool();
	a.sprint      = buf.readBool();
	a.fly         = buf.readBool();
	a.jumpVelocity= buf.readF32();
	a.lookPitch   = buf.readF32();
	a.lookYaw     = buf.readF32();
	a.goalText    = buf.readString();
	if (!buf.hasMore()) return a;
	a.clientPos    = buf.readVec3();
	a.hasClientPos = buf.readBool();
	// Relocate
	if (!buf.hasMore()) return a;
	a.relocateFrom = readContainer(buf);
	a.relocateTo   = readContainer(buf);
	a.itemId       = buf.readString();
	a.itemCount    = buf.readI32();
	a.equipSlot    = buf.readString();
	// Convert
	if (!buf.hasMore()) return a;
	a.fromItem    = buf.readString();
	a.fromCount   = buf.readI32();
	a.toItem      = buf.readString();
	a.toCount     = buf.readI32();
	a.convertFrom = readContainer(buf);
	a.convertInto = readContainer(buf);
	if (!buf.hasMore()) return a;
	a.placeParam2 = buf.readU8();
	// Interact
	if (!buf.hasMore()) return a;
	a.blockPos = buf.readIVec3();
	if (buf.hasMore()) a.appearanceIdx = buf.readI16();
	// Hot-reload side-channel
	if (!buf.hasMore()) return a;
	a.behaviorSource = buf.readString();
	return a;
}

// --- EntityState (server → client) ---

struct EntityState {
	EntityId id;
	std::string typeId;
	glm::vec3 position;
	glm::vec3 velocity;
	float yaw;
	bool onGround;
	std::string goalText;
	std::string characterSkin; // visual override (e.g., "knight")
	int hp;
	int maxHp;
	int owner = 0;  // owning player EntityId; 0 = unowned
	glm::vec3 moveTarget = {0, 0, 0};  // for client prediction
	float moveSpeed = 0.0f;
	float lookYaw   = 0.0f;           // degrees, for remote head tracking
	float lookPitch = 0.0f;           // degrees
	// All props serialized as string k/v (float/int/bool/vec3 → string for transport).
	std::vector<std::pair<std::string, std::string>> props;
};

inline void serializeEntityState(WriteBuffer& buf, const EntityState& e) {
	buf.writeU32(e.id);
	buf.writeString(e.typeId);
	buf.writeVec3(e.position);
	buf.writeVec3(e.velocity);
	buf.writeF32(e.yaw);
	buf.writeBool(e.onGround);
	buf.writeString(e.goalText);
	buf.writeString(e.characterSkin);
	buf.writeI32(e.hp);
	buf.writeI32(e.maxHp);
	buf.writeI32(e.owner);
	buf.writeVec3(e.moveTarget);
	buf.writeF32(e.moveSpeed);
	buf.writeF32(e.lookYaw);
	buf.writeF32(e.lookPitch);
	buf.writeU32((uint32_t)e.props.size());
	for (auto& [k, v] : e.props) {
		buf.writeString(k);
		buf.writeString(v);
	}
}

inline EntityState deserializeEntityState(ReadBuffer& buf) {
	EntityState e;
	e.id = buf.readU32();
	e.typeId = buf.readString();
	e.position = buf.readVec3();
	e.velocity = buf.readVec3();
	e.yaw = buf.readF32();
	e.onGround = buf.readBool();
	e.goalText = buf.readString();
	e.characterSkin = buf.readString();
	e.hp = buf.readI32();
	e.maxHp = buf.readI32();
	if (buf.hasMore()) e.owner = buf.readI32();
	if (buf.hasMore()) e.moveTarget = buf.readVec3();
	if (buf.hasMore()) e.moveSpeed = buf.readF32();
	if (buf.hasMore()) e.lookYaw   = buf.readF32();
	if (buf.hasMore()) e.lookPitch = buf.readF32();
	uint32_t propCount = buf.readU32();
	for (uint32_t i = 0; i < propCount && buf.hasMore(); i++) {
		std::string k = buf.readString();
		std::string v = buf.readString();
		e.props.push_back({k, v});
	}
	return e;
}

// --- EntityState delta codec (S_ENTITY_DELTA, v4+) ---
//
// Wire format: [u32 eid][u32 fieldMask][fields for set bits, in bit order].
//   pos     → int32 mm per axis  (±2147 km range, 1 mm step)
//   vel     → int16 cm/s per axis (±327 m/s range, 1 cm/s step)
//   yaw/look→ int16 centidegrees (±327.68°; server wraps to [-180,180])
//   hp/maxHp/owner → i32, moveSpeed → f32, bools/strings as-is.
// Quantization thresholds below (kPosEps etc.) match the quantizer's
// resolution so a sub-step float wiggle doesn't force a resend.
enum EntityStateField : uint32_t {
	FLD_TYPE_ID     = 1u << 0,   // only sent on first broadcast per entity
	FLD_POSITION    = 1u << 1,
	FLD_VELOCITY    = 1u << 2,
	FLD_YAW         = 1u << 3,
	FLD_ON_GROUND   = 1u << 4,
	FLD_GOAL_TEXT   = 1u << 5,
	FLD_CHAR_SKIN   = 1u << 6,
	FLD_HP          = 1u << 7,
	FLD_MAX_HP      = 1u << 8,
	FLD_OWNER       = 1u << 9,
	FLD_MOVE_TARGET = 1u << 10,
	FLD_MOVE_SPEED  = 1u << 11,
	FLD_LOOK_YAW    = 1u << 12,
	FLD_LOOK_PITCH  = 1u << 13,
	FLD_PROPS       = 1u << 14,  // props diff as a set → resend whole list on change
};

// Epsilons = one quantizer step. Smaller deltas would round to the same wire
// value, so flagging them as "changed" would waste bytes for no fidelity.
static constexpr float kPosEps      = 0.001f;   // 1 mm
static constexpr float kVelEps      = 0.01f;    // 1 cm/s
static constexpr float kAngleEps    = 0.01f;    // 0.01°
static constexpr float kSpeedEps    = 0.001f;

inline uint32_t diffEntityState(const EntityState& prev, const EntityState& cur,
                                bool isFirst) {
	auto vecDiffer = [](glm::vec3 a, glm::vec3 b, float eps) {
		return std::fabs(a.x - b.x) > eps
		    || std::fabs(a.y - b.y) > eps
		    || std::fabs(a.z - b.z) > eps;
	};
	uint32_t m = 0;
	if (isFirst || prev.typeId        != cur.typeId)        m |= FLD_TYPE_ID;
	if (isFirst || vecDiffer(prev.position, cur.position, kPosEps))    m |= FLD_POSITION;
	if (isFirst || vecDiffer(prev.velocity, cur.velocity, kVelEps))    m |= FLD_VELOCITY;
	if (isFirst || std::fabs(prev.yaw - cur.yaw) > kAngleEps)          m |= FLD_YAW;
	if (isFirst || prev.onGround      != cur.onGround)      m |= FLD_ON_GROUND;
	if (isFirst || prev.goalText      != cur.goalText)      m |= FLD_GOAL_TEXT;
	if (isFirst || prev.characterSkin != cur.characterSkin) m |= FLD_CHAR_SKIN;
	if (isFirst || prev.hp            != cur.hp)            m |= FLD_HP;
	if (isFirst || prev.maxHp         != cur.maxHp)         m |= FLD_MAX_HP;
	if (isFirst || prev.owner         != cur.owner)         m |= FLD_OWNER;
	if (isFirst || vecDiffer(prev.moveTarget, cur.moveTarget, kPosEps)) m |= FLD_MOVE_TARGET;
	if (isFirst || std::fabs(prev.moveSpeed - cur.moveSpeed) > kSpeedEps) m |= FLD_MOVE_SPEED;
	if (isFirst || std::fabs(prev.lookYaw   - cur.lookYaw)   > kAngleEps) m |= FLD_LOOK_YAW;
	if (isFirst || std::fabs(prev.lookPitch - cur.lookPitch) > kAngleEps) m |= FLD_LOOK_PITCH;
	if (isFirst || prev.props         != cur.props)         m |= FLD_PROPS;
	return m;
}

namespace detail {
	inline int32_t quantPosMm(float v)  { return (int32_t)std::lround(v * 1000.0f); }
	inline float   dequantPosMm(int32_t q) { return (float)q * 0.001f; }
	inline int16_t quantVelCm(float v) {
		float c = v * 100.0f;
		if (c >  32767.0f) c =  32767.0f;
		if (c < -32768.0f) c = -32768.0f;
		return (int16_t)std::lround(c);
	}
	inline float   dequantVelCm(int16_t q) { return (float)q * 0.01f; }
	inline int16_t quantAngleCdeg(float deg) {
		// Wrap to (-180, 180] so int16 covers the full circle with headroom.
		float d = std::fmod(deg + 180.0f, 360.0f);
		if (d < 0) d += 360.0f;
		d -= 180.0f;
		float c = d * 100.0f;
		if (c >  32767.0f) c =  32767.0f;
		if (c < -32768.0f) c = -32768.0f;
		return (int16_t)std::lround(c);
	}
	inline float dequantAngleCdeg(int16_t q) { return (float)q * 0.01f; }
}

inline void serializeEntityStateDelta(WriteBuffer& buf, const EntityState& e,
                                      uint32_t fieldMask) {
	buf.writeU32(e.id);
	buf.writeU32(fieldMask);
	if (fieldMask & FLD_TYPE_ID)     buf.writeString(e.typeId);
	if (fieldMask & FLD_POSITION) {
		buf.writeI32(detail::quantPosMm(e.position.x));
		buf.writeI32(detail::quantPosMm(e.position.y));
		buf.writeI32(detail::quantPosMm(e.position.z));
	}
	if (fieldMask & FLD_VELOCITY) {
		buf.writeI16(detail::quantVelCm(e.velocity.x));
		buf.writeI16(detail::quantVelCm(e.velocity.y));
		buf.writeI16(detail::quantVelCm(e.velocity.z));
	}
	if (fieldMask & FLD_YAW)         buf.writeI16(detail::quantAngleCdeg(e.yaw));
	if (fieldMask & FLD_ON_GROUND)   buf.writeBool(e.onGround);
	if (fieldMask & FLD_GOAL_TEXT)   buf.writeString(e.goalText);
	if (fieldMask & FLD_CHAR_SKIN)   buf.writeString(e.characterSkin);
	if (fieldMask & FLD_HP)          buf.writeI32(e.hp);
	if (fieldMask & FLD_MAX_HP)      buf.writeI32(e.maxHp);
	if (fieldMask & FLD_OWNER)       buf.writeI32(e.owner);
	if (fieldMask & FLD_MOVE_TARGET) {
		buf.writeI32(detail::quantPosMm(e.moveTarget.x));
		buf.writeI32(detail::quantPosMm(e.moveTarget.y));
		buf.writeI32(detail::quantPosMm(e.moveTarget.z));
	}
	if (fieldMask & FLD_MOVE_SPEED)  buf.writeF32(e.moveSpeed);
	if (fieldMask & FLD_LOOK_YAW)    buf.writeI16(detail::quantAngleCdeg(e.lookYaw));
	if (fieldMask & FLD_LOOK_PITCH)  buf.writeI16(detail::quantAngleCdeg(e.lookPitch));
	if (fieldMask & FLD_PROPS) {
		buf.writeU32((uint32_t)e.props.size());
		for (auto& [k, v] : e.props) { buf.writeString(k); buf.writeString(v); }
	}
}

// Header precedes the per-field payload. Exposed so client can look up its
// baseline EntityState by id before merging the delta into it.
struct EntityDeltaHeader { EntityId eid; uint32_t mask; };
inline EntityDeltaHeader readEntityDeltaHeader(ReadBuffer& buf) {
	EntityDeltaHeader h;
	h.eid = buf.readU32();
	h.mask = buf.readU32();
	return h;
}

// Merge field payload into `out`. Unset bits leave the corresponding field
// untouched, so callers must seed `out` from their per-entity cached last
// state before calling (empty EntityState for a newly-seen entity is fine
// since the server sends FLD_TYPE_ID on first broadcast).
inline void mergeEntityDeltaFields(ReadBuffer& buf, uint32_t mask, EntityState& out) {
	if (mask & FLD_TYPE_ID)     out.typeId = buf.readString();
	if (mask & FLD_POSITION) {
		int32_t x = buf.readI32(), y = buf.readI32(), z = buf.readI32();
		out.position = {detail::dequantPosMm(x), detail::dequantPosMm(y), detail::dequantPosMm(z)};
	}
	if (mask & FLD_VELOCITY) {
		int16_t x = buf.readI16(), y = buf.readI16(), z = buf.readI16();
		out.velocity = {detail::dequantVelCm(x), detail::dequantVelCm(y), detail::dequantVelCm(z)};
	}
	if (mask & FLD_YAW)         out.yaw = detail::dequantAngleCdeg(buf.readI16());
	if (mask & FLD_ON_GROUND)   out.onGround = buf.readBool();
	if (mask & FLD_GOAL_TEXT)   out.goalText = buf.readString();
	if (mask & FLD_CHAR_SKIN)   out.characterSkin = buf.readString();
	if (mask & FLD_HP)          out.hp = buf.readI32();
	if (mask & FLD_MAX_HP)      out.maxHp = buf.readI32();
	if (mask & FLD_OWNER)       out.owner = buf.readI32();
	if (mask & FLD_MOVE_TARGET) {
		int32_t x = buf.readI32(), y = buf.readI32(), z = buf.readI32();
		out.moveTarget = {detail::dequantPosMm(x), detail::dequantPosMm(y), detail::dequantPosMm(z)};
	}
	if (mask & FLD_MOVE_SPEED)  out.moveSpeed = buf.readF32();
	if (mask & FLD_LOOK_YAW)    out.lookYaw   = detail::dequantAngleCdeg(buf.readI16());
	if (mask & FLD_LOOK_PITCH)  out.lookPitch = detail::dequantAngleCdeg(buf.readI16());
	if (mask & FLD_PROPS) {
		out.props.clear();
		uint32_t n = buf.readU32();
		for (uint32_t i = 0; i < n && buf.hasMore(); i++) {
			std::string k = buf.readString();
			std::string v = buf.readString();
			out.props.push_back({k, v});
		}
	}
}

// ChunkInfo wire protocol removed — agents share PlayerClient's chunk cache

} // namespace civcraft::net
