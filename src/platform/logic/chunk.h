#pragma once

#include "logic/types.h"
#include "logic/block_registry.h"
#include "logic/chunk_default.h"
#include "logic/zone.h"
#include <array>
#include <memory>

namespace solarium {

// A Chunk is in one of two modes (see docs/22_APPEARANCE.md § Chunk compression):
//
//   Lite — every cell is the same {bid, appearance}, all param2 == 0. The
//          per-cell arrays are unallocated; storage is just the 3-byte tuple.
//   Full — divergent cells exist; per-cell arrays are heap-allocated (~16 KB).
//
// All chunks start Lite (AIR + appearance 0). The first divergent write
// (via set or setAppearance) triggers hydrate(). classify() can demote a
// homogeneous Full chunk back to Lite (caller-driven; no auto-classify on
// read). I9: read paths NEVER hydrate.
class Chunk {
public:
	enum class Mode : uint8_t { Lite, Full };

	Chunk() = default;

	BlockId get(int x, int y, int z) const {
		if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_SIZE || z < 0 || z >= CHUNK_SIZE)
			return BLOCK_AIR;
		if (!m_blocks) return m_liteBid;
		return (*m_blocks)[index(x, y, z)];
	}

	// param2: rotation data. FourDir bits 0-1 = facing: 0=+Z, 1=+X, 2=-Z, 3=-X.
	uint8_t getParam2(int x, int y, int z) const {
		if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_SIZE || z < 0 || z >= CHUNK_SIZE)
			return 0;
		if (!m_param2) return 0;
		return (*m_param2)[index(x, y, z)];
	}

	// Appearance: index into BlockDef::appearance_palette. 0 = default variant.
	uint8_t getAppearance(int x, int y, int z) const {
		if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_SIZE || z < 0 || z >= CHUNK_SIZE)
			return 0;
		if (!m_appearance) return m_liteApp;
		return (*m_appearance)[index(x, y, z)];
	}

	void set(int x, int y, int z, BlockId type, uint8_t p2 = 0) {
		if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_SIZE || z < 0 || z >= CHUNK_SIZE)
			return;
		if (!m_blocks) {
			// Lite — divergent? hydrate first. Same {bid,p2,app=0} preserves Lite.
			if (type == m_liteBid && p2 == 0) {
				// appearance reset to 0 happens in Full mode below; keep Lite if app already 0.
				if (m_liteApp == 0) return;  // no change
				// Otherwise we need Full to express per-cell appearance reset.
				hydrate();
			} else {
				hydrate();
			}
		}
		int i = index(x, y, z);
		(*m_blocks)[i] = type;
		(*m_param2)[i] = p2;
		(*m_appearance)[i] = 0;  // default variant on type change
		m_dirty = true;
	}

	void setAppearance(int x, int y, int z, uint8_t idx) {
		if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_SIZE || z < 0 || z >= CHUNK_SIZE)
			return;
		if (!m_appearance) {
			if (idx == m_liteApp) return;  // no change, stay Lite
			hydrate();
		}
		(*m_appearance)[index(x, y, z)] = idx;
		m_dirty = true;
	}

	bool isDirty() const { return m_dirty; }
	void clearDirty() { m_dirty = false; }

	// Zone — gameplay tag for the whole chunk (Wilderness, City, Water, …).
	// Set once by the WorldTemplate at generate time, sent once on the wire,
	// never updated at runtime (see logic/zone.h). Independent of Lite/Full
	// chunk classification — homogeneous chunks still carry a zone byte.
	Zone zone() const { return m_zone; }
	void setZone(Zone z) { m_zone = z; }

	// --- Mode introspection (for save/load, net, mesher cull) ---
	Mode mode() const { return m_blocks ? Mode::Full : Mode::Lite; }
	bool isLite() const { return !m_blocks; }
	BlockId liteBid() const { return m_liteBid; }
	uint8_t liteAppearance() const { return m_liteApp; }

	// "Default" = matches the engine policy for chunks at this y (AIR above
	// world-y 0, DIRT below; see logic/chunk_default.h). The server skips
	// streaming default chunks; clients fall back to the same policy on
	// lookup miss, so the wire saves ~75% of S_CHUNKs in voxel-earth bakes
	// (sky chunks above bbox top, underground below floor).
	bool isDefault(int cy, const DefaultFill& d) const {
		if (!isLite())              return false;
		if (m_liteApp != 0)         return false;
		if (m_zone != Zone::Unknown) return false;
		return m_liteBid == d.forChunkY(cy);
	}

	// Force Lite mode with a uniform fill. Drops any allocated arrays.
	// Used by deserialization when the wire/disk format declares Lite.
	void resetLite(BlockId bid, uint8_t app = 0) {
		m_blocks.reset();
		m_param2.reset();
		m_appearance.reset();
		m_liteBid = bid;
		m_liteApp = app;
		m_dirty = true;
	}

	// Force Full mode, allocating arrays if needed and seeding from current
	// state (Lite tuple replicated 4096×, or kept if already Full). Used by
	// deserialization before bulk setRaw fills, and as the I7 sole hydrator.
	void hydrate() {
		if (m_blocks) return;
		m_blocks     = std::make_unique<std::array<BlockId, CHUNK_VOLUME>>();
		m_param2     = std::make_unique<std::array<uint8_t, CHUNK_VOLUME>>();
		m_appearance = std::make_unique<std::array<uint8_t, CHUNK_VOLUME>>();
		m_blocks->fill(m_liteBid);
		m_param2->fill(0);
		m_appearance->fill(m_liteApp);
		m_dirty = true;
	}

	// Scan all cells; if homogeneous, demote to Lite. Caller-driven (e.g. after
	// a worldgen pass or after a Full chunk's last divergent cell is reverted).
	// Returns true if the chunk is Lite after this call.
	bool classify() {
		if (!m_blocks) return true;  // already Lite
		BlockId bid0 = (*m_blocks)[0];
		uint8_t app0 = (*m_appearance)[0];
		for (int i = 0; i < CHUNK_VOLUME; ++i) {
			if ((*m_blocks)[i] != bid0) return false;
			if ((*m_param2)[i] != 0) return false;
			if ((*m_appearance)[i] != app0) return false;
		}
		resetLite(bid0, app0);
		return true;
	}

	// --- Raw per-cell access (mode-transparent reads).
	// Reads return the Lite tuple if Lite; writes hydrate first to express the
	// per-cell value. Used by save/load and S_CHUNK serialization paths.
	BlockId getRaw(int i) const { return m_blocks ? (*m_blocks)[i] : m_liteBid; }
	void setRaw(int i, BlockId v) {
		if (!m_blocks) {
			if (v == m_liteBid) return;
			hydrate();
		}
		(*m_blocks)[i] = v;
		m_dirty = true;
	}
	uint8_t getRawParam2(int i) const { return m_param2 ? (*m_param2)[i] : 0; }
	void setRawParam2(int i, uint8_t v) {
		if (!m_param2) {
			if (v == 0) return;
			hydrate();
		}
		(*m_param2)[i] = v;
	}
	uint8_t getRawAppearance(int i) const { return m_appearance ? (*m_appearance)[i] : m_liteApp; }
	void setRawAppearance(int i, uint8_t v) {
		if (!m_appearance) {
			if (v == m_liteApp) return;
			hydrate();
		}
		(*m_appearance)[i] = v;
	}

	// Bulk fill from a saved Full chunk. Hydrates if needed.
	void setRawBlocks(const std::array<BlockId, CHUNK_VOLUME>& data) {
		hydrate(); *m_blocks = data; m_dirty = true;
	}
	void setRawParam2Array(const std::array<uint8_t, CHUNK_VOLUME>& data) {
		hydrate(); *m_param2 = data;
	}
	void setRawAppearanceArray(const std::array<uint8_t, CHUNK_VOLUME>& data) {
		hydrate(); *m_appearance = data;
	}

private:
	static int index(int x, int y, int z) {
		return y * CHUNK_SIZE * CHUNK_SIZE + z * CHUNK_SIZE + x;
	}

	// Lite tuple — also valid in Full mode but ignored.
	BlockId m_liteBid = BLOCK_AIR;
	uint8_t m_liteApp = 0;

	// Chunk-level zone tag; constant after WorldTemplate::generate().
	Zone m_zone = Zone::Unknown;

	// Full mode storage; nullptr ⇒ Lite.
	std::unique_ptr<std::array<BlockId, CHUNK_VOLUME>> m_blocks;
	std::unique_ptr<std::array<uint8_t, CHUNK_VOLUME>> m_param2;
	std::unique_ptr<std::array<uint8_t, CHUNK_VOLUME>> m_appearance;

	bool m_dirty = true;
};

} // namespace solarium
