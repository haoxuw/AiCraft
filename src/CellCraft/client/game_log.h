// CellCraft — event log writer. Derives everything from sim events so
// the server stays display-free (Rule 5 analogue). Writes to
// /tmp/cellcraft_game.log, truncated on construction.
#pragma once

#include <cstdio>
#include <string>

namespace civcraft::cellcraft {

class GameLog {
public:
	void open(const char* path = "/tmp/cellcraft_game.log");
	void close();
	void write(const char* category, const std::string& text);
private:
	FILE* f_ = nullptr;
};

} // namespace civcraft::cellcraft
