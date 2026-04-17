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

.PHONY: game game-build game-configure build configure clean server client stop test_e2e web web-build web-configure web-clean proxy test-dog test-villager profiler killservers character_views item_views model-editor model-snap animation_sweep test_animation download_music jukebox civcraft crafter bbmodel gl vk sample

# ── Native (CivCraft) ───────────────────────────────────────
#
# CivCraft is a native C++ voxel sandbox built on src/platform/.
#
# Quick reference:
#   make civcraft             Singleplayer CivCraft (voxel sandbox)
#   make game                 Alias for `make civcraft`
#   make game GAME_PORT=7890  CivCraft on a fixed port
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

civcraft:
	$(MAKE) game GAME=civcraft

gl: build
	cd $(BUILD_DIR) && ./civcraft-ui --skip-menu$(if $(GAME_PORT), --port $(GAME_PORT),)

vk: configure
	cmake --build $(BUILD_DIR) --target civcraft-ui-vk -j$(PAR)
	cd $(BUILD_DIR) && ./civcraft-ui-vk

# `make game` launches the Vulkan-native client. civcraft-ui-vk always spawns
# its own civcraft-server (village world, seed 42) on startup and connects
# over TCP — same architecture as the GL client, just rendered through the
# RHI's Vulkan backend. --skip-menu jumps straight into gameplay so the loop
# is ready for behavior verification (DECIDE logs, screenshots, etc.).
game: game-build
	cd $(GAME_BUILD_DIR) && ./civcraft-ui-vk --skip-menu$(if $(GAME_PORT), --port $(GAME_PORT),)

# Legacy GL entry point — keep around for A/B comparisons during Phase 3.
game-gl: game-build
	cd $(GAME_BUILD_DIR) && ./$(GAME)-ui --skip-menu$(if $(GAME_PORT), --port $(GAME_PORT),)

profiler: game-build
	cd $(GAME_BUILD_DIR) && ./$(GAME)-ui --skip-menu --profiler$(if $(GAME_PORT), --port $(GAME_PORT),)

# Minimal isolation worlds for focused behavior testing.
test-dog: game-build
	cd $(GAME_BUILD_DIR) && ./$(GAME)-ui --skip-menu --template 3

test-villager: game-build
	cd $(GAME_BUILD_DIR) && ./$(GAME)-ui --skip-menu --template 4

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
	cd $(BUILD_DIR) && ./$(GAME)-ui --skip-menu \
	    --debug-scenario character_views --debug-character $(CHARACTER) \
	    $(if $(CLIP),--debug-clip $(CLIP))

item_views: build
	cd $(BUILD_DIR) && ./$(GAME)-ui --skip-menu \
	    --debug-scenario item_views --debug-item $(ITEM)

# sample: drive civcraft-ui-vk through a menu → gameplay (FPS/TPS/RPG/RTS)
# → inventory → paused → admin-overview tour and write one PNG per stage
# to $(SAMPLE_DIR). Uses the file-based debug triggers baked into Debug
# builds (no xdotool / key injection), so $(BUILD_DIR) MUST be Debug.
#
#   make sample                           # → /tmp/civcraft_samples/*.png
#   make sample SAMPLE_DIR=/tmp/foo       # custom output dir
SAMPLE_DIR := /tmp/civcraft_samples
sample: build
	@tools/platform/capture_samples.sh $(BUILD_DIR) $(SAMPLE_DIR)

# animation_sweep: shoot every (character × clip) combination and sort the
# output into /tmp/anim_review/<character>/<clip>_p{0..3}.png. Clips probed:
# walk (pseudo, drives walk cycle) + the six named clips defined in models
# (attack/chop/mine/wave/dance/sleep/fly/land). Missing clips produce a
# still pose — easy to skip during review. Takes ~25s per char × clip.
#
#   make animation_sweep                 # all characters × {walk,attack,chop,mine,wave,dance}
#   make animation_sweep CHARS="player knight mage"
#   make animation_sweep CLIPS="walk attack"
CHARS := player knight mage villager skeleton giant owl pig cat chicken dog beaver bee squirrel raccoon
CLIPS := walk attack chop mine wave dance fly land sleep
# Per-clip held item: shows held-weapon wiring alongside the clip arc.
# attack/chop/mine → sword, walk → shield (idle guard pose), others bare.
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
	@pgrep -x $(GAME)-server 2>/dev/null | xargs -r kill -9 ; true
	@for char in $(CHARS); do \
	  for clip in $(CLIPS); do \
	    hand_var=HAND_$$clip; \
	    hand=$$(eval echo \$$$$hand_var); \
	    echo "=== $$char / $$clip$${hand:+ +$$hand} ==="; \
	    pgrep -x $(GAME)-server 2>/dev/null | xargs -r kill -9 ; \
	    rm -f /tmp/debug_*.ppm; \
	    ( cd $(BUILD_DIR) && timeout 30 ./$(GAME)-ui --skip-menu \
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

# Alias: `make test_animation` is the canonical name for regression-finding
# runs. Each (character × clip × held-item) combo writes 4 phase shots
# (sin() quarters) so max-swing frames surface axis/pivot bugs quickly.
# Works for a subset to keep runtime reasonable; override CHARS/CLIPS on
# the command line to narrow further:
#   make test_animation CHARS="knight" CLIPS="attack wave"
test_animation: animation_sweep

# Standalone model viewer / snapshot tool (no world, no server, no full client).
#
#   make model-editor MODEL=src/artifacts/models/base/cat.py
#       Interactive window (drag to orbit, scroll to zoom, Esc to quit).
#
#   make model-snap   MODEL=src/artifacts/models/base/cat.py OUT=/tmp/cat
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

# ModelCrafter: pure-Python animation debugger (tools/modelcrafter.py). No
# build, no game — imports a model .py directly and runs the same clip math
# as src/platform/client/model.cpp in a matplotlib 3D window.
#
#   make crafter                              # default model: player
#   make crafter MODEL=src/artifacts/models/base/knight.py
#   make crafter CLIP=sit                     # start with a named clip selected
crafter:
	@model="$(MODEL)"; \
	  [ -n "$$model" ] || model="src/artifacts/models/base/player.py"; \
	  python3 tools/modelcrafter.py $$model $(if $(CLIP),--clip $(CLIP))

# Round-trip a model through Blockbench: export .py → .bbmodel, open
# Blockbench (blocks until you close it), then import the edited
# .bbmodel back over the original .py — preserving colors, swing
# params, clips, and hand/pivot points via name-match merge against
# the original. Requires `blockbench` on PATH.
#
#   make bbmodel MODEL=src/artifacts/models/base/player.py
BB := /tmp/$(notdir $(basename $(MODEL))).bbmodel
bbmodel:
	@[ -n "$(MODEL)" ] || (echo "usage: make bbmodel MODEL=path/to/model.py" >&2 && exit 1)
	@[ -f "$(MODEL)" ] || (echo "$(MODEL): not a file" >&2 && exit 1)
	@command -v blockbench >/dev/null || (echo "blockbench not on PATH — install from https://www.blockbench.net/" >&2 && exit 1)
	python3 tools/bbmodel_export.py $(MODEL) $(BB)
	@echo "[bbmodel] opening $(BB) in Blockbench — save (Ctrl+S) then close to import back."
	blockbench $(BB)
	python3 tools/bbmodel_import.py $(BB) $(MODEL) --base $(MODEL)
	@echo "[bbmodel] imported back into $(MODEL). Run 'make build' then 'make character_views CHARACTER=...' to verify."

model-snap: model-editor-build
	@[ -n "$(MODEL)" ] || (echo "usage: make model-snap MODEL=path/to/model.py [OUT=dir]" >&2 && exit 1)
	cd $(BUILD_DIR) && ./model-editor $(CURDIR)/$(MODEL) \
	    --snapshot $(OUT) --size $(SIZE) $(if $(CLIP),--clip $(CLIP))

# Dedicated server (interactive world select, or --world/--seed/--template flags)
server: build
	cd $(BUILD_DIR) && ./$(GAME)-server --port $(PORT)

# GUI client: shows menu with "Start game" and "Join a game" tabs
client: build
	cd $(BUILD_DIR) && ./$(GAME)-ui$(if $(HOST), --host $(HOST) --port $(PORT),)

stop:
	@-pkill -f "$(GAME)" 2>/dev/null; sleep 1
	@echo "All $(GAME) processes stopped."

killservers:
	@echo "Looking for $(GAME) server processes..."
	@-pgrep -fa "$(GAME)-server" 2>/dev/null && pkill -f "$(GAME)-server" && echo "Killed." || echo "No servers running."
	@-pgrep -fa "$(GAME)".*--port" 2>/dev/null && pkill -f "$(GAME)".*--port" && echo "Killed port processes." || true

# Headless E2E gameplay tests (CivCraft)
pathfinding_test: build
	@echo "[pathfinding_test] Running headless pathfinding tests..."
	cd $(BUILD_DIR) && ./$(GAME)-test-pathfinding
	@echo "[pathfinding_test] Done."

test_e2e: build pathfinding_test
	@echo "[test_e2e] Running headless gameplay tests..."
	cd $(BUILD_DIR) && ./$(GAME)-test
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

# ── Music library ─────────────────────────────────────────
# `music/` holds ~2,100 royalty-free tracks (Incompetech + OpenGameArt).
# Tracks are gitignored; re-fetch with `make download_music` (idempotent —
# skips files already on disk). `make jukebox` runs download_music first,
# then opens the terminal curator. See music/README.md for details.

# Skip the download entirely when tracks/ already has files — both
# harvesters still hit the network (catalog enumeration) even when every
# file is on disk, so we short-circuit here. Force a refresh with
# `make download_music FORCE=1`.
download_music:
	@count=$$(find music/tracks -type f \( -name '*.mp3' -o -name '*.ogg' -o -name '*.wav' -o -name '*.flac' \) 2>/dev/null | wc -l); \
	if [ "$(FORCE)" != "1" ] && [ $$count -gt 0 ]; then \
		echo "[download_music] $$count tracks already present in music/tracks/ — skipping. Re-fetch with FORCE=1."; \
	else \
		cd music && ./redownload.sh; \
	fi

jukebox: download_music
	cd music && python3 jukebox.py
