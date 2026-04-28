#pragma once

// String id ("base:humanoid") → const Rig* lookup. Populated from
// `<artifactsRoot>/rigs/<ns>/*.py` files via rig_loader.h.
//
// Lifetime: registry owns the Rig instances. Callers keep `const Rig*`
// borrows that are valid until the registry is rescanned or destroyed.
// loadAll() clears + repopulates atomically (build a new map, swap on
// success), so a concurrent reader holding a Rig* may briefly point at a
// reaped instance — callers needing strict aliveness during reload must
// drop their pointers first. Mirrors the existing model registry's
// hot-reload contract.

#include "client/rig.h"
#include "client/rig_loader.h"
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace solarium {

class RigRegistry {
public:
	// Scan `<artifactsRoot>/rigs/<ns>/*.py`. Returns the number of rigs
	// successfully loaded. Skips files that fail to parse but logs to stderr.
	int loadAll(const std::string& artifactsRoot = "artifacts") {
		namespace fs = std::filesystem;
		std::unordered_map<std::string, Rig> next;

		std::string root = artifactsRoot + "/rigs";
		std::error_code ec;
		if (!fs::exists(root, ec)) {
			m_rigs = std::move(next);
			return 0;
		}

		for (auto& nsEntry : fs::directory_iterator(root, ec)) {
			if (!nsEntry.is_directory()) continue;
			const std::string ns = nsEntry.path().filename().string();
			for (auto& fileEntry : fs::directory_iterator(nsEntry.path(), ec)) {
				if (!fileEntry.is_regular_file()) continue;
				if (fileEntry.path().extension() != ".py") continue;
				const std::string fname = fileEntry.path().filename().string();
				if (fname.starts_with("_")) continue;   // __init__.py etc.

				Rig r;
				if (!rig_loader::loadRigFile(fileEntry.path().string(), r)) {
					std::fprintf(stderr, "[rig] failed to load %s\n",
					             fileEntry.path().c_str());
					continue;
				}
				const std::string stem = fileEntry.path().stem().string();
				// Honour `id:` field if set; otherwise derive from filename.
				if (r.id.empty()) r.id = stem;
				const std::string key = ns + ":" + r.id;
				next.emplace(key, std::move(r));
			}
		}

		m_rigs = std::move(next);
		return (int)m_rigs.size();
	}

	// Lookup by fully-qualified id (e.g. "base:humanoid"). Returns nullptr if
	// not found. Borrowed pointer; see lifetime note above.
	const Rig* find(const std::string& fqId) const {
		auto it = m_rigs.find(fqId);
		return it == m_rigs.end() ? nullptr : &it->second;
	}

	// Diagnostic — list loaded rig ids in arbitrary order.
	std::vector<std::string> ids() const {
		std::vector<std::string> out;
		out.reserve(m_rigs.size());
		for (auto& [k, _] : m_rigs) out.push_back(k);
		return out;
	}

	size_t size() const { return m_rigs.size(); }

private:
	std::unordered_map<std::string, Rig> m_rigs;
};

} // namespace solarium
