SHELL := /bin/bash
BUILD_DIR := build
BUILD_WEB := build-web
BUILD_TYPE := Debug
HOST :=
PORT := 7777
GAME_PORT :=
WEB_PORT := 8080
EMSDK := $(HOME)/emsdk

.PHONY: game build configure clean server client stop test_e2e web web-build web-configure web-clean proxy

# ── Native ─────────────────────────────────────────────────
#
# Quick reference:
#   make game                 Singleplayer: new village world, random port, skip menu
#   make game GAME_PORT=7890  Same, but on a fixed port
#   make server               Dedicated server (interactive world select)
#   make server PORT=N        Dedicated server on port N
#   make client               Open GUI → "Start game" or "Join a game" from menu
#   make client HOST=X PORT=N Open GUI with server pre-filled in "Join a game" tab
#   make stop                 Kill all modcraft processes
#   make proxy                HTTP→TCP action proxy with Swagger UI on :8088
#
# Multiplayer (separate terminals):
#   Terminal 1: make server PORT=7777
#   Terminal 2: make client HOST=127.0.0.1 PORT=7777
#   Terminal 3: make client HOST=127.0.0.1 PORT=7777   (second player)
#

# Singleplayer: auto-launches server + bot processes + GUI client
# Uses AgentManager internally — no manual bot spawning needed
game: build
	./$(BUILD_DIR)/modcraft --skip-menu$(if $(GAME_PORT), --port $(GAME_PORT),)

# Dedicated server (interactive world select, or --world/--seed/--template flags)
server: build
	./$(BUILD_DIR)/modcraft-server --port $(PORT)

# GUI client: shows menu with "Start game" and "Join a game" tabs
# Optionally pre-fills server address: make client HOST=192.168.1.5 PORT=7777
client: build
	./$(BUILD_DIR)/modcraft$(if $(HOST), --host $(HOST) --port $(PORT),)

# Kill everything
stop:
	@-pkill -f "modcraft"" 2>/dev/null; sleep 1
	@echo "All modcraft processes stopped."

# Kill only servers (find by listening port)
killservers:
	@echo "Looking for modcraft server processes..."
	@-pgrep -fa "modcraft-server" 2>/dev/null && pkill -f "modcraft-server" && echo "Killed." || echo "No servers running."
	@-pgrep -fa "modcraft".*--port" 2>/dev/null && pkill -f "modcraft".*--port" && echo "Killed port processes." || true

# Run headless E2E gameplay tests
test_e2e: build
	@echo "[test_e2e] Running headless gameplay tests..."
	cd $(BUILD_DIR) && ./modcraft-test
	@echo "[test_e2e] Done."

build: configure
	cmake --build $(BUILD_DIR) -j$$(nproc)

configure:
	@if [ ! -f $(BUILD_DIR)/CMakeCache.txt ]; then \
		cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE); \
	fi

clean:
	rm -rf $(BUILD_DIR)

# Action proxy: HTTP → TCP bridge with Swagger UI
# Usage: make proxy  (connects automatically if server is already running)
# Then open: http://localhost:8088/docs
PROXY_PORT := 8088
proxy:
	python3 tools/action_proxy.py --server-host 127.0.0.1 --server-port $(PORT) \
	    --proxy-port $(PROXY_PORT) --auto-connect

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
