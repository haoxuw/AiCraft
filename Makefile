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
GAME := civcraft

# Parallelism for cmake --build. Defaults to HALF the core count so a build
# doesn't pin the machine / trigger OOM on big templated sources. Override on
# the command line, e.g. `make build PAR=8` or `make build PAR=1`.
PAR := $(shell nproc 2>/dev/null | awk '{n=int($$1/2); print (n<1)?1:n}')

.PHONY: game game-build game-configure build configure clean server client stop test_e2e web web-build web-configure web-clean proxy test-dog test-villager profiler killservers lifecraft-server-build lifecraft-server-game-build character_views item_views model-editor model-snap

# ── Native ─────────────────────────────────────────────────
#
# This repo builds two games from one C++ engine (src/platform):
#   civcraft       voxel sandbox (default) — C++ server + C++/OpenGL client
#   lifecraft       Spore cell stage       — C++ server + Godot 4 client
#
# Override the game with GAME=lifecraft for LifeCraft targets.
#
# Quick reference:
#   make game                 Singleplayer (CivCraft): new village, random port, skip menu
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
# LifeCraft launches a C++ TCP server + a Godot 4 client talking to it over
# localhost. The server is lifecraft-server (built by CMake); the client is
# Godot project src/LifeCraft/godot/ launched via `godot4`.
LIFECRAFT_PORT := 7888
ifeq ($(GAME),lifecraft)
lifecraft-server-game-build: game-configure
	cmake --build $(GAME_BUILD_DIR) --target lifecraft-server -j$(PAR)

game: lifecraft-server-game-build
	@echo "[lifecraft] starting server on :$(LIFECRAFT_PORT), then Godot client..."
	@pkill -x lifecraft-server 2>/dev/null ; sleep 0.2 ; \
	  $(CURDIR)/$(GAME_BUILD_DIR)/lifecraft-server --port $(LIFECRAFT_PORT) & \
	  SERVER_PID=$$! ; \
	  sleep 0.3 ; \
	  godot4 --path $(CURDIR)/src/LifeCraft/godot -- --host 127.0.0.1 --port $(LIFECRAFT_PORT) ; \
	  kill $$SERVER_PID 2>/dev/null ; wait 2>/dev/null || true
else
# `make game` builds with CIVCRAFT_PERF=ON in a separate build dir so the
# server emits frame/tick/handler timing logs (see [Perf] lines on stderr and
# /tmp/civcraft_log_*.log). Production targets (`make server`, `make client`)
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

# ── Visual QA scenarios ─────────────────────────────────────
#
# character_views: orbit the RPG camera around a character model and write
# 6 screenshots (front/three_q/side/back/top/rts) to /tmp/debug_N_*.ppm.
# Use to calibrate mob/player models — silhouette, proportions, palette.
#
#   make character_views                      # default: base:pig
#   make character_views CHARACTER=base:cat
#   make character_views CHARACTER=base:skeleton
#
# item_views: same idea for held items (FPS/TPS/RPG/RTS + on-ground).
#
#   make item_views                           # default: base:sword
#   make item_views ITEM=base:wood_axe
CHARACTER := base:pig
ITEM      := base:sword
CLIP      :=

character_views: build
	cd $(BUILD_DIR) && ./$(GAME) --skip-menu \
	    --debug-scenario character_views --debug-character $(CHARACTER) \
	    $(if $(CLIP),--debug-clip $(CLIP))

item_views: build
	cd $(BUILD_DIR) && ./$(GAME) --skip-menu \
	    --debug-scenario item_views --debug-item $(ITEM)

# Standalone model viewer / snapshot tool (no world, no server, no full client).
# Shared by CivCraft + LifeCraft.
#
#   make model-editor MODEL=src/CivCraft/artifacts/models/base/cat.py
#       Interactive window (drag to orbit, scroll to zoom, Esc to quit).
#
#   make model-snap   MODEL=src/CivCraft/artifacts/models/base/cat.py OUT=/tmp/cat
#       Write 6 PPMs (front/three_q/side/back/top/rts) to OUT and exit.
#       Optional: CLIP=dance, SIZE=512x512
MODEL ?=
OUT   ?= /tmp/model_snap
SIZE  ?= 512x512
CLIP  ?=

model-editor-build: configure
	cmake --build $(BUILD_DIR) --target model-editor -j$(PAR)

model-editor: model-editor-build
	@[ -n "$(MODEL)" ] || (echo "usage: make model-editor MODEL=path/to/model.py" >&2 && exit 1)
	cd $(BUILD_DIR) && ./model-editor $(CURDIR)/$(MODEL) --size $(SIZE) $(if $(CLIP),--clip $(CLIP))

model-snap: model-editor-build
	@[ -n "$(MODEL)" ] || (echo "usage: make model-snap MODEL=path/to/model.py [OUT=dir]" >&2 && exit 1)
	cd $(BUILD_DIR) && ./model-editor $(CURDIR)/$(MODEL) \
	    --snapshot $(OUT) --size $(SIZE) $(if $(CLIP),--clip $(CLIP))

# Dedicated server (interactive world select, or --world/--seed/--template flags)
ifeq ($(GAME),lifecraft)
server: lifecraft-server-build
	cd $(BUILD_DIR) && ./lifecraft-server --port $(if $(PORT),$(PORT),$(LIFECRAFT_PORT))

client:
	godot4 --path $(CURDIR)/src/LifeCraft/godot -- \
	  --host $(if $(HOST),$(HOST),127.0.0.1) --port $(if $(PORT),$(PORT),$(LIFECRAFT_PORT))
else
server: build
	cd $(BUILD_DIR) && ./$(GAME)-server --port $(PORT)

# GUI client: shows menu with "Start game" and "Join a game" tabs
client: build
	cd $(BUILD_DIR) && ./$(GAME)$(if $(HOST), --host $(HOST) --port $(PORT),)
endif

stop:
ifeq ($(GAME),lifecraft)
	@-pkill -x lifecraft-server 2>/dev/null; \
	  pkill -f "godot4.*src/LifeCraft/godot" 2>/dev/null; \
	  sleep 0.5; true
	@echo "All lifecraft processes stopped."
else
	@-pkill -f "$(GAME)" 2>/dev/null; sleep 1
	@echo "All $(GAME) processes stopped."
endif

killservers:
	@echo "Looking for $(GAME) server processes..."
	@-pgrep -fa "$(GAME)-server" 2>/dev/null && pkill -f "$(GAME)-server" && echo "Killed." || echo "No servers running."
	@-pgrep -fa "$(GAME)".*--port" 2>/dev/null && pkill -f "$(GAME)".*--port" && echo "Killed port processes." || true

# Headless E2E gameplay tests
ifeq ($(GAME),lifecraft)
# LifeCraft doesn't need the full civcraft build — only lifecraft-server.
lifecraft-server-build: configure
	cmake --build $(BUILD_DIR) --target lifecraft-server -j$(PAR)

test_e2e: lifecraft-server-build
	@echo "[test_e2e] LifeCraft M1 — server tick broadcast handshake..."
	@pkill -x lifecraft-server 2>/dev/null ; sleep 0.2 ; \
	  $(CURDIR)/$(BUILD_DIR)/lifecraft-server --port $(LIFECRAFT_PORT) & \
	  SERVER_PID=$$! ; \
	  sleep 0.3 ; \
	  godot4 --headless --path $(CURDIR)/src/LifeCraft/godot -- \
	    --host 127.0.0.1 --port $(LIFECRAFT_PORT) --ticks 3 ; \
	  STATUS=$$? ; \
	  kill $$SERVER_PID 2>/dev/null ; wait 2>/dev/null ; \
	  exit $$STATUS
	@echo "[test_e2e] Done."
else
test_e2e: build
	@echo "[test_e2e] Running headless gameplay tests..."
	cd $(BUILD_DIR) && ./$(GAME)-test
	@echo "[test_e2e] Done."
endif

build: configure
	cmake --build $(BUILD_DIR) -j$(PAR)

configure:
	@if [ ! -f $(BUILD_DIR)/CMakeCache.txt ]; then \
		cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE); \
	fi

# Perf-instrumented build for `make game` (and other --skip-menu test targets).
# Lives in its own dir so toggling between game/server/client doesn't trigger
# a full reconfigure each time.
game-build: game-configure
	cmake --build $(GAME_BUILD_DIR) -j$(PAR)

game-configure:
	@if [ ! -f $(GAME_BUILD_DIR)/CMakeCache.txt ]; then \
		cmake -B $(GAME_BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DCIVCRAFT_PERF=ON; \
	fi

clean:
	rm -rf $(BUILD_DIR) $(GAME_BUILD_DIR)

# Action proxy: HTTP → TCP bridge with Swagger UI
PROXY_PORT := 8088
proxy:
	python3 src/CivCraft/tools/action_proxy.py --server-host 127.0.0.1 --server-port $(PORT) \
	    --proxy-port $(PROXY_PORT) --auto-connect

# ── Web (WASM + WebGL) ────────────────────────────────────

web: web-build
	python3 tools/platform/serve_web.py $(WEB_PORT) $(BUILD_WEB)

web-build: web-configure
	cmake --build $(BUILD_WEB) -j$(PAR)

web-configure:
	@if [ ! -f $(BUILD_WEB)/CMakeCache.txt ]; then \
		. $(EMSDK)/emsdk_env.sh 2>/dev/null && \
		emcmake cmake -B $(BUILD_WEB) -DCMAKE_BUILD_TYPE=Release; \
	fi

web-clean:
	rm -rf $(BUILD_WEB)
