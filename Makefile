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

.PHONY: game game-build game-configure build configure clean server client stop test_e2e web web-build web-configure web-clean proxy test-dog test-villager profiler killservers cellcraft-server-build cellcraft-server-game-build character_views item_views model-editor model-snap animation_sweep test_animation download_music jukebox civcraft cellcraft

# ── Native ─────────────────────────────────────────────────
#
# This repo builds two games from one C++ engine (src/platform):
#   civcraft       voxel sandbox (default) — C++ server + C++/OpenGL client
#   cellcraft       Spore cell stage       — C++ (single-binary M0; server+agent split lands M1)
#
# Override the game with GAME=cellcraft for CellCraft targets.
#
# Quick reference:
#   make civcraft             Singleplayer CivCraft (voxel sandbox)
#   make cellcraft            Singleplayer CellCraft (drawing game)
#   make game                 Alias for `make civcraft` (legacy; GAME= still works)
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

# Singleplayer: auto-launches server + bot processes + GUI client.
# Assets are staged into $(BUILD_DIR) by CMake POST_BUILD, so we cd there
# so "shaders/", "artifacts/", "python/", "fonts/", "config/", "resources/"
# resolve via CWD-relative paths at runtime.
#
# CellCraft is a pure C++ game built on src/platform/. `make game GAME=cellcraft`
# runs the `cellcraft` binary; `make cellcraft-godot` still launches the Godot
# prototype in src/CellCraft/godot/ for visual reference.
CELLCRAFT_PORT := 7888

# Explicit per-game entry points — `make game` alone is ambiguous since
# this repo builds two games. These just re-invoke make with GAME set:
#   make civcraft     → CivCraft voxel sandbox (same as `make game`)
#   make cellcraft    → CellCraft drawing game (same as `make game GAME=cellcraft`)
civcraft:
	$(MAKE) game GAME=civcraft
cellcraft:
	$(MAKE) game GAME=cellcraft

ifeq ($(GAME),cellcraft)
# M0 scope: single-binary drawing client. Server + agent processes come with
# networking (M1+). Binary runs out of build/src/CellCraft/ so its shaders/
# post-build copy resolves CWD-relative.
game: cellcraft-build
	cd $(BUILD_DIR)/src/CellCraft && ./cellcraft $(if $(DEMO),--demo,)
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
	    ( cd $(BUILD_DIR) && timeout 30 ./$(GAME) --skip-menu \
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
# Shared by CivCraft + CellCraft.
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
ifeq ($(GAME),cellcraft)
# Server/client not yet split — M0 ships as the single `cellcraft` binary.
# Re-enable these once networking lands.
server:
	@echo "cellcraft server is not yet split from the client; see src/CellCraft/docs/00_OVERVIEW.md M1" >&2
	@exit 1

client: cellcraft-build
	cd $(BUILD_DIR)/src/CellCraft && ./cellcraft

cellcraft-build: configure
	cmake --build $(BUILD_DIR) --target cellcraft -j$(PAR)

# Godot prototype is retained as a visual reference — not the shipping client.
cellcraft-godot:
	godot4 --path $(CURDIR)/src/CellCraft/godot

.PHONY: cellcraft-build cellcraft-godot
else
server: build
	cd $(BUILD_DIR) && ./$(GAME)-server --port $(PORT)

# GUI client: shows menu with "Start game" and "Join a game" tabs
client: build
	cd $(BUILD_DIR) && ./$(GAME)$(if $(HOST), --host $(HOST) --port $(PORT),)
endif

stop:
ifeq ($(GAME),cellcraft)
	@-pkill -x cellcraft 2>/dev/null; \
	  pkill -x cellcraft-server 2>/dev/null; \
	  pkill -f "godot4.*src/CellCraft/godot" 2>/dev/null; \
	  sleep 0.5; true
	@echo "All cellcraft processes stopped."
else
	@-pkill -f "$(GAME)" 2>/dev/null; sleep 1
	@echo "All $(GAME) processes stopped."
endif

killservers:
	@echo "Looking for $(GAME) server processes..."
	@-pgrep -fa "$(GAME)-server" 2>/dev/null && pkill -f "$(GAME)-server" && echo "Killed." || echo "No servers running."
	@-pgrep -fa "$(GAME)".*--port" 2>/dev/null && pkill -f "$(GAME)".*--port" && echo "Killed port processes." || true

# Headless E2E gameplay tests
ifeq ($(GAME),cellcraft)
# CellCraft doesn't need the full civcraft build — only cellcraft-server.
cellcraft-server-build: configure
	cmake --build $(BUILD_DIR) --target cellcraft-server -j$(PAR)

test_e2e: cellcraft-server-build
	@echo "[test_e2e] CellCraft M1 — server tick broadcast handshake..."
	@pkill -x cellcraft-server 2>/dev/null ; sleep 0.2 ; \
	  $(CURDIR)/$(BUILD_DIR)/cellcraft-server --port $(CELLCRAFT_PORT) & \
	  SERVER_PID=$$! ; \
	  sleep 0.3 ; \
	  godot4 --headless --path $(CURDIR)/src/CellCraft/godot -- \
	    --host 127.0.0.1 --port $(CELLCRAFT_PORT) --ticks 3 ; \
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

# ── Music library ─────────────────────────────────────────
# `music/` holds ~2,100 royalty-free tracks (Incompetech + OpenGameArt)
# used by CellCraft. Tracks are gitignored; re-fetch with `make download_music`
# (idempotent — skips files already on disk). `make jukebox` runs
# download_music first, then opens the terminal curator (+ / - / ?).
# See music/README.md for details.

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
