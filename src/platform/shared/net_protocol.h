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
 * S_REMOVE        0x1004  [u32 entityId]
 * S_TIME          0x1005  [f32 worldTime]
 * S_BLOCK         0x1006  [i32 x][i32 y][i32 z][u32 blockId][u8 param2]
 * S_INVENTORY     0x1007  [u32 eid][u32 n][str×n id][i32×n count][u8 equipN][str×equipN slot][str×equipN id]
 * S_ERROR         0x100B  [u32 entityId][str message]
 * S_CHUNK_EVICT   0x100E  Discard chunk [i32 cx][i32 cy][i32 cz]  (v2)
 * S_CHUNK_Z       0x100F  zstd-compressed chunk (v2)
 * S_NPC_INTERRUPT 0x1010  [u32 eid][str reason]                  (v3, TODO(decide-loop))
 * S_WORLD_EVENT   0x1011  [str kind][str payload]                (v3, TODO(decide-loop))
 */

#include "shared/entity.h"
#include "shared/action.h"
#include "shared/chunk.h"
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>

namespace modcraft::net {

// Protocol version sent in C_HELLO. Server uses this to pick S_CHUNK vs S_CHUNK_Z.
// Increment this whenever the wire format changes.
static constexpr uint32_t PROTOCOL_VERSION = 2;

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

	// Server → Client
	S_WELCOME         = 0x1001,
	S_ENTITY          = 0x1002,
	S_CHUNK           = 0x1003,  // uncompressed chunk (v1 clients or fallback)
	S_REMOVE          = 0x1004,
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
};

// Message header (8 bytes)
struct MsgHeader {
	uint32_t type;
	uint32_t length;  // payload length (excluding header)
};

// ================================================================
// Serialization helpers — simple memcpy-based binary encoding
// ================================================================

class WriteBuffer {
public:
	void writeU8(uint8_t v)   { write(&v, 1); }
	void writeU16(uint16_t v) { write(&v, 2); }
	void writeU32(uint32_t v) { write(&v, 4); }
	void writeI32(int32_t v)  { write(&v, 4); }
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
	uint32_t readU32() { uint32_t v; read(&v, 4); return v; }
	int32_t  readI32() { int32_t  v; read(&v, 4); return v; }
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

	// Raw pointer to unread bytes — for passing to external decoders (e.g. zstd).
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

// ================================================================
// Serialize/deserialize ActionProposal
// ================================================================

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
	// Interact
	buf.writeIVec3(a.blockPos);
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
	// Interact
	if (!buf.hasMore()) return a;
	a.blockPos = buf.readIVec3();
	// Hot-reload side-channel
	if (!buf.hasMore()) return a;
	a.behaviorSource = buf.readString();
	return a;
}

// ================================================================
// Serialize entity state update (server → client)
// ================================================================

struct EntityState {
	EntityId id;
	std::string typeId;
	glm::vec3 position;
	glm::vec3 velocity;
	float yaw;
	bool onGround;
	std::string goalText;
	std::string characterSkin; // visual override (e.g., "base:knight")
	int hp;
	int maxHp;
	int owner = 0;  // EntityId of owning player (0 = unowned)
	glm::vec3 moveTarget = {0, 0, 0};  // where entity is heading (client prediction)
	float moveSpeed = 0.0f;            // speed toward moveTarget
	float lookYaw   = 0.0f;           // head look yaw (degrees, for remote head tracking)
	float lookPitch = 0.0f;           // head look pitch (degrees)
	// All properties — serialized as string key-value pairs.
	// Float/int/bool/vec3 props are converted to strings for transport.
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

// ChunkInfo wire protocol removed — agents share PlayerClient's chunk cache

} // namespace modcraft::net
