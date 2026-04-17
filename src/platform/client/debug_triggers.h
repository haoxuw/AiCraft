#pragma once
// DebugTriggers — file-based remote control for headless testing.
//
// Each trigger is a sentinel file under /tmp/. When poll() detects the file,
// it removes it and fires the associated callback. Triggers that carry a
// payload (e.g. coordinates) read the file content before removal.
//
// Compiled out in Release builds (NDEBUG) so production frames pay zero cost
// for filesystem polling. The class is still defined (empty poll()) so call
// sites don't need #ifdefs.

#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace civcraft {

class DebugTriggers {
public:
	using Callback     = std::function<void()>;
	using PayloadCB    = std::function<void(const std::string&)>;

	void addTrigger(const char* path, Callback cb) {
		m_triggers.push_back({path, std::move(cb), nullptr});
	}

	void addPayloadTrigger(const char* path, PayloadCB cb) {
		m_triggers.push_back({path, nullptr, std::move(cb)});
	}

	void poll() {
#ifndef NDEBUG
		for (auto& t : m_triggers) {
			if (!std::filesystem::exists(t.path)) continue;
			if (t.payloadCb) {
				std::string payload;
				{ std::ifstream f(t.path); std::getline(f, payload); }
				std::filesystem::remove(t.path);
				t.payloadCb(payload);
			} else {
				std::filesystem::remove(t.path);
				if (t.cb) t.cb();
			}
		}
#endif
	}

private:
	struct Entry {
		std::string path;
		Callback    cb;
		PayloadCB   payloadCb;
	};
	std::vector<Entry> m_triggers;
};

} // namespace civcraft
