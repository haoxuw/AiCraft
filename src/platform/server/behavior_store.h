#pragma once

// Read-only loader for artifacts/behaviors/base/*.py. Consumed by AgentClient
// to resolve behavior-module source by name. User/mod behaviors will come
// from a database layer (not yet implemented) — there is no on-disk writable
// tier.

#include <string>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace civcraft {

class BehaviorStore {
public:
	void init(const std::string& basePath = "artifacts/behaviors") {
		m_basePath = basePath;
		m_initialized = true;
	}

	bool isInitialized() const { return m_initialized; }

	std::string load(const std::string& name) {
		std::string path = m_basePath + "/base/" + name + ".py";
		std::ifstream f(path);
		if (!f.is_open()) return "";
		std::ostringstream ss;
		ss << f.rdbuf();
		return ss.str();
	}

private:
	bool m_initialized = false;
	std::string m_basePath;
};

} // namespace civcraft
