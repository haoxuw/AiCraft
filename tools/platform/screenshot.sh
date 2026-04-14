#!/bin/bash
# Take a screenshot of CivCraft at a specific game state.
# Usage: ./tools/screenshot.sh [label] [timeout_seconds]
#
# Starts the game in demo mode, captures screenshots, converts to PNG.
# Results go to /tmp/civcraft_test_*.png

LABEL="${1:-test}"
TIMEOUT="${2:-12}"
BUILD="./build/civcraft"

echo "=== CivCraft Screenshot: $LABEL ==="

# Clean old screenshots
rm -f /tmp/civcraft_view_*.ppm /tmp/civcraft_menu_screenshot.ppm

# Run demo
timeout "$TIMEOUT" "$BUILD" --demo 2>/dev/null 1>/dev/null

# Convert all screenshots to PNG
python3 -c "
from PIL import Image
import glob, os
files = sorted(glob.glob('/tmp/civcraft_*.ppm'))
for f in files:
    name = os.path.basename(f).replace('.ppm', '')
    out = f'/tmp/civcraft_${LABEL}_{name}.png'
    Image.open(f).resize((800, 450)).save(out)
    print(f'  {out}')
print(f'Total: {len(files)} screenshots')
" 2>/dev/null

echo "Done."
