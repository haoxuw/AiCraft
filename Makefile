SHELL := /bin/bash
BUILD_DIR := build
GAME_BUILD_DIR := build-perf
BUILD_TYPE := Debug
HOST :=
PORT := 7777
GAME_PORT :=
GAME := civcraft

# Parallelism for cmake --build. Defaults to HALF the core count so a build
# doesn't pin the machine / trigger OOM on big templated sources. Override on
# the command line, e.g. `make build PAR=8` or `make build PAR=1`.
PAR := $(shell nproc 2>/dev/null | awk '{n=int($$1/2); print (n<1)?1:n}')

.PHONY: game game-build game-configure build configure clean server client stop test_e2e proxy test-dog test-villager profiler killservers character_views item_views model-editor model-snap animation_sweep test_animation download_music jukebox civcraft crafter bbmodel sample pathfinding_test

# ── Native (CivCraft) ───────────────────────────────────────
#
# CivCraft is a native Vulkan C++ voxel sandbox.
#
# Quick reference:
#   make game                 Singleplayer (spawns its own server, skip-menu)
#   make game GAME_PORT=7890  Singleplayer on a fixed port
#   make server               Dedicated server (interactive world select)
#   make server PORT=N        Dedicated server on port N
#   make client               GUI client → menu (Start / Join)
#   make client HOST=X PORT=N GUI with server pre-filled
#   make test_e2e             Headless end-to-end gameplay tests
#   make stop                 Kill all game processes
#   make proxy                HTTP→TCP action proxy with Swagger UI on :8088
#
# Multiplayer (separate terminals):
#   Terminal 1: make server PORT=7777
#   Terminal 2: make client HOST=127.0.0.1 PORT=7777

civcraft: game

# `make game` launches the Vulkan client. It spawns its own civcraft-server
# on startup and connects over TCP — same architecture as `make client`,
# just wired for single-user, skip-menu iteration.
game: game-build
	cd $(GAME_BUILD_DIR) && ./civcraft-ui-vk --skip-menu$(if $(GAME_PORT), --port $(GAME_PORT),)

profiler: game-build
	cd $(GAME_BUILD_DIR) && ./civcraft-ui-vk --skip-menu --profiler$(if $(GAME_PORT), --port $(GAME_PORT),)

# Minimal isolation worlds for focused behavior testing.
test-dog: game-build
	cd $(GAME_BUILD_DIR) && ./civcraft-ui-vk --skip-menu --template 3

test-villager: game-build
	cd $(GAME_BUILD_DIR) && ./civcraft-ui-vk --skip-menu --template 4

# ── Visual QA scenarios ─────────────────────────────────────
CHARACTER := base:pig
ITEM      := base:sword
CLIP      :=

character_views: build
	cd $(BUILD_DIR) && ./civcraft-ui-vk --skip-menu \
	    --debug-scenario character_views --debug-character $(CHARACTER) \
	    $(if $(CLIP),--debug-clip $(CLIP))

item_views: build
	cd $(BUILD_DIR) && ./civcraft-ui-vk --skip-menu \
	    --debug-scenario item_views --debug-item $(ITEM)

SAMPLE_DIR := /tmp/civcraft_samples
sample: build
	@src/model_editor/capture_samples.sh $(BUILD_DIR) $(SAMPLE_DIR)

CHARS := player knight mage villager skeleton giant owl pig cat chicken dog beaver bee squirrel raccoon
CLIPS := walk attack chop mine wave dance fly land sleep
HAND_attack := sword
HAND_chop   := sword
HAND_mine   := sword
HAND_walk   := shield
HAND_wave   := sword
HAND_dance  :=
HAND_fly    :=
HAND_land   :=
HAND_sleep  :=

animation_sweep: build
	@rm -rf /tmp/anim_review
	@mkdir -p /tmp/anim_review
	@pgrep -x civcraft-server 2>/dev/null | xargs -r kill -9 ; true
	@for char in $(CHARS); do \
	  for clip in $(CLIPS); do \
	    hand_var=HAND_$$clip; \
	    hand=$$(eval echo \$$$$hand_var); \
	    echo "=== $$char / $$clip$${hand:+ +$$hand} ==="; \
	    pgrep -x civcraft-server 2>/dev/null | xargs -r kill -9 ; \
	    rm -f /tmp/debug_*.ppm; \
	    ( cd $(BUILD_DIR) && timeout 30 ./civcraft-ui-vk --skip-menu \
	      --debug-scenario character_views --debug-character base:$$char \
	      --debug-clip $$clip \
	      $${hand:+--debug-hand-item $$hand} ) >/dev/null 2>&1 || true; \
	    mkdir -p /tmp/anim_review/$$char; \
	    for p in /tmp/debug_*.ppm; do \
	      [ -e "$$p" ] || continue; \
	      suf=$$(basename $$p .ppm | sed 's/^debug_[0-9]*_//'); \
	      python3 -c "from PIL import Image; Image.open('$$p').save('/tmp/anim_review/$$char/$$suf.png')"; \
	    done; \
	  done; \
	done
	@echo "Done. Shots in /tmp/anim_review/<char>/<clip>_p{0..3}.png"

test_animation: animation_sweep

# ── Model viewer / snapshot tool ────────────────────────────
# (standalone — not yet ported to the VK RHI)
MODEL ?=
OUT   ?= /tmp/model_snap
SIZE  ?= 512x512

crafter:
	@model="$(MODEL)"; \
	  [ -n "$$model" ] || model="src/artifacts/models/base/player.py"; \
	  python3 src/model_editor/modelcrafter.py $$model $(if $(CLIP),--clip $(CLIP))

BB := /tmp/$(notdir $(basename $(MODEL))).bbmodel
bbmodel:
	@[ -n "$(MODEL)" ] || (echo "usage: make bbmodel MODEL=path/to/model.py" >&2 && exit 1)
	@[ -f "$(MODEL)" ] || (echo "$(MODEL): not a file" >&2 && exit 1)
	@command -v blockbench >/dev/null || (echo "blockbench not on PATH — install from https://www.blockbench.net/" >&2 && exit 1)
	python3 src/model_editor/bbmodel_export.py $(MODEL) $(BB)
	@echo "[bbmodel] opening $(BB) in Blockbench — save (Ctrl+S) then close to import back."
	blockbench $(BB)
	python3 src/model_editor/bbmodel_import.py $(BB) $(MODEL) --base $(MODEL)
	@echo "[bbmodel] imported back into $(MODEL). Run 'make build' then 'make character_views CHARACTER=...' to verify."

# Dedicated server (interactive world select, or --world/--seed/--template flags)
server: build
	cd $(BUILD_DIR) && ./civcraft-server --port $(PORT)

# GUI client: shows menu with "Start game" and "Join a game" tabs
client: build
	cd $(BUILD_DIR) && ./civcraft-ui-vk$(if $(HOST), --host $(HOST) --port $(PORT),)

stop:
	@-pgrep -x civcraft-ui-vk 2>/dev/null | xargs -r kill ; true
	@-pgrep -x civcraft-server 2>/dev/null | xargs -r kill ; true
	@-pgrep -x civcraft-agent 2>/dev/null | xargs -r kill ; true
	@sleep 1
	@echo "All civcraft processes stopped."

killservers:
	@echo "Looking for civcraft server processes..."
	@-pgrep -x civcraft-server 2>/dev/null | xargs -r kill && echo "Killed." || echo "No servers running."

# Headless E2E gameplay tests (CivCraft)
pathfinding_test: build
	@echo "[pathfinding_test] Running headless pathfinding tests..."
	cd $(BUILD_DIR) && ./civcraft-test-pathfinding
	@echo "[pathfinding_test] Done."

test_e2e: build pathfinding_test
	@echo "[test_e2e] Running headless gameplay tests..."
	cd $(BUILD_DIR) && ./civcraft-test
	@echo "[test_e2e] Done."

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
	python3 src/python/action_proxy.py --server-host 127.0.0.1 --server-port $(PORT) \
	    --proxy-port $(PROXY_PORT) --auto-connect

# ── Music library ─────────────────────────────────────────
download_music:
	@count=$$(find music/tracks -type f \( -name '*.mp3' -o -name '*.ogg' -o -name '*.wav' -o -name '*.flac' \) 2>/dev/null | wc -l); \
	if [ "$(FORCE)" != "1" ] && [ $$count -gt 0 ]; then \
		echo "[download_music] $$count tracks already present in music/tracks/ — skipping. Re-fetch with FORCE=1."; \
	else \
		cd music && ./redownload.sh; \
	fi

jukebox: download_music
	cd music && python3 jukebox.py
