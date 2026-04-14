#include "CellCraft/client/game_log.h"

#include <ctime>

namespace civcraft::cellcraft {

void GameLog::open(const char* path) {
	close();
	f_ = std::fopen(path, "w");
}

void GameLog::close() {
	if (f_) { std::fclose(f_); f_ = nullptr; }
}

void GameLog::write(const char* category, const std::string& text) {
	if (!f_) return;
	std::time_t now = std::time(nullptr);
	std::tm tm{};
	localtime_r(&now, &tm);
	std::fprintf(f_, "[%02d:%02d:%02d] [%s] %s\n",
		tm.tm_hour, tm.tm_min, tm.tm_sec, category, text.c_str());
	std::fflush(f_);
}

} // namespace civcraft::cellcraft
