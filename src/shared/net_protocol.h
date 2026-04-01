#pragma once

/**
 * Network protocol for client-server communication.
 *
 * Simple binary protocol over TCP:
 *   [4 bytes: message type] [4 bytes: payload length] [payload bytes]
 *
 * Client → Server:
 *   C_ACTION    — ActionProposal (movement, block interaction)
 *   C_SLOT      — Selected hotbar slot change
 *
 * Server → Client:
 *   S_WELCOME   — Your player EntityId + spawn position
 *   S_ENTITY    — Entity state update (position, velocity, props)
 *   S_CHUNK     — Chunk block data (16x16x16)
 *   S_REMOVE    — Entity removed
 *   S_TIME      — World time update
 */

#include "shared/entity.h"
#include "shared/action.h"
#include "shared/chunk.h"
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>

namespace agentworld::net {

enum MsgType : uint32_t {
	// Client → Server
	C_ACTION     = 0x0001,
	C_SLOT       = 0x0002,

	// Server → Client
	S_WELCOME    = 0x1001,
	S_ENTITY     = 0x1002,
	S_CHUNK      = 0x1003,
	S_REMOVE     = 0x1004,
	S_TIME       = 0x1005,
	S_BLOCK      = 0x1006,  // single block change
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
	void writeU32(uint32_t v) { write(&v, 4); }
	void writeI32(int32_t v) { write(&v, 4); }
	void writeF32(float v) { write(&v, 4); }
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

	uint32_t readU32() { uint32_t v; read(&v, 4); return v; }
	int32_t readI32() { int32_t v; read(&v, 4); return v; }
	float readF32() { float v; read(&v, 4); return v; }
	glm::vec3 readVec3() { float x = readF32(), y = readF32(), z = readF32(); return {x,y,z}; }
	glm::ivec3 readIVec3() { int x = readI32(), y = readI32(), z = readI32(); return {x,y,z}; }
	bool readBool() { uint8_t b; read(&b, 1); return b != 0; }
	std::string readString() {
		uint32_t len = readU32();
		std::string s((const char*)(m_data + m_pos), len);
		m_pos += len;
		return s;
	}

	bool hasMore() const { return m_pos < m_size; }

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

inline void serializeAction(WriteBuffer& buf, const ActionProposal& a) {
	buf.writeU32((uint32_t)a.type);
	buf.writeU32(a.actorId);
	buf.writeVec3(a.desiredVel);
	buf.writeBool(a.jump);
	buf.writeBool(a.fly);
	buf.writeF32(a.jumpVelocity);
	buf.writeIVec3(a.blockPos);
	buf.writeString(a.blockType);
	buf.writeI32(a.slotIndex);
	buf.writeU32(a.targetEntity);
	buf.writeF32(a.damage);
}

inline ActionProposal deserializeAction(ReadBuffer& buf) {
	ActionProposal a;
	a.type = (ActionProposal::Type)buf.readU32();
	a.actorId = buf.readU32();
	a.desiredVel = buf.readVec3();
	a.jump = buf.readBool();
	a.fly = buf.readBool();
	a.jumpVelocity = buf.readF32();
	a.blockPos = buf.readIVec3();
	a.blockType = buf.readString();
	a.slotIndex = buf.readI32();
	a.targetEntity = buf.readU32();
	a.damage = buf.readF32();
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
	int hp;
	int maxHp;
};

inline void serializeEntityState(WriteBuffer& buf, const EntityState& e) {
	buf.writeU32(e.id);
	buf.writeString(e.typeId);
	buf.writeVec3(e.position);
	buf.writeVec3(e.velocity);
	buf.writeF32(e.yaw);
	buf.writeBool(e.onGround);
	buf.writeString(e.goalText);
	buf.writeI32(e.hp);
	buf.writeI32(e.maxHp);
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
	e.hp = buf.readI32();
	e.maxHp = buf.readI32();
	return e;
}

} // namespace agentworld::net
