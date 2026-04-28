SHELL := /bin/bash
BUILD_DIR := build
GAME_BUILD_DIR := build-perf
# NOTE: both trees currently use Debug until the Release-mode world-gen crash
# in ConfigurableWorldTemplate::generate is fixed (recursive generateChunk
# via getBlock in initWorld's lambda — SIGSEGVs in Release, hangs silently in
# RelWithDebInfo). Once that's resolved, flip `make client`/`make server` to
# Release and `make game` to RelWithDebInfo+SOLARIUM_PERF=ON for representative
# perf numbers.
BUILD_TYPE := Debug
HOST :=
PORT := 7777
GAME_PORT :=
GAME := solarium

# Parallelism for cmake --build. Defaults to HALF the core count so a build
# doesn't pin the machine / trigger OOM on big templated sources. Override on
# the command line, e.g. `make build PAR=8` or `make build PAR=1`.
PAR := $(shell nproc 2>/dev/null | awk '{n=int($$1/2); print (n<1)?1:n}')

.PHONY: game game-build game-configure build configure clean server client stop test_e2e proxy test-dog test-villager test-chicken toronto world debug_villager profiler killservers character_views item_views model-editor model-snap animation_sweep test_animation download_music jukebox solarium crafter bbmodel sample pathfinding_test perf_fps perf_server flamegraph llm_setup llm_server llm_stop llm_clean whisper_setup whisper_server whisper_stop tts_setup tts_server tts_stop imageto3d_setup imageto3d_smoke imageto3d_stop imageto3d_clean ai_setup ai_stop ai_clean cef_setup cef_clean cef_demo demo

# ── Native (Solarium) ───────────────────────────────────────
#
# Solarium is a native Vulkan C++ voxel sandbox.
#
# Quick reference:
#   make game                 Singleplayer under perf record (dev/profiling default)
#   make game GDB=1           Same, but wrap under gdb for crash debugging
#   make game PROFILE=none    Bare binary (no gdb, no perf)
#   make game GAME_PORT=7890  Fixed singleplayer port
#   make server               Dedicated server (interactive world select)
#   make server PORT=N        Dedicated server on port N
#   make client               GUI client → menu (no profiler, prod-style)
#   make client HOST=X PORT=N GUI with server pre-filled
#   make test_e2e             Headless end-to-end gameplay tests
#   make stop                 Kill all game processes
#   make proxy                HTTP→TCP action proxy with Swagger UI on :8088
#
# Multiplayer (separate terminals):
#   Terminal 1: make server PORT=7777
#   Terminal 2: make client HOST=127.0.0.1 PORT=7777

solarium: game

# `make game N` → spawn N villagers. Captures the first positional arg after
# `game` when it's all digits, stamps it into GAME_VILLAGERS, and registers
# the digit-word as a no-op phony target so make doesn't bail on "no rule to
# make target '100'". Pure client-side — the client sets SOLARIUM_VILLAGERS
# in the env inherited by the spawned server.
ifeq ($(firstword $(MAKECMDGOALS)),game)
  GAME_ARG := $(word 2,$(MAKECMDGOALS))
  GAME_VILLAGERS := $(shell echo "$(GAME_ARG)" | grep -E '^[0-9]+$$')
  ifneq ($(GAME_VILLAGERS),)
    .PHONY: $(GAME_VILLAGERS)
    $(GAME_VILLAGERS):
	@:
  endif
endif

# `make game` launches the Vulkan client under `perf record` (CPU profiling)
# by default so you get per-session stack samples *and* the built-in PERF
# histograms + bottleneck line in the exit summary. For release-style smoke
# runs without the profiler attached, use `make client` instead.
#
# Override the log path with `make game GAME_LOG=/path/to.log`.
#
# Escape hatches:
#   make game GDB=1    — wrap under gdb (crash-debug mode; no perf record)
#   make game PROFILE=none — run bare binary (no gdb, no perf)
GAME_LOG  := /tmp/solarium_game_run.log
PERF_DATA := /tmp/solarium.perf.data
PERF_REPORT := /tmp/solarium_perf_report.txt

# perf samples at 99 Hz with call graphs (DWARF unwinder — works on our
# non-frame-pointer-friendly optimization levels). --call-graph dwarf keeps
# stacks accurate at modest recording overhead (<5% in our framework).
define run_under_perf
	cd $(GAME_BUILD_DIR) && { \
	paranoid=$$(cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null || echo 99); \
	can_perf=1; \
	if ! command -v perf >/dev/null 2>&1; then \
	    echo "[make] perf not on PATH — falling back to bare run."; \
	    echo "[make]   install with:  sudo apt install linux-tools-generic"; \
	    can_perf=0; \
	elif [ "$$paranoid" -gt 2 ]; then \
	    echo "[make] kernel.perf_event_paranoid=$$paranoid (>2) — falling back to bare run."; \
	    echo "[make]   enable with:   sudo sysctl kernel.perf_event_paranoid=2"; \
	    can_perf=0; \
	fi; \
	if [ "$$can_perf" = "0" ]; then \
	    echo "[make]   for crash-debug:  make game GDB=1"; \
	    echo "[make] launching bare — log: $(GAME_LOG)"; \
	    ./solarium-ui-vk --skip-menu --cef-menu $(1) 2>&1 | tee $(GAME_LOG); \
	else \
	    rm -f $(PERF_DATA); \
	    echo "[make] launching under perf — data: $(PERF_DATA), log: $(GAME_LOG)"; \
	    perf record -F 99 --call-graph dwarf -o $(PERF_DATA) \
	        -- ./solarium-ui-vk --skip-menu --cef-menu $(1) 2>&1 | tee $(GAME_LOG); \
	    echo; echo "=========== perf report (top-30) ==========="; \
	    perf report --stdio --no-children -g none -i $(PERF_DATA) 2>/dev/null | head -60 | tee $(PERF_REPORT); \
	    if command -v flamegraph.pl >/dev/null && command -v stackcollapse-perf.pl >/dev/null; then \
	        ts=$$(date '+%Y%m%d_%H%M%S'); \
	        perf script -i $(PERF_DATA) 2>/dev/null | stackcollapse-perf.pl > /tmp/solarium_folded.txt && \
	        flamegraph.pl --title="solarium combined $$ts" < /tmp/solarium_folded.txt \
	            > /tmp/solarium_flamegraph_combined_$$ts.svg && \
	        grep '^solarium-ui-vk;'  /tmp/solarium_folded.txt | \
	            flamegraph.pl --title="solarium-ui-vk (client) $$ts" \
	            > /tmp/solarium_flamegraph_client_$$ts.svg && \
	        grep '^solarium-server;' /tmp/solarium_folded.txt | \
	            flamegraph.pl --title="solarium-server (server) $$ts" \
	            > /tmp/solarium_flamegraph_server_$$ts.svg && \
	        rm -f /tmp/solarium_folded.txt && \
	        ln -sf /tmp/solarium_flamegraph_combined_$$ts.svg /tmp/solarium_flamegraph.svg && \
	        ln -sf /tmp/solarium_flamegraph_client_$$ts.svg   /tmp/solarium_flamegraph_client.svg && \
	        ln -sf /tmp/solarium_flamegraph_server_$$ts.svg   /tmp/solarium_flamegraph_server.svg; \
	    else \
	        echo "[make] flamegraph.pl not in PATH — install FlameGraph to get /tmp/solarium_flamegraph*.svg:"; \
	        echo "         git clone https://github.com/brendangregg/FlameGraph ~/FlameGraph"; \
	        echo "         export PATH=\$$PATH:~/FlameGraph"; \
	    fi; \
	fi; \
	echo; echo "=========== make game artifacts ==========="; \
	for f in $$(ls -t /tmp/solarium_perf_client_*.txt 2>/dev/null | head -1); do \
	    echo "  perf summary (client):  $$f"; \
	done; \
	for f in $$(ls -t /tmp/solarium_perf_server_*.txt 2>/dev/null | head -1); do \
	    echo "  perf summary (server):  $$f"; \
	done; \
	for f in $$(ls -t /tmp/solarium_flamegraph_client_*.svg 2>/dev/null | head -1); do \
	    echo "  flamegraph (client):    $$f"; \
	done; \
	for f in $$(ls -t /tmp/solarium_flamegraph_server_*.svg 2>/dev/null | head -1); do \
	    echo "  flamegraph (server):    $$f"; \
	done; \
	for f in $$(ls -t /tmp/solarium_flamegraph_combined_*.svg 2>/dev/null | head -1); do \
	    echo "  flamegraph (combined):  $$f"; \
	done; \
	if ls -t /tmp/solarium_flamegraph_client_*.svg >/dev/null 2>&1; then \
	    echo "                          (also: /tmp/solarium_flamegraph_{client,server,combined}.svg → newest)"; \
	    echo "                          inspect older runs:  make flamegraph N=2"; \
	elif [ "$$can_perf" = "0" ]; then \
	    echo "  flamegraph:             SKIPPED — bare run, perf record disabled (see [make] note above)"; \
	    if [ "$$paranoid" -gt 2 ]; then \
	        echo "                          fix:  sudo sysctl kernel.perf_event_paranoid=2"; \
	    fi; \
	else \
	    echo "  flamegraph:             SKIPPED — flamegraph.pl/stackcollapse-perf.pl not on PATH"; \
	fi; \
	[ -f $(PERF_DATA) ]   && echo "  perf record data:       $(PERF_DATA)"; \
	[ -f $(PERF_REPORT) ] && echo "  perf top-30 report:     $(PERF_REPORT)"; \
	[ -f $(GAME_LOG) ]    && echo "  game stdout/stderr log: $(GAME_LOG)"; \
	count=$$(ls /tmp/solarium_entity_*.log 2>/dev/null | wc -l); \
	if [ "$$count" -gt 0 ]; then \
	    echo "  entity logs ($$count files):"; \
	    ls -1 /tmp/solarium_entity_*.log | sed 's/^/    /'; \
	fi; \
	}
endef

# Common wrapper: $(call run_under_gdb, <extra solarium-ui-vk args>)
define run_under_gdb
	cd $(GAME_BUILD_DIR) && \
	echo "[make] launching under gdb — log: $(GAME_LOG)" && \
	gdb -batch \
	    -ex 'set pagination off' \
	    -ex 'set confirm off' \
	    -ex 'handle SIGPIPE nostop noprint pass' \
	    -ex 'handle SIGINT  nostop noprint pass' \
	    -ex 'handle SIGTERM nostop noprint pass' \
	    -ex 'handle SIGHUP  nostop noprint pass' \
	    -ex 'run' \
	    -ex 'echo \n=========== CRASH BACKTRACE ===========\n' \
	    -ex 'bt full' \
	    -ex 'echo \n=========== ALL THREADS ===========\n' \
	    -ex 'thread apply all bt' \
	    -ex 'echo \n=========== DISASM AT CRASH ===========\n' \
	    -ex 'disassemble' \
	    -ex 'info registers' \
	    -ex 'quit' \
	    --args ./solarium-ui-vk --skip-menu --cef-menu $(1) 2>&1 | tee $(GAME_LOG)
endef

# Bare invocation for PROFILE=none.
define run_bare
	cd $(GAME_BUILD_DIR) && \
	echo "[make] launching bare — log: $(GAME_LOG)" && \
	./solarium-ui-vk --skip-menu --cef-menu $(1) 2>&1 | tee $(GAME_LOG)
endef

# Router: GDB=1 → gdb wrapper; PROFILE=none → bare; else → perf record.
# $(call launch, <extra args>)
ifeq ($(GDB),1)
define launch
	$(call run_under_gdb,$(1))
endef
else ifeq ($(PROFILE),none)
define launch
	$(call run_bare,$(1))
endef
else
define launch
	$(call run_under_perf,$(1))
endef
endif

game: game-build
	$(call launch,$(if $(GAME_VILLAGERS),--villagers $(GAME_VILLAGERS)) $(if $(GAME_PORT),--port $(GAME_PORT)) $(if $(TERMINATE_AFTER_S),--terminate-after $(TERMINATE_AFTER_S)))

# Visible-window demo of the CEF HTML menu. No --log-only, no --skip-menu.
# Click Singleplayer/Multiplayer/Settings to dismiss the overlay (reveals
# native UI underneath). Click Quit to exit.
cef_demo: game-build
	cd $(GAME_BUILD_DIR) && ./solarium-ui-vk --cef-menu 2>&1 | tee $(GAME_LOG)

# Primary demo entry point. CEF HTML menu overlays the live menu plaza;
# clicking Singleplayer drives the engine's normal Connecting→Playing flow.
# Quit closes the window cleanly. Same as cef_demo today; the alias is
# intended to stay stable as the demo grows.
demo: cef_demo

profiler: game-build
	$(call launch,--profiler $(if $(GAME_PORT),--port $(GAME_PORT)))

# Minimal isolation worlds for focused behavior testing.
test-dog: game-build
	$(call launch,--template 2)

test-villager: game-build
	$(call launch,--template 3)

test-chicken: game-build
	$(call launch,--template 4)

# Voxel Earth demo: walk around a baked slab of Toronto. See docs/VOXEL_EARTH.md.
# First-time setup (one-off):
#   python -m voxel_earth set-key <YOUR_GOOGLE_MAPS_KEY>
#   python -m voxel_earth download --location "CN Tower, Toronto" \
#                                  --radius 800 --height 1200
#   ./build/solarium-voxel-bake --glb-dir ~/.voxel/google/glb \
#                                --out     ~/.voxel/regions/toronto/blocks.bin
# Then: `make toronto` (or its alias `make world`).
toronto: game-build
	@if [ ! -f $(HOME)/.voxel/regions/toronto/blocks.bin ]; then \
	  echo "[toronto] missing $(HOME)/.voxel/regions/toronto/blocks.bin"; \
	  echo "          run the bake recipe at the top of the toronto: target."; \
	  exit 1; \
	fi
	$(call launch,--template 6)

world: toronto

# Isolated single-villager behavior smoke: 1 villager, village world, 4× sim,
# hidden window, tee event stream to /tmp/solarium_game.log. Use for
# behavior/pathfinding/decision iteration — see the testing-plan skill.
# DURATION=N (seconds) ends the run early; default is unbounded.
DEBUG_DURATION ?=
debug_villager: debug_villager-reconfigure game-build
	@-pgrep -x solarium-ui-vk  2>/dev/null | xargs -r kill ; true
	@-pgrep -x solarium-server 2>/dev/null | xargs -r kill ; true
	@sleep 1
	@rm -f /tmp/solarium_entity_*.log
	@echo "[debug_villager] --debug-behavior (1 villager, sim-speed 4, log-only)"
	@echo "[debug_villager] log: /tmp/solarium_game.log (prior run kept as .prev)"
	@echo "[debug_villager] per-entity logs: /tmp/solarium_entity_<id>.log (cleared on launch)"
	@cd $(GAME_BUILD_DIR) && { \
	    $(if $(DEBUG_DURATION),timeout $(DEBUG_DURATION) )./solarium-ui-vk --debug-behavior; \
	    rc=$$?; \
	    $(if $(DEBUG_DURATION),[ $$rc -eq 124 ] && rc=0;) \
	    exit $$rc; \
	 }

# ── Visual QA scenarios ─────────────────────────────────────
CHARACTER := base:pig
ITEM      := base:sword
CLIP      :=

character_views: build
	cd $(BUILD_DIR) && ./solarium-ui-vk --skip-menu --cef-menu \
	    --debug-scenario character_views --debug-character $(CHARACTER) \
	    $(if $(CLIP),--debug-clip $(CLIP))

item_views: build
	cd $(BUILD_DIR) && ./solarium-ui-vk --skip-menu --cef-menu \
	    --debug-scenario item_views --debug-item $(ITEM)

SAMPLE_DIR := /tmp/solarium_samples
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
	@pgrep -x solarium-server 2>/dev/null | xargs -r kill -9 ; true
	@for char in $(CHARS); do \
	  for clip in $(CLIPS); do \
	    hand_var=HAND_$$clip; \
	    hand=$$(eval echo \$$$$hand_var); \
	    echo "=== $$char / $$clip$${hand:+ +$$hand} ==="; \
	    pgrep -x solarium-server 2>/dev/null | xargs -r kill -9 ; \
	    rm -f /tmp/debug_*.ppm; \
	    ( cd $(BUILD_DIR) && timeout 30 ./solarium-ui-vk --skip-menu --cef-menu \
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
	cd $(BUILD_DIR) && ./solarium-server --port $(PORT)

# GUI client (production-style: no perf record, no gdb).
#   bare       → CEF main menu (Singleplayer / Multiplayer / Handbook / Settings / Quit)
#   HOST=X PORT=N → menu boots straight to the character picker with the
#                   network transport pre-targeted at that server, so the
#                   next character pick connects to it.
client: build
	cd $(BUILD_DIR) && \
	$(if $(HOST),SOLARIUM_BOOT_PAGE=chars )./solarium-ui-vk --cef-menu$(if $(HOST), --host $(HOST) --port $(PORT),)

stop:
	@-pgrep -x solarium-ui-vk 2>/dev/null | xargs -r kill ; true
	@-pgrep -x solarium-server 2>/dev/null | xargs -r kill ; true
	@-pgrep -x solarium-agent 2>/dev/null | xargs -r kill ; true
	@sleep 1
	@echo "All solarium processes stopped."

killservers:
	@echo "Looking for solarium server processes..."
	@-pgrep -x solarium-server 2>/dev/null | xargs -r kill && echo "Killed." || echo "No servers running."

# Headless E2E gameplay tests (Solarium)
pathfinding_test: build
	@echo "[pathfinding_test] Running headless pathfinding tests..."
	cd $(BUILD_DIR) && ./solarium-test-pathfinding
	@echo "[pathfinding_test] Done."

test_e2e: build pathfinding_test
	@echo "[test_e2e] Running headless gameplay tests..."
	cd $(BUILD_DIR) && ./solarium-test
	@echo "[test_e2e] Done."

# ── Perf breakdown tables ──────────────────────────────────────────────
# Spin a solarium-ui-vk session (which auto-spawns a solarium-server),
# wait PERF_DURATION seconds, then parse the histograms each binary writes
# to /tmp/solarium_perf_{client,server}_<ts>.txt into a percent-breakdown
# table. Overrides:
#   PERF_DURATION=120  sample window (seconds)
#   PERF_TEMPLATE=5    world template index (5 = perf_stress, 100 villagers)
PERF_DURATION ?= 60
PERF_TEMPLATE ?= 0
PERF_REPORT_PY := src/platform/debug/perf_report.py

define run_perf_session
	@-pgrep -x solarium-ui-vk  2>/dev/null | xargs -r kill ; true
	@-pgrep -x solarium-server 2>/dev/null | xargs -r kill ; true
	@sleep 1
	@echo "[$(1)] running solarium-ui-vk (template=$(PERF_TEMPLATE)) for $(PERF_DURATION)s..."
	@cd $(GAME_BUILD_DIR) && timeout $(PERF_DURATION) \
	    ./solarium-ui-vk --skip-menu --cef-menu --log-only --template $(PERF_TEMPLATE) \
	    > /tmp/solarium_perf_$(1).stdout 2> /tmp/solarium_perf_$(1).stderr ; \
	    true
	@-pgrep -x solarium-ui-vk  2>/dev/null | xargs -r kill ; true
	@-pgrep -x solarium-server 2>/dev/null | xargs -r kill ; true
	@sleep 1
endef

perf_fps: game-build
	$(call run_perf_session,fps)
	@dump=$$(ls -t /tmp/solarium_perf_client_*.txt 2>/dev/null | head -1); \
	if [ -z "$$dump" ]; then \
	    echo "[perf_fps] no client dump — session may have crashed pre-Playing."; \
	    echo "[perf_fps] see /tmp/solarium_perf_fps.stderr"; \
	    exit 1; \
	fi; \
	echo "[perf_fps] parsing $$dump"; echo; \
	python3 $(PERF_REPORT_PY) client "$$dump"

perf_server: game-build
	$(call run_perf_session,server)
	@dump=$$(ls -t /tmp/solarium_perf_server_*.txt 2>/dev/null | head -1); \
	if [ -z "$$dump" ]; then \
	    echo "[perf_server] no server dump — session may have crashed early."; \
	    echo "[perf_server] see /tmp/solarium_perf_server.stderr"; \
	    exit 1; \
	fi; \
	echo "[perf_server] parsing $$dump"; echo; \
	python3 $(PERF_REPORT_PY) server "$$dump"

# Open Nth most-recent flamegraph(s) from /tmp. N=1 (default) → newest run;
# N=2 → run before that; etc. Lists every flamegraph on disk when nothing
# matches so the user can see what's there.
#
# `make flamegraph` is the same as N=1. Positional digit also works
# (`make flamegraph 2`) via the same trick `make game N` uses.
ifeq ($(firstword $(MAKECMDGOALS)),flamegraph)
  FLAME_ARG := $(word 2,$(MAKECMDGOALS))
  FLAME_N := $(shell echo "$(FLAME_ARG)" | grep -E '^[0-9]+$$')
  ifneq ($(FLAME_N),)
    .PHONY: $(FLAME_N)
    $(FLAME_N):
	@:
  endif
endif

flamegraph:
	@n=$${N:-$(if $(FLAME_N),$(FLAME_N),1)}; \
	cli=$$(ls -t /tmp/solarium_flamegraph_client_*.svg 2>/dev/null | sed -n "$${n}p"); \
	srv=$$(ls -t /tmp/solarium_flamegraph_server_*.svg 2>/dev/null | sed -n "$${n}p"); \
	cmb=$$(ls -t /tmp/solarium_flamegraph_combined_*.svg 2>/dev/null | sed -n "$${n}p"); \
	if [ -z "$$cli" ] && [ -z "$$srv" ] && [ -z "$$cmb" ]; then \
	    echo "[flamegraph] run #$$n not found."; \
	    total=$$(ls /tmp/solarium_flamegraph_client_*.svg 2>/dev/null | wc -l); \
	    if [ "$$total" -gt 0 ]; then \
	        echo "[flamegraph] $$total run(s) on disk (newest first):"; \
	        ls -lt /tmp/solarium_flamegraph_client_*.svg | sed 's/^/  /'; \
	    else \
	        echo "[flamegraph] no flamegraphs on /tmp — run \`make game\` first."; \
	    fi; \
	    exit 1; \
	fi; \
	echo "[flamegraph] run #$$n:"; \
	[ -n "$$cli" ] && echo "  client:   $$cli"; \
	[ -n "$$srv" ] && echo "  server:   $$srv"; \
	[ -n "$$cmb" ] && echo "  combined: $$cmb"; \
	if command -v xdg-open >/dev/null 2>&1; then \
	    [ -n "$$cli" ] && xdg-open "$$cli" >/dev/null 2>&1 & \
	    [ -n "$$srv" ] && xdg-open "$$srv" >/dev/null 2>&1 & \
	    wait; \
	else \
	    echo "[flamegraph] xdg-open not on PATH — open the SVGs manually in a browser."; \
	fi

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
		cmake -B $(GAME_BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DSOLARIUM_PERF=ON -DSOLARIUM_PATHFINDING_DEBUG=ON; \
	fi

# debug_villager needs PATHLOG compiled in so per-entity logs capture the
# navigator lifecycle. Force a reconfigure if the existing cache was built
# without it — cheap when the flag is already set.
debug_villager-reconfigure:
	@if ! grep -q "SOLARIUM_PATHFINDING_DEBUG:BOOL=ON" $(GAME_BUILD_DIR)/CMakeCache.txt 2>/dev/null; then \
		echo "[debug_villager] enabling SOLARIUM_PATHFINDING_DEBUG on $(GAME_BUILD_DIR)"; \
		cmake -B $(GAME_BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DSOLARIUM_PERF=ON -DSOLARIUM_PATHFINDING_DEBUG=ON; \
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

# ── Local LLM sidecar (NPC dialog) ──────────────────────────
#
# `make llm_setup` builds llama.cpp and downloads ONE permissively-licensed
# chat model into llm/models/. `make llm_server` starts the sidecar on
# 127.0.0.1:8080 — solarium-ui-vk's DialogPanel connects here when you press
# [T] on a humanoid NPC.
#
# Weights are NOT checked into source. Override model choice via LLM_MODEL=.
# Three vetted choices (all Apache-2.0 / MIT, safe for Steam redistribution):
#
#   LLM_MODEL=qwen    Qwen 2.5 3B Instruct, Q4_K_M (~2.0 GB) — default, best quality
#   LLM_MODEL=smol    SmolLM2 1.7B Instruct, Q4_K_M (~1.1 GB) — fastest, low-VRAM
#   LLM_MODEL=phi     Phi-3.5 Mini 3.8B Instruct, Q4_K_M (~2.3 GB) — strongest reasoning
#
# Named LLM_MODEL (not MODEL) to avoid collision with the bbmodel MODEL= var
# used by the modelcrafter targets further up.
LLM_MODEL   ?= qwen
LLM_DIR     := llm
LLAMA_DIR   := $(LLM_DIR)/llama.cpp
LLM_MODELS  := $(LLM_DIR)/models
LLM_PORT    ?= 8080
LLM_CTX     ?= 2048

# Model catalog — url + filename. Keep entries together so adding a new
# option is a single block.
LLM_URL_qwen  := https://huggingface.co/Qwen/Qwen2.5-3B-Instruct-GGUF/resolve/main/qwen2.5-3b-instruct-q4_k_m.gguf
LLM_FILE_qwen := qwen2.5-3b-instruct-q4_k_m.gguf

LLM_URL_smol  := https://huggingface.co/HuggingFaceTB/SmolLM2-1.7B-Instruct-GGUF/resolve/main/smollm2-1.7b-instruct-q4_k_m.gguf
LLM_FILE_smol := smollm2-1.7b-instruct-q4_k_m.gguf

LLM_URL_phi   := https://huggingface.co/bartowski/Phi-3.5-mini-instruct-GGUF/resolve/main/Phi-3.5-mini-instruct-Q4_K_M.gguf
LLM_FILE_phi  := Phi-3.5-mini-instruct-Q4_K_M.gguf

LLM_URL  := $(LLM_URL_$(LLM_MODEL))
LLM_FILE := $(LLM_FILE_$(LLM_MODEL))

llm_setup:
	@if [ -z "$(LLM_URL)" ]; then \
		echo "Unknown LLM_MODEL='$(LLM_MODEL)'. Pick one of: qwen, smol, phi" >&2; exit 1; \
	fi
	@mkdir -p $(LLM_MODELS)
	@if [ ! -d $(LLAMA_DIR) ]; then \
		echo "[llm] cloning llama.cpp into $(LLAMA_DIR)…"; \
		git clone --depth 1 https://github.com/ggerganov/llama.cpp $(LLAMA_DIR); \
	else \
		echo "[llm] llama.cpp already cloned — skipping"; \
	fi
	@if [ ! -x $(LLAMA_DIR)/build/bin/llama-server ]; then \
		echo "[llm] building llama.cpp (llama-server)…"; \
		cmake -S $(LLAMA_DIR) -B $(LLAMA_DIR)/build -DLLAMA_CURL=OFF -DCMAKE_BUILD_TYPE=Release; \
		cmake --build $(LLAMA_DIR)/build -j$(PAR) --target llama-server; \
	else \
		echo "[llm] llama-server already built — skipping"; \
	fi
	@if [ ! -f $(LLM_MODELS)/$(LLM_FILE) ]; then \
		echo "[llm] downloading model $(LLM_MODEL) → $(LLM_MODELS)/$(LLM_FILE)"; \
		echo "[llm]   (large file — may take several minutes)"; \
		curl -L --fail --progress-bar -o $(LLM_MODELS)/$(LLM_FILE).part $(LLM_URL) \
		  && mv $(LLM_MODELS)/$(LLM_FILE).part $(LLM_MODELS)/$(LLM_FILE); \
	else \
		echo "[llm] model $(LLM_FILE) already present — skipping"; \
	fi
	@echo ""
	@echo "[llm] setup done. The sidecar is spawned automatically by 'make game'."
	@echo "[llm] (or run 'make llm_server' to start it manually in its own terminal.)"

llm_server:
	@if [ ! -x $(LLAMA_DIR)/build/bin/llama-server ]; then \
		echo "[llm] llama-server not built — run 'make llm_setup' first" >&2; exit 1; \
	fi
	@if [ ! -f $(LLM_MODELS)/$(LLM_FILE) ]; then \
		echo "[llm] model $(LLM_FILE) missing — run 'make llm_setup' first" >&2; exit 1; \
	fi
	@echo "[llm] serving $(LLM_FILE) on 127.0.0.1:$(LLM_PORT)"
	@echo "[llm] (press [T] on a humanoid in-game to open the chat)"
	$(LLAMA_DIR)/build/bin/llama-server \
	    --host 127.0.0.1 --port $(LLM_PORT) \
	    --ctx-size $(LLM_CTX) --n-predict 200 \
	    -m $(LLM_MODELS)/$(LLM_FILE)

llm_stop:
	@-pgrep -x llama-server 2>/dev/null | xargs -r kill ; true
	@echo "[llm] sidecar stopped."

llm_clean:
	rm -rf $(LLM_DIR)

# ── Chromium Embedded Framework (CEF) ──────────────────────────────────────
#
# `make cef_setup` downloads the minimal CEF binary distribution into
# third_party/cef/. The directory is gitignored (~1.4 GB extracted, 290 MB
# tarball). solarium-ui-vk links libcef.so + libcef_dll_wrapper, and CEF's
# subprocess children re-exec ./solarium-cef-subprocess (built alongside).
#
# Linux x64 only today. macOS/Windows tarballs differ + need helper-app
# bundles — see docs/CEF_UI_PLAN.md §2.
CEF_DIR     := third_party/cef
CEF_CACHE   := $(GAME_BUILD_DIR)/_cef_cache
CEF_VERSION := 146.0.12+g6214c8e+chromium-146.0.7680.179
CEF_TARBALL := cef_binary_$(CEF_VERSION)_linux64_minimal.tar.bz2
CEF_URL     := https://cef-builds.spotifycdn.com/$(subst +,%2B,$(CEF_TARBALL))
CEF_SHA1    := b35f2607b5b9cd91550920c3f8cf04b46e319195

cef_setup:
	@if [ -f $(CEF_DIR)/Release/libcef.so ]; then \
		echo "[cef] already extracted at $(CEF_DIR) — skipping"; exit 0; \
	fi
	@mkdir -p $(CEF_CACHE) $(CEF_DIR)
	@if [ ! -f $(CEF_CACHE)/cef.tar.bz2 ]; then \
		echo "[cef] downloading minimal binary distribution ($(CEF_VERSION))…"; \
		curl -L --fail --progress-bar -o $(CEF_CACHE)/cef.tar.bz2.part "$(CEF_URL)" \
		  && mv $(CEF_CACHE)/cef.tar.bz2.part $(CEF_CACHE)/cef.tar.bz2; \
	else \
		echo "[cef] tarball already cached — skipping download"; \
	fi
	@echo "[cef] verifying SHA1…"
	@echo "$(CEF_SHA1)  $(CEF_CACHE)/cef.tar.bz2" | sha1sum -c - \
		|| (echo "[cef] SHA1 mismatch — delete $(CEF_CACHE)/cef.tar.bz2 and retry" >&2; exit 1)
	@echo "[cef] extracting into $(CEF_DIR)/ (1.4 GB, takes ~30 s)…"
	@tar -xjf $(CEF_CACHE)/cef.tar.bz2 -C $(CEF_DIR) --strip-components=1
	@echo "[cef] setup done. Re-configure with 'make configure'."

cef_clean:
	rm -rf $(CEF_DIR) $(CEF_CACHE)
	@echo "[cef] removed $(CEF_DIR) and tarball cache."

# ── Monaco editor (in-game code editor for Handbook + Inspect tabs) ─────
#
# Source lives at third_party/monaco-editor/ as a git submodule. The
# pre-built distribution is NOT committed; this target runs the upstream
# build to produce min/vs/ which the CEF pages reference via file://.
#
# `make monaco_setup` is idempotent (skips if min/vs/loader.js exists).
# Re-run after a submodule bump.
MONACO_DIR := third_party/monaco-editor
# Build output lives under out/monaco-editor/{dev,min}/vs/. We reference
# min/vs in the CEF page (smaller, gzipped sources).
MONACO_OUT := $(MONACO_DIR)/out/monaco-editor/min/vs/loader.js
.PHONY: monaco_setup monaco_clean
monaco_setup:
	@if [ ! -d $(MONACO_DIR) ] || [ -z "$$(ls $(MONACO_DIR))" ]; then \
		echo "[monaco] submodule empty — initialising…"; \
		git submodule update --init --depth 1 $(MONACO_DIR); \
	fi
	@if [ -f $(MONACO_OUT) ]; then \
		echo "[monaco] already built at $(MONACO_DIR)/min/ — skipping"; exit 0; \
	fi
	@echo "[monaco] npm install (may take 1-2 min)…"
	@cd $(MONACO_DIR) && npm install --no-audit --no-fund
	@echo "[monaco] npm run build (may take 2-3 min)…"
	@cd $(MONACO_DIR) && npm run build
	@if [ ! -f $(MONACO_OUT) ]; then \
		echo "[monaco] build did not produce $(MONACO_OUT) — check upstream README" >&2; \
		exit 1; \
	fi
	@echo "[monaco] setup done — built dist at $(MONACO_DIR)/min/."

monaco_clean:
	@cd $(MONACO_DIR) && rm -rf min release out node_modules 2>/dev/null
	@echo "[monaco] cleaned dist + node_modules."

# ── Whisper.cpp sidecar (speech-to-text for dialog input) ───────────────────
#
# `make whisper_setup` builds whisper.cpp's HTTP server binary and downloads
# one ggml model into llm/whisper_models/. solarium-ui-vk auto-spawns the
# sidecar on 127.0.0.1:8081 — hold [Y] in the dialog panel to push-to-talk.
#
#   WHISPER_MODEL=tiny   ~75 MB  — fastest, fine for short dialog (default)
#   WHISPER_MODEL=base   ~150 MB — more accurate
#   WHISPER_MODEL=small  ~490 MB — overkill but crispest
#
# Shared llm/ dir with LLM models so one `make ai_clean` wipes everything.
WHISPER_MODEL ?= tiny
WHISPER_DIR   := $(LLM_DIR)/whisper.cpp
WHISPER_MODELS:= $(LLM_DIR)/whisper_models
WHISPER_PORT  ?= 8081

WHISPER_URL_tiny  := https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.en.bin
WHISPER_FILE_tiny := ggml-tiny.en.bin

WHISPER_URL_base  := https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.en.bin
WHISPER_FILE_base := ggml-base.en.bin

WHISPER_URL_small := https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.en.bin
WHISPER_FILE_small:= ggml-small.en.bin

WHISPER_URL  := $(WHISPER_URL_$(WHISPER_MODEL))
WHISPER_FILE := $(WHISPER_FILE_$(WHISPER_MODEL))

whisper_setup:
	@if [ -z "$(WHISPER_URL)" ]; then \
		echo "Unknown WHISPER_MODEL='$(WHISPER_MODEL)'. Pick tiny, base, or small" >&2; exit 1; \
	fi
	@mkdir -p $(WHISPER_MODELS)
	@if [ ! -d $(WHISPER_DIR) ]; then \
		echo "[whisper] cloning whisper.cpp into $(WHISPER_DIR)…"; \
		git clone --depth 1 https://github.com/ggerganov/whisper.cpp $(WHISPER_DIR); \
	else \
		echo "[whisper] whisper.cpp already cloned — skipping"; \
	fi
	@if [ ! -x $(WHISPER_DIR)/build/bin/whisper-server ]; then \
		echo "[whisper] building whisper.cpp (whisper-server)…"; \
		cmake -S $(WHISPER_DIR) -B $(WHISPER_DIR)/build -DCMAKE_BUILD_TYPE=Release -DWHISPER_BUILD_SERVER=ON; \
		cmake --build $(WHISPER_DIR)/build -j$(PAR) --target whisper-server; \
	else \
		echo "[whisper] whisper-server already built — skipping"; \
	fi
	@if [ ! -f $(WHISPER_MODELS)/$(WHISPER_FILE) ]; then \
		echo "[whisper] downloading model $(WHISPER_MODEL) → $(WHISPER_MODELS)/$(WHISPER_FILE)"; \
		curl -L --fail --progress-bar -o $(WHISPER_MODELS)/$(WHISPER_FILE).part $(WHISPER_URL) \
		  && mv $(WHISPER_MODELS)/$(WHISPER_FILE).part $(WHISPER_MODELS)/$(WHISPER_FILE); \
	else \
		echo "[whisper] model $(WHISPER_FILE) already present — skipping"; \
	fi
	@echo "[whisper] setup done. Sidecar is spawned automatically by 'make game'."

whisper_server:
	@if [ ! -x $(WHISPER_DIR)/build/bin/whisper-server ]; then \
		echo "[whisper] not built — run 'make whisper_setup' first" >&2; exit 1; \
	fi
	@if [ ! -f $(WHISPER_MODELS)/$(WHISPER_FILE) ]; then \
		echo "[whisper] model missing — run 'make whisper_setup' first" >&2; exit 1; \
	fi
	@echo "[whisper] serving $(WHISPER_FILE) on 127.0.0.1:$(WHISPER_PORT)"
	$(WHISPER_DIR)/build/bin/whisper-server \
	    --host 127.0.0.1 --port $(WHISPER_PORT) \
	    -m $(WHISPER_MODELS)/$(WHISPER_FILE)

whisper_stop:
	@-pgrep -x whisper-server 2>/dev/null | xargs -r kill ; true
	@echo "[whisper] sidecar stopped."

# ── Piper sidecar (text-to-speech for NPC voice) ────────────────────────────
#
# Piper is a small, fast neural TTS (MIT, Rhasspy). We download a prebuilt
# binary + one voice model, and solarium-ui-vk spawns it in "JSON stdin →
# WAV-file" mode so each utterance gets a predictable output path.
#
#   TTS_VOICE=amy     en_US, female, medium   (~60 MB) — default
#   TTS_VOICE=ryan    en_US, male,   medium   (~60 MB)
#   TTS_VOICE=alan    en_GB, male,   medium   (~60 MB)
TTS_VOICE     ?= amy
PIPER_DIR     := $(LLM_DIR)/piper
PIPER_VOICES  := $(LLM_DIR)/piper_voices
TTS_PORT      ?= 8082

# Piper binary release (platform-agnostic .tar.gz with its own libs bundled).
# Pinning to a specific release so builds are reproducible; bump deliberately.
PIPER_REL      := 2023.11.14-2
PIPER_TGZ_URL  := https://github.com/rhasspy/piper/releases/download/$(PIPER_REL)/piper_linux_x86_64.tar.gz

VOICE_URL_amy_onnx  := https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/amy/medium/en_US-amy-medium.onnx
VOICE_URL_amy_json  := https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/amy/medium/en_US-amy-medium.onnx.json
VOICE_FILE_amy      := en_US-amy-medium

VOICE_URL_ryan_onnx := https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/ryan/medium/en_US-ryan-medium.onnx
VOICE_URL_ryan_json := https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/ryan/medium/en_US-ryan-medium.onnx.json
VOICE_FILE_ryan     := en_US-ryan-medium

VOICE_URL_alan_onnx := https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_GB/alan/medium/en_GB-alan-medium.onnx
VOICE_URL_alan_json := https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_GB/alan/medium/en_GB-alan-medium.onnx.json
VOICE_FILE_alan     := en_GB-alan-medium

VOICE_ONNX_URL := $(VOICE_URL_$(TTS_VOICE)_onnx)
VOICE_JSON_URL := $(VOICE_URL_$(TTS_VOICE)_json)
VOICE_FILE     := $(VOICE_FILE_$(TTS_VOICE))

tts_setup:
	@if [ -z "$(VOICE_ONNX_URL)" ]; then \
		echo "Unknown TTS_VOICE='$(TTS_VOICE)'. Pick amy, ryan, or alan" >&2; exit 1; \
	fi
	@mkdir -p $(PIPER_VOICES)
	@if [ ! -x $(PIPER_DIR)/piper ]; then \
		echo "[tts] downloading piper binary release $(PIPER_REL)…"; \
		mkdir -p $(LLM_DIR); \
		curl -L --fail --progress-bar -o $(LLM_DIR)/piper.tgz $(PIPER_TGZ_URL); \
		tar -xzf $(LLM_DIR)/piper.tgz -C $(LLM_DIR); \
		rm $(LLM_DIR)/piper.tgz; \
	else \
		echo "[tts] piper binary already present — skipping"; \
	fi
	@if [ ! -f $(PIPER_VOICES)/$(VOICE_FILE).onnx ]; then \
		echo "[tts] downloading voice $(TTS_VOICE) → $(PIPER_VOICES)/$(VOICE_FILE).onnx"; \
		curl -L --fail --progress-bar -o $(PIPER_VOICES)/$(VOICE_FILE).onnx.part $(VOICE_ONNX_URL) \
		  && mv $(PIPER_VOICES)/$(VOICE_FILE).onnx.part $(PIPER_VOICES)/$(VOICE_FILE).onnx; \
		curl -L --fail --progress-bar -o $(PIPER_VOICES)/$(VOICE_FILE).onnx.json $(VOICE_JSON_URL); \
	else \
		echo "[tts] voice $(VOICE_FILE) already present — skipping"; \
	fi
	@echo "[tts] setup done. Piper launches on demand inside solarium-ui-vk."

tts_server:
	@if [ ! -x $(PIPER_DIR)/piper ]; then \
		echo "[tts] piper not present — run 'make tts_setup' first" >&2; exit 1; \
	fi
	@echo "[tts] piper is spawned inside solarium-ui-vk. For a CLI smoke test:"
	@echo "      echo 'hello world' | $(PIPER_DIR)/piper --model $(PIPER_VOICES)/$(VOICE_FILE).onnx --output_file /tmp/piper_test.wav"

tts_stop:
	@-pgrep -x piper 2>/dev/null | xargs -r kill ; true
	@echo "[tts] piper stopped."

# ── InstantMesh sidecar (image → 3D mesh) ──────────────────────────────────
#
# TencentARC InstantMesh — single image → textured GLB in ~10 s on a CUDA
# GPU with ≥ 8 GB VRAM. Apache 2.0. Per-request subprocess (no daemon)
# because it pins 8 GB of VRAM while loaded and we run it occasionally.
#
# First-time `imageto3d_smoke` downloads ~5 GB of model weights from
# HuggingFace into HF cache. Subsequent runs are fast.
#
# Pipeline: photo.png → InstantMesh → GLB → voxel_earth/voxelizer →
#           Solarium box-list model.py. See docs/MODEL_PIPELINE.md.
INSTANTMESH_DIR  := $(LLM_DIR)/imageto3d
INSTANTMESH_REPO := https://github.com/TencentARC/InstantMesh
INSTANTMESH_VENV := $(INSTANTMESH_DIR)/venv
INSTANTMESH_OUT  := /tmp/imageto3d

imageto3d_setup:
	@if ! command -v python3 >/dev/null 2>&1; then \
		echo "[imageto3d] python3 not found" >&2; exit 1; \
	fi
	@if ! command -v nvidia-smi >/dev/null 2>&1; then \
		echo "[imageto3d] WARNING: nvidia-smi not found — InstantMesh requires CUDA."; \
		echo "[imageto3d]          Setup will proceed but smoke test will fail."; \
	else \
		echo "[imageto3d] GPU detected:"; nvidia-smi --query-gpu=name,memory.total --format=csv,noheader | sed 's/^/[imageto3d]   /'; \
	fi
	@mkdir -p $(LLM_DIR)
	@if [ ! -d $(INSTANTMESH_DIR)/.git ]; then \
		echo "[imageto3d] cloning InstantMesh into $(INSTANTMESH_DIR)…"; \
		git clone --depth 1 $(INSTANTMESH_REPO) $(INSTANTMESH_DIR); \
	else \
		echo "[imageto3d] InstantMesh already cloned — skipping"; \
	fi
	@if [ ! -f $(INSTANTMESH_VENV)/bin/activate ]; then \
		if [ -d $(INSTANTMESH_VENV) ]; then \
			echo "[imageto3d] venv directory exists but is incomplete — recreating"; \
			rm -rf $(INSTANTMESH_VENV); \
		fi; \
		if [ -n "$$VIRTUAL_ENV" ]; then \
			echo "[imageto3d] note: detected active venv ($$VIRTUAL_ENV) — unsetting VIRTUAL_ENV for clean creation"; \
		fi; \
		echo "[imageto3d] creating venv at $(INSTANTMESH_VENV)…"; \
		unset VIRTUAL_ENV PYTHONHOME && python3 -m venv $(INSTANTMESH_VENV) \
			|| (echo "[imageto3d] venv creation failed — install python3-venv (e.g. apt install python3-venv)" >&2; exit 1); \
		if [ ! -f $(INSTANTMESH_VENV)/bin/activate ]; then \
			echo "[imageto3d] venv created but activate script missing — likely venv-in-venv bug." >&2; \
			echo "[imageto3d]   try 'deactivate' first, then re-run 'make imageto3d_setup'." >&2; \
			exit 1; \
		fi; \
		. $(INSTANTMESH_VENV)/bin/activate && pip install --upgrade pip wheel; \
	else \
		echo "[imageto3d] venv already present — skipping"; \
	fi
	@if ! command -v nvcc >/dev/null 2>&1; then \
		echo "[imageto3d] ERROR: nvcc not found." >&2; \
		echo "[imageto3d]   nvdiffrast (an InstantMesh dep) compiles CUDA from source and needs nvcc." >&2; \
		echo "[imageto3d]   Install with: sudo apt install nvidia-cuda-toolkit  (~3 GB, gets CUDA 12.0)" >&2; \
		echo "[imageto3d]   Then re-run 'make imageto3d_setup'." >&2; \
		exit 1; \
	fi
	@echo "[imageto3d] nvcc detected: $$(nvcc --version | tail -1)"
	@if [ ! -x /usr/bin/g++-12 ]; then \
		echo "[imageto3d] ERROR: g++-12 not found at /usr/bin/g++-12." >&2; \
		echo "[imageto3d]   CUDA 12.0 rejects g++ >= 13 as a host compiler. Ubuntu 24.04 ships g++-13" >&2; \
		echo "[imageto3d]   by default, so we need g++-12 on the side just for nvdiffrast's nvcc invocation." >&2; \
		echo "[imageto3d]   Install with: sudo apt install g++-12  (~80 MB)" >&2; \
		echo "[imageto3d]   Then re-run 'make imageto3d_setup'." >&2; \
		exit 1; \
	fi
	@echo "[imageto3d] installing dependencies (cu126 PyTorch wheel — keeps Pascal sm_61 support)…"
	@echo "[imageto3d]   override: set INSTANTMESH_TORCH_INDEX before re-running"
	@echo "[imageto3d]   PyTorch wheels: cu121 dropped from index, cu128/cu130 dropped sm_61, cu126 keeps both"
	@. $(INSTANTMESH_VENV)/bin/activate && \
		pip install --index-url $${INSTANTMESH_TORCH_INDEX:-https://download.pytorch.org/whl/cu126} \
			torch torchvision && \
		echo "[imageto3d] freezing torch versions as constraints to prevent transitive upgrade…" && \
		pip freeze | grep -E '^(torch|torchvision)==' > $(INSTANTMESH_DIR)/.torch_constraints.txt && \
		grep -v '^git+.*nvdiffrast' $(INSTANTMESH_DIR)/requirements.txt | \
			pip install -c $(INSTANTMESH_DIR)/.torch_constraints.txt -r /dev/stdin && \
		echo "[imageto3d] pinning accelerate to the era-contemporary 0.24.0 (transformers 4.34.1 forces hub 0.17, which modern accelerate cannot use)…" && \
		pip install -c $(INSTANTMESH_DIR)/.torch_constraints.txt 'accelerate==0.24.0' && \
		echo "[imageto3d] installing onnxruntime (rembg runtime dep, not in requirements.txt)…" && \
		pip install onnxruntime && \
		echo "[imageto3d] compiling nvdiffrast against installed torch (no build isolation, g++-12 host)…" && \
		CUDAHOSTCXX=/usr/bin/g++-12 CC=/usr/bin/gcc-12 CXX=/usr/bin/g++-12 \
			pip install --no-build-isolation 'git+https://github.com/NVlabs/nvdiffrast/'
	@echo ""
	@echo "[imageto3d] setup done."
	@echo "[imageto3d] First 'make imageto3d_smoke' downloads ~5 GB of weights from HuggingFace."

imageto3d_smoke:
	@if [ ! -d $(INSTANTMESH_VENV) ]; then \
		echo "[imageto3d] venv missing — run 'make imageto3d_setup' first" >&2; exit 1; \
	fi
	@if [ ! -f $(INSTANTMESH_DIR)/examples/hatsune_miku.png ]; then \
		echo "[imageto3d] smoke input examples/hatsune_miku.png missing — bad clone?" >&2; exit 1; \
	fi
	@if pgrep -x llama-server >/dev/null 2>&1; then \
		echo "[imageto3d] llama-server is running — VRAM contention likely."; \
		echo "[imageto3d]   stop it first with 'make llm_stop' if smoke OOMs."; \
	fi
	@mkdir -p $(INSTANTMESH_OUT)
	@echo "[imageto3d] running InstantMesh on examples/hatsune_miku.png …"
	@echo "[imageto3d]   config: $${INSTANTMESH_CONFIG:-instant-nerf-base}.yaml"
	@echo "[imageto3d]   ('-mesh-large' wants 15 GiB at FlexiCubes — too big for 11 GiB GPUs;"
	@echo "[imageto3d]    '-mesh-base' wants ~11 GiB peak — too tight for the 1080 Ti;"
	@echo "[imageto3d]    '-nerf-base' uses NeRF + marching cubes, fits in ~10 GiB. Default for now.)"
	@echo "[imageto3d]   --export_texmap disabled by default (adds 2-3 GiB peak; voxelizer"
	@echo "[imageto3d]    averages texels into voxels anyway). Set INSTANTMESH_TEXMAP=1 to opt in."
	@cd $(INSTANTMESH_DIR) && . venv/bin/activate && \
		PYTORCH_CUDA_ALLOC_CONF=expandable_segments:True \
		python run.py configs/$${INSTANTMESH_CONFIG:-instant-nerf-base}.yaml \
		    examples/hatsune_miku.png \
		    --output_path $(INSTANTMESH_OUT)/smoke \
		    $${INSTANTMESH_TEXMAP:+--export_texmap}
	@MESHES=$$(find $(INSTANTMESH_OUT)/smoke -type f \( -name '*.glb' -o -name '*.obj' -o -name '*.ply' \) 2>/dev/null); \
	if [ -n "$$MESHES" ]; then \
		echo "[imageto3d] smoke OK. Output under $(INSTANTMESH_OUT)/smoke/:"; \
		echo "$$MESHES" | xargs -I{} ls -lh {} | awk '{print "[imageto3d]   " $$5 "  " $$NF}'; \
		ANY=$$(echo "$$MESHES" | head -1); \
		echo "[imageto3d] inspect with: blender $$ANY"; \
	else \
		echo "[imageto3d] smoke FAILED — no mesh produced. Check output above." >&2; exit 1; \
	fi

imageto3d_stop:
	@-pgrep -f "InstantMesh.*run.py" 2>/dev/null | xargs -r kill ; true
	@echo "[imageto3d] any running InstantMesh subprocess killed."

imageto3d_clean:
	rm -rf $(INSTANTMESH_DIR)
	rm -rf $(INSTANTMESH_OUT)
	@echo "[imageto3d] removed $(INSTANTMESH_DIR) and $(INSTANTMESH_OUT)."

# ── Bundled AI setup (all three sidecars) ──────────────────────────────────
# Single command to prepare a dialog-capable game: chat LLM, whisper STT,
# and piper TTS. Needed once; model files are cached under llm/.
ai_setup: llm_setup whisper_setup tts_setup
	@echo ""
	@echo "[ai] All three sidecars ready. 'make game' will spawn them automatically."

ai_stop: llm_stop whisper_stop tts_stop

ai_clean: llm_clean
	@echo "[ai] Everything under llm/ removed."
