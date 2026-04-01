BUILD_DIR := build
BUILD_TYPE := Debug
HOST := 127.0.0.1
PORT := 7777

.PHONY: build configure clean server client game

# Play the game (full experience: menu, characters, world)
# Runs server + client in one process (singleplayer)
game: build
	./$(BUILD_DIR)/aicraft

# Dedicated server (headless, for hosting multiplayer)
server: build
	./$(BUILD_DIR)/aicraft-server --port $(PORT)

# Multiplayer client (connects to a running server)
# Server must be started first with 'make server'
client: build
	./$(BUILD_DIR)/aicraft-client --host $(HOST) --port $(PORT)

build: configure
	cmake --build $(BUILD_DIR) -j$$(nproc)

configure:
	@if [ ! -f $(BUILD_DIR)/CMakeCache.txt ]; then \
		cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE); \
	fi

clean:
	rm -rf $(BUILD_DIR)
