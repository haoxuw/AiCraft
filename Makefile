SHELL := /bin/bash
BUILD_DIR := build
GAME_BUILD_DIR := build-perf
# NOTE: both trees currently use Debug until the Release-mode world-gen crash
# in ConfigurableWorldTemplate::generate is fixed (recursive generateChunk
# via getBlock in initWorld's lambda — SIGSEGVs in Release, hangs silently in
# RelWithDebInfo). Once that's resolved, flip `make client`/`make server` to
# Release and `make game` to RelWithDebInfo+CIVCRAFT_PERF=ON for representative
# perf numbers.
BUILD_TYPE := Debug
HOST :=
PORT := 7777
GAME_PORT :=
GAME := civcraft

# Parallelism for cmake --build. Defaults to HALF the core count so a build
# doesn't pin the machine / trigger OOM on big templated sources. Override on
# the command line, e.g. `make build PAR=8` or `make build PAR=1`.
PAR := $(shell nproc 2>/dev/null | awk '{n=int($$1/2); print (n<1)?1:n}')

.PHONY: game game-build game-configure build configure clean server client stop test_e2e proxy test-dog test-villager test-chicken toronto world debug_villager profiler killservers character_views item_views model-editor model-snap animation_sweep test_animation download_music jukebox civcraft crafter bbmodel sample pathfinding_test perf_fps perf_server flamegraph llm_setup llm_server llm_stop llm_clean whisper_setup whisper_server whisper_stop tts_setup tts_server tts_stop ai_setup ai_stop ai_clean cef_setup cef_clean cef_demo demo

# ── Native (CivCraft) ───────────────────────────────────────
#
# CivCraft is a native Vulkan C++ voxel sandbox.
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

civcraft: game

# `make game N` → spawn N villagers. Captures the first positional arg after
# `game` when it's all digits, stamps it into GAME_VILLAGERS, and registers
# the digit-word as a no-op phony target so make doesn't bail on "no rule to
# make target '100'". Pure client-side — the client sets CIVCRAFT_VILLAGERS
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
GAME_LOG  := /tmp/civcraft_game_run.log
PERF_DATA := /tmp/civcraft.perf.data
PERF_REPORT := /tmp/civcraft_perf_report.txt

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
	    ./civcraft-ui-vk --skip-menu --cef-menu $(1) 2>&1 | tee $(GAME_LOG); \
	else \
	    rm -f $(PERF_DATA); \
	    echo "[make] launching under perf — data: $(PERF_DATA), log: $(GAME_LOG)"; \
	    perf record -F 99 --call-graph dwarf -o $(PERF_DATA) \
	        -- ./civcraft-ui-vk --skip-menu --cef-menu $(1) 2>&1 | tee $(GAME_LOG); \
	    echo; echo "=========== perf report (top-30) ==========="; \
	    perf report --stdio --no-children -g none -i $(PERF_DATA) 2>/dev/null | head -60 | tee $(PERF_REPORT); \
	    if command -v flamegraph.pl >/dev/null && command -v stackcollapse-perf.pl >/dev/null; then \
	        ts=$$(date '+%Y%m%d_%H%M%S'); \
	        perf script -i $(PERF_DATA) 2>/dev/null | stackcollapse-perf.pl > /tmp/civcraft_folded.txt && \
	        flamegraph.pl --title="civcraft combined $$ts" < /tmp/civcraft_folded.txt \
	            > /tmp/civcraft_flamegraph_combined_$$ts.svg && \
	        grep '^civcraft-ui-vk;'  /tmp/civcraft_folded.txt | \
	            flamegraph.pl --title="civcraft-ui-vk (client) $$ts" \
	            > /tmp/civcraft_flamegraph_client_$$ts.svg && \
	        grep '^civcraft-server;' /tmp/civcraft_folded.txt | \
	            flamegraph.pl --title="civcraft-server (server) $$ts" \
	            > /tmp/civcraft_flamegraph_server_$$ts.svg && \
	        rm -f /tmp/civcraft_folded.txt && \
	        ln -sf /tmp/civcraft_flamegraph_combined_$$ts.svg /tmp/civcraft_flamegraph.svg && \
	        ln -sf /tmp/civcraft_flamegraph_client_$$ts.svg   /tmp/civcraft_flamegraph_client.svg && \
	        ln -sf /tmp/civcraft_flamegraph_server_$$ts.svg   /tmp/civcraft_flamegraph_server.svg; \
	    else \
	        echo "[make] flamegraph.pl not in PATH — install FlameGraph to get /tmp/civcraft_flamegraph*.svg:"; \
	        echo "         git clone https://github.com/brendangregg/FlameGraph ~/FlameGraph"; \
	        echo "         export PATH=\$$PATH:~/FlameGraph"; \
	    fi; \
	fi; \
	echo; echo "=========== make game artifacts ==========="; \
	for f in $$(ls -t /tmp/civcraft_perf_client_*.txt 2>/dev/null | head -1); do \
	    echo "  perf summary (client):  $$f"; \
	done; \
	for f in $$(ls -t /tmp/civcraft_perf_server_*.txt 2>/dev/null | head -1); do \
	    echo "  perf summary (server):  $$f"; \
	done; \
	for f in $$(ls -t /tmp/civcraft_flamegraph_client_*.svg 2>/dev/null | head -1); do \
	    echo "  flamegraph (client):    $$f"; \
	done; \
	for f in $$(ls -t /tmp/civcraft_flamegraph_server_*.svg 2>/dev/null | head -1); do \
	    echo "  flamegraph (server):    $$f"; \
	done; \
	for f in $$(ls -t /tmp/civcraft_flamegraph_combined_*.svg 2>/dev/null | head -1); do \
	    echo "  flamegraph (combined):  $$f"; \
	done; \
	if ls -t /tmp/civcraft_flamegraph_client_*.svg >/dev/null 2>&1; then \
	    echo "                          (also: /tmp/civcraft_flamegraph_{client,server,combined}.svg → newest)"; \
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
	count=$$(ls /tmp/civcraft_entity_*.log 2>/dev/null | wc -l); \
	if [ "$$count" -gt 0 ]; then \
	    echo "  entity logs ($$count files):"; \
	    ls -1 /tmp/civcraft_entity_*.log | sed 's/^/    /'; \
	fi; \
	}
endef

# Common wrapper: $(call run_under_gdb, <extra civcraft-ui-vk args>)
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
	    --args ./civcraft-ui-vk --skip-menu --cef-menu $(1) 2>&1 | tee $(GAME_LOG)
endef

# Bare invocation for PROFILE=none.
define run_bare
	cd $(GAME_BUILD_DIR) && \
	echo "[make] launching bare — log: $(GAME_LOG)" && \
	./civcraft-ui-vk --skip-menu --cef-menu $(1) 2>&1 | tee $(GAME_LOG)
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
	cd $(GAME_BUILD_DIR) && ./civcraft-ui-vk --cef-menu 2>&1 | tee $(GAME_LOG)

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

# Voxel Earth demo: walk around a baked slab of Toronto.
# First-time setup (one-off):
#   python -m voxel_earth set-key <YOUR_GOOGLE_MAPS_KEY>
#   python -m voxel_earth download --location "Toronto" --radius 100
#   ./build/civcraft-voxel-bake --glb-dir ~/.voxel/google/glb \
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
# hidden window, tee event stream to /tmp/civcraft_game.log. Use for
# behavior/pathfinding/decision iteration — see the testing-plan skill.
# DURATION=N (seconds) ends the run early; default is unbounded.
DEBUG_DURATION ?=
debug_villager: debug_villager-reconfigure game-build
	@-pgrep -x civcraft-ui-vk  2>/dev/null | xargs -r kill ; true
	@-pgrep -x civcraft-server 2>/dev/null | xargs -r kill ; true
	@sleep 1
	@rm -f /tmp/civcraft_entity_*.log
	@echo "[debug_villager] --debug-behavior (1 villager, sim-speed 4, log-only)"
	@echo "[debug_villager] log: /tmp/civcraft_game.log (prior run kept as .prev)"
	@echo "[debug_villager] per-entity logs: /tmp/civcraft_entity_<id>.log (cleared on launch)"
	@cd $(GAME_BUILD_DIR) && { \
	    $(if $(DEBUG_DURATION),timeout $(DEBUG_DURATION) )./civcraft-ui-vk --debug-behavior; \
	    rc=$$?; \
	    $(if $(DEBUG_DURATION),[ $$rc -eq 124 ] && rc=0;) \
	    exit $$rc; \
	 }

# ── Visual QA scenarios ─────────────────────────────────────
CHARACTER := base:pig
ITEM      := base:sword
CLIP      :=

character_views: build
	cd $(BUILD_DIR) && ./civcraft-ui-vk --skip-menu --cef-menu \
	    --debug-scenario character_views --debug-character $(CHARACTER) \
	    $(if $(CLIP),--debug-clip $(CLIP))

item_views: build
	cd $(BUILD_DIR) && ./civcraft-ui-vk --skip-menu --cef-menu \
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
	    ( cd $(BUILD_DIR) && timeout 30 ./civcraft-ui-vk --skip-menu --cef-menu \
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

# ── Perf breakdown tables ──────────────────────────────────────────────
# Spin a civcraft-ui-vk session (which auto-spawns a civcraft-server),
# wait PERF_DURATION seconds, then parse the histograms each binary writes
# to /tmp/civcraft_perf_{client,server}_<ts>.txt into a percent-breakdown
# table. Overrides:
#   PERF_DURATION=120  sample window (seconds)
#   PERF_TEMPLATE=5    world template index (5 = perf_stress, 100 villagers)
PERF_DURATION ?= 60
PERF_TEMPLATE ?= 0
PERF_REPORT_PY := src/platform/debug/perf_report.py

define run_perf_session
	@-pgrep -x civcraft-ui-vk  2>/dev/null | xargs -r kill ; true
	@-pgrep -x civcraft-server 2>/dev/null | xargs -r kill ; true
	@sleep 1
	@echo "[$(1)] running civcraft-ui-vk (template=$(PERF_TEMPLATE)) for $(PERF_DURATION)s..."
	@cd $(GAME_BUILD_DIR) && timeout $(PERF_DURATION) \
	    ./civcraft-ui-vk --skip-menu --cef-menu --log-only --template $(PERF_TEMPLATE) \
	    > /tmp/civcraft_perf_$(1).stdout 2> /tmp/civcraft_perf_$(1).stderr ; \
	    true
	@-pgrep -x civcraft-ui-vk  2>/dev/null | xargs -r kill ; true
	@-pgrep -x civcraft-server 2>/dev/null | xargs -r kill ; true
	@sleep 1
endef

perf_fps: game-build
	$(call run_perf_session,fps)
	@dump=$$(ls -t /tmp/civcraft_perf_client_*.txt 2>/dev/null | head -1); \
	if [ -z "$$dump" ]; then \
	    echo "[perf_fps] no client dump — session may have crashed pre-Playing."; \
	    echo "[perf_fps] see /tmp/civcraft_perf_fps.stderr"; \
	    exit 1; \
	fi; \
	echo "[perf_fps] parsing $$dump"; echo; \
	python3 $(PERF_REPORT_PY) client "$$dump"

perf_server: game-build
	$(call run_perf_session,server)
	@dump=$$(ls -t /tmp/civcraft_perf_server_*.txt 2>/dev/null | head -1); \
	if [ -z "$$dump" ]; then \
	    echo "[perf_server] no server dump — session may have crashed early."; \
	    echo "[perf_server] see /tmp/civcraft_perf_server.stderr"; \
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
	cli=$$(ls -t /tmp/civcraft_flamegraph_client_*.svg 2>/dev/null | sed -n "$${n}p"); \
	srv=$$(ls -t /tmp/civcraft_flamegraph_server_*.svg 2>/dev/null | sed -n "$${n}p"); \
	cmb=$$(ls -t /tmp/civcraft_flamegraph_combined_*.svg 2>/dev/null | sed -n "$${n}p"); \
	if [ -z "$$cli" ] && [ -z "$$srv" ] && [ -z "$$cmb" ]; then \
	    echo "[flamegraph] run #$$n not found."; \
	    total=$$(ls /tmp/civcraft_flamegraph_client_*.svg 2>/dev/null | wc -l); \
	    if [ "$$total" -gt 0 ]; then \
	        echo "[flamegraph] $$total run(s) on disk (newest first):"; \
	        ls -lt /tmp/civcraft_flamegraph_client_*.svg | sed 's/^/  /'; \
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
		cmake -B $(GAME_BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DCIVCRAFT_PERF=ON -DCIVCRAFT_PATHFINDING_DEBUG=ON; \
	fi

# debug_villager needs PATHLOG compiled in so per-entity logs capture the
# navigator lifecycle. Force a reconfigure if the existing cache was built
# without it — cheap when the flag is already set.
debug_villager-reconfigure:
	@if ! grep -q "CIVCRAFT_PATHFINDING_DEBUG:BOOL=ON" $(GAME_BUILD_DIR)/CMakeCache.txt 2>/dev/null; then \
		echo "[debug_villager] enabling CIVCRAFT_PATHFINDING_DEBUG on $(GAME_BUILD_DIR)"; \
		cmake -B $(GAME_BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DCIVCRAFT_PERF=ON -DCIVCRAFT_PATHFINDING_DEBUG=ON; \
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
# 127.0.0.1:8080 — civcraft-ui-vk's DialogPanel connects here when you press
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
# tarball). civcraft-ui-vk links libcef.so + libcef_dll_wrapper, and CEF's
# subprocess children re-exec ./civcraft-cef-subprocess (built alongside).
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

# ── Whisper.cpp sidecar (speech-to-text for dialog input) ───────────────────
#
# `make whisper_setup` builds whisper.cpp's HTTP server binary and downloads
# one ggml model into llm/whisper_models/. civcraft-ui-vk auto-spawns the
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
# binary + one voice model, and civcraft-ui-vk spawns it in "JSON stdin →
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
	@echo "[tts] setup done. Piper launches on demand inside civcraft-ui-vk."

tts_server:
	@if [ ! -x $(PIPER_DIR)/piper ]; then \
		echo "[tts] piper not present — run 'make tts_setup' first" >&2; exit 1; \
	fi
	@echo "[tts] piper is spawned inside civcraft-ui-vk. For a CLI smoke test:"
	@echo "      echo 'hello world' | $(PIPER_DIR)/piper --model $(PIPER_VOICES)/$(VOICE_FILE).onnx --output_file /tmp/piper_test.wav"

tts_stop:
	@-pgrep -x piper 2>/dev/null | xargs -r kill ; true
	@echo "[tts] piper stopped."

# ── Bundled AI setup (all three sidecars) ──────────────────────────────────
# Single command to prepare a dialog-capable game: chat LLM, whisper STT,
# and piper TTS. Needed once; model files are cached under llm/.
ai_setup: llm_setup whisper_setup tts_setup
	@echo ""
	@echo "[ai] All three sidecars ready. 'make game' will spawn them automatically."

ai_stop: llm_stop whisper_stop tts_stop

ai_clean: llm_clean
	@echo "[ai] Everything under llm/ removed."
