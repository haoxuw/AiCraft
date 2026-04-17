#!/bin/bash
# capture_samples.sh — drive civcraft-ui-vk through representative game
# states and capture one PNG per state into $OUT_DIR.
#
# Uses only file-based debug triggers (no xdotool / keyboard injection):
#   /tmp/civcraft_screenshot_request         — take a screenshot
#   /tmp/civcraft_vk_camera_request          — cycle FPS→TPS→RPG→RTS
#   /tmp/civcraft_vk_inventory_request       — toggle inventory panel
#   /tmp/civcraft_vk_pause_request           — toggle pause
#   /tmp/civcraft_vk_admin_request           — toggle admin
#   /tmp/civcraft_vk_fly_request             — toggle fly (requires admin)
#   /tmp/civcraft_vk_ascend_request          — +5 y (requires fly)
#
# File triggers are compiled out in Release — the caller must pass a Debug
# build dir (the Makefile uses $(BUILD_DIR), which is Debug by default).
#
# Usage: capture_samples.sh <build-dir> <out-dir>

set -u
BUILD_DIR="${1:-build}"
OUT_DIR="${2:-/tmp/civcraft_samples}"
BIN="$BUILD_DIR/civcraft-ui-vk"

if [[ ! -x "$BIN" ]]; then
	echo "[samples] $BIN not found — run 'make build' first" >&2
	exit 1
fi

log() { echo "[samples] $*"; }

kill_game() {
	for p in $(pgrep -x civcraft-ui-vk 2>/dev/null); do kill -9 "$p" 2>/dev/null || true; done
	for p in $(pgrep -x civcraft-server 2>/dev/null); do kill -9 "$p" 2>/dev/null || true; done
	# Small grace period so the OS reaps the processes and the port frees.
	sleep 0.4
}

# Touch a trigger file and wait briefly so the game's next frame picks it up.
trigger() {
	local req="$1"; local wait_s="${2:-0.3}"
	touch "$req"
	sleep "$wait_s"
}

# Shoot a screenshot and rename the resulting .ppm to a stable, labeled path.
# The VK screenshot writer names files sequentially: civcraft_vk_screenshot_N.ppm.
# We bump $SHOT_N ourselves to track which file corresponds to which label.
SHOT_N=0
shoot() {
	local label="$1"
	rm -f /tmp/civcraft_screenshot_request
	touch /tmp/civcraft_screenshot_request
	# Wait for the game to pick up the trigger and write the PPM. Poll for
	# the file to exist so we don't race with slow frames on loaded systems.
	local src="/tmp/civcraft_vk_screenshot_${SHOT_N}.ppm"
	# Poll until the file exists AND its size has been stable for two samples
	# in a row — the game writes ~3MB asynchronously, so `-e` alone returns
	# true while the file is still being filled. A partial PPM makes PIL
	# raise "image file is truncated" and the .png conversion silently fails.
	local prev_size=-1 cur_size=-1 stable=0
	for _ in $(seq 1 40); do
		if [[ -e "$src" ]]; then
			cur_size=$(stat -c%s "$src" 2>/dev/null || echo 0)
			if [[ "$cur_size" -gt 0 && "$cur_size" -eq "$prev_size" ]]; then
				stable=1; break
			fi
			prev_size="$cur_size"
		fi
		sleep 0.1
	done
	if [[ "$stable" -ne 1 ]]; then
		log "WARN: screenshot never stabilized for '$label' (expected $src, size=$cur_size)"
		SHOT_N=$((SHOT_N+1))
		return 1
	fi
	local dst_ppm="$OUT_DIR/${label}.ppm"
	mv "$src" "$dst_ppm"
	# Convert to PNG (smaller, viewable in any browser).
	python3 - "$dst_ppm" <<'PY' 2>/dev/null || true
import sys, os
from PIL import Image
src = sys.argv[1]
dst = os.path.splitext(src)[0] + ".png"
Image.open(src).save(dst)
os.remove(src)
PY
	log "  → ${label}.png"
	SHOT_N=$((SHOT_N+1))
}

mkdir -p "$OUT_DIR"
rm -f "$OUT_DIR"/*.png "$OUT_DIR"/*.ppm
rm -f /tmp/civcraft_vk_screenshot_*.ppm
kill_game

# ── Phase 1: main menu ───────────────────────────────────────────────────
# No --skip-menu: the MENU state renders title + buttons. Give it ~2.5s to
# finish fade-in / title animation before shooting.
log "Phase 1: main menu"
# CIVCRAFT_NO_FOCUS_PAUSE=1 keeps gameplay ticking when the window isn't the
# active X window — without it the game auto-pauses the moment our shell
# grabs focus, and every "gameplay" shot comes out as the PAUSED overlay.
( cd "$BUILD_DIR" && CIVCRAFT_NO_FOCUS_PAUSE=1 ./civcraft-ui-vk ) >/tmp/civcraft_samples_phase1.log 2>&1 &
PHASE1_PID=$!
sleep 3.0
shoot "01_main_menu"
kill -9 "$PHASE1_PID" 2>/dev/null || true
kill_game
SHOT_N=0  # new process → screenshot counter resets

# ── Phase 2: gameplay + UI panels ────────────────────────────────────────
# --skip-menu drops straight into PLAYING. World streams over TCP so give
# the server generous time (village template) before the first shot.
log "Phase 2: gameplay"
( cd "$BUILD_DIR" && CIVCRAFT_NO_FOCUS_PAUSE=1 ./civcraft-ui-vk --skip-menu ) >/tmp/civcraft_samples_phase2.log 2>&1 &
PHASE2_PID=$!
sleep 7.0   # world gen + chunk streaming + mesh upload

shoot "02_gameplay_fps"

trigger /tmp/civcraft_vk_camera_request 0.6
shoot "03_gameplay_tps"

trigger /tmp/civcraft_vk_camera_request 0.6
shoot "04_gameplay_rpg"

trigger /tmp/civcraft_vk_camera_request 0.6
shoot "05_gameplay_rts"

# Back to FPS (one more cycle wraps around).
trigger /tmp/civcraft_vk_camera_request 0.6
shoot "06_gameplay_fps_again"

# Inventory panel — Tab-equivalent trigger.
trigger /tmp/civcraft_vk_inventory_request 0.4
shoot "07_inventory_open"
trigger /tmp/civcraft_vk_inventory_request 0.4  # close

# Pause overlay.
trigger /tmp/civcraft_vk_pause_request 0.4
shoot "08_paused"
trigger /tmp/civcraft_vk_pause_request 0.4  # resume

# Admin fly — gives a top-down peek at the village.
trigger /tmp/civcraft_vk_admin_request 0.3
trigger /tmp/civcraft_vk_fly_request   0.3
for _ in 1 2 3 4 5 6; do trigger /tmp/civcraft_vk_ascend_request 0.15; done
trigger /tmp/civcraft_vk_camera_request 0.6   # RPG gives a clean overhead
trigger /tmp/civcraft_vk_camera_request 0.6   # RTS — bird's eye
shoot "09_admin_overview"

kill -9 "$PHASE2_PID" 2>/dev/null || true
kill_game

log "Done. Samples in $OUT_DIR/"
ls -1 "$OUT_DIR"/ 2>/dev/null | sed 's/^/  /'
