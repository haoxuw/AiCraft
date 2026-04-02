BUILD_DIR := build
BUILD_WEB := build-web
BUILD_TYPE := Debug
HOST := 127.0.0.1
PORT := 7777
WEB_PORT := 8080
EMSDK := $(HOME)/emsdk

.PHONY: game play build configure clean server client stop web web-build web-configure web-clean

# ── Native (Linux) ─────────────────────────────────────────

# Singleplayer — server + client in one process (no networking)
game: build
	@-pkill -x "agentworld" 2>/dev/null; sleep 0.5
	./$(BUILD_DIR)/agentworld

# Quick LAN test — starts server in background + client in foreground
# Usage: make play              (port 7777)
#        make play PORT=7890    (custom port)
play: build
	@-pkill -x "agentworld-server" 2>/dev/null; sleep 0.3
	@echo "[make] Starting server on port $(PORT)..."
	@./$(BUILD_DIR)/agentworld-server --port $(PORT) --template 1 --seed 42 &
	@sleep 1
	@echo "[make] Starting client → 127.0.0.1:$(PORT)..."
	./$(BUILD_DIR)/agentworld-client --host 127.0.0.1 --port $(PORT)
	@echo "[make] Client exited, stopping server..."
	@-pkill -x "agentworld-server" 2>/dev/null

# Dedicated server — headless, accepts multiple clients
# Usage: make server             (port 7777, interactive world select)
#        make server PORT=7890   (custom port)
server: build
	@-pkill -x "agentworld-server" 2>/dev/null; sleep 0.5
	./$(BUILD_DIR)/agentworld-server --port $(PORT)

# Network client — connects to a running server
# Usage: make client                              (localhost:7777)
#        make client HOST=192.168.1.5 PORT=7890   (custom host/port)
client: build
	./$(BUILD_DIR)/agentworld-client --host $(HOST) --port $(PORT)

# Kill all agentworld processes
stop:
	@-pkill -f "agentworld" 2>/dev/null; sleep 1
	@echo "All agentworld processes stopped."

build: configure
	cmake --build $(BUILD_DIR) -j$$(nproc)

configure:
	@if [ ! -f $(BUILD_DIR)/CMakeCache.txt ]; then \
		cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE); \
	fi

clean:
	rm -rf $(BUILD_DIR)

# ── Web (WASM + WebGL) ────────────────────────────────────

web: web-build
	python3 tools/serve_web.py $(WEB_PORT) $(BUILD_WEB)

web-build: web-configure
	cmake --build $(BUILD_WEB) -j$$(nproc)

web-configure:
	@if [ ! -f $(BUILD_WEB)/CMakeCache.txt ]; then \
		. $(EMSDK)/emsdk_env.sh 2>/dev/null && \
		emcmake cmake -B $(BUILD_WEB) -DCMAKE_BUILD_TYPE=Release; \
	fi

web-clean:
	rm -rf $(BUILD_WEB)
