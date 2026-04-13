SHELL := /bin/bash
BUILD_DIR := build
GAME_BUILD_DIR := build-perf
BUILD_WEB := build-web
BUILD_TYPE := Debug
HOST :=
PORT := 7777
GAME_PORT :=
WEB_PORT := 8080
EMSDK := $(HOME)/emsdk
GAME := modcraft

.PHONY: game game-build game-configure build configure clean server client stop test_e2e web web-build web-configure web-clean proxy test-dog test-villager profiler killservers

# ── Native ─────────────────────────────────────────────────
#
# This repo builds two games from one C++ engine (src/platform):
#   modcraft       voxel sandbox (default)
#   evolvecraft    Spore-like cell stage (stub)
#
# Override the game with GAME=evolvecraft for future EvolveCraft targets.
#
# Quick reference:
#   make game                 Singleplayer (ModCraft): new village, random port, skip menu
#   make game GAME_PORT=7890  Same, but on a fixed port
#   make server               Dedicated server (interactive world select)
#   make server PORT=N        Dedicated server on port N
#   make client               Open GUI → "Start game" or "Join a game" from menu
#   make client HOST=X PORT=N Open GUI with server pre-filled
#   make test_e2e             Headless end-to-end gameplay tests
#   make stop                 Kill all game processes
#   make proxy                HTTP→TCP action proxy with Swagger UI on :8088
#
# Multiplayer (separate terminals):
#   Terminal 1: make server PORT=7777
#   Terminal 2: make client HOST=127.0.0.1 PORT=7777
#

# Singleplayer: auto-launches server + bot processes + GUI client.
# Assets are staged into $(BUILD_DIR) by CMake POST_BUILD, so we cd there
# so "shaders/", "artifacts/", "python/", "fonts/", "config/", "resources/"
# resolve via CWD-relative paths at runtime.
#
# EvolveCraft is a single-process binary (no server/agents yet); it accepts a
# different flag set, so dispatch per GAME.
ifeq ($(GAME),evolvecraft)
game: game-build
	cd $(GAME_BUILD_DIR) && ./evolvecraft --template 1
else
# `make game` builds with MODCRAFT_PERF=ON in a separate build dir so the
# server emits frame/tick/handler timing logs (see [Perf] lines on stderr and
# /tmp/modcraft_log_*.log). Production targets (`make server`, `make client`)
# use the default build dir without this flag — the instrumentation code is
# not compiled in.
game: game-build
	cd $(GAME_BUILD_DIR) && ./$(GAME) --skip-menu$(if $(GAME_PORT), --port $(GAME_PORT),)
endif

profiler: game-build
	cd $(GAME_BUILD_DIR) && ./$(GAME) --skip-menu --profiler$(if $(GAME_PORT), --port $(GAME_PORT),)

# Minimal isolation worlds for focused behavior testing.
test-dog: game-build
	cd $(GAME_BUILD_DIR) && ./$(GAME) --skip-menu --template 3

test-villager: game-build
	cd $(GAME_BUILD_DIR) && ./$(GAME) --skip-menu --template 4

# Dedicated server (interactive world select, or --world/--seed/--template flags)
server: build
	cd $(BUILD_DIR) && ./$(GAME)-server --port $(PORT)

# GUI client: shows menu with "Start game" and "Join a game" tabs
client: build
	cd $(BUILD_DIR) && ./$(GAME)$(if $(HOST), --host $(HOST) --port $(PORT),)

stop:
	@-pkill -f "$(GAME)" 2>/dev/null; sleep 1
	@echo "All $(GAME) processes stopped."

killservers:
	@echo "Looking for $(GAME) server processes..."
	@-pgrep -fa "$(GAME)-server" 2>/dev/null && pkill -f "$(GAME)-server" && echo "Killed." || echo "No servers running."
	@-pgrep -fa "$(GAME)".*--port" 2>/dev/null && pkill -f "$(GAME)".*--port" && echo "Killed port processes." || true

# Headless E2E gameplay tests
ifeq ($(GAME),evolvecraft)
test_e2e: build
	@echo "[test_e2e] Running EvolveCraft headless simulation..."
	cd $(BUILD_DIR) && ./evolvecraft --headless --template 1 --seconds 10
	@echo "[test_e2e] Done."
else
test_e2e: build
	@echo "[test_e2e] Running headless gameplay tests..."
	cd $(BUILD_DIR) && ./$(GAME)-test
	@echo "[test_e2e] Done."
endif

build: configure
	cmake --build $(BUILD_DIR) -j$$(nproc)

configure:
	@if [ ! -f $(BUILD_DIR)/CMakeCache.txt ]; then \
		cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE); \
	fi

# Perf-instrumented build for `make game` (and other --skip-menu test targets).
# Lives in its own dir so toggling between game/server/client doesn't trigger
# a full reconfigure each time.
game-build: game-configure
	cmake --build $(GAME_BUILD_DIR) -j$$(nproc)

game-configure:
	@if [ ! -f $(GAME_BUILD_DIR)/CMakeCache.txt ]; then \
		cmake -B $(GAME_BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DMODCRAFT_PERF=ON; \
	fi

clean:
	rm -rf $(BUILD_DIR) $(GAME_BUILD_DIR)

# Action proxy: HTTP → TCP bridge with Swagger UI
PROXY_PORT := 8088
proxy:
	python3 src/ModCraft/tools/action_proxy.py --server-host 127.0.0.1 --server-port $(PORT) \
	    --proxy-port $(PROXY_PORT) --auto-connect

# ── Web (WASM + WebGL) ────────────────────────────────────

web: web-build
	python3 tools/platform/serve_web.py $(WEB_PORT) $(BUILD_WEB)

web-build: web-configure
	cmake --build $(BUILD_WEB) -j$$(nproc)

web-configure:
	@if [ ! -f $(BUILD_WEB)/CMakeCache.txt ]; then \
		. $(EMSDK)/emsdk_env.sh 2>/dev/null && \
		emcmake cmake -B $(BUILD_WEB) -DCMAKE_BUILD_TYPE=Release; \
	fi

web-clean:
	rm -rf $(BUILD_WEB)
