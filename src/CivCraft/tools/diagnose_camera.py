#!/usr/bin/env python3
"""
Diagnose camera mode bugs by analyzing the C++ source code.
Traces cursor state, mouse processing, and crosshair logic per mode.
"""

import re, os, sys

SRC = os.path.join(os.path.dirname(__file__), '..', 'src')

def read(path):
    with open(os.path.join(SRC, path)) as f:
        return f.read()

def find_lines(src, pattern):
    return [(i+1, line.strip()) for i, line in enumerate(src.split('\n'))
            if re.search(pattern, line)]

print("=" * 70)
print("CAMERA MODE DIAGNOSTIC")
print("=" * 70)

# 1. Check cursor state transitions
print("\n--- Cursor State Transitions ---")
gameplay = read('game/gameplay.cpp')
for n, line in find_lines(gameplay, r'GLFW_CURSOR'):
    print(f"  gameplay.cpp:{n}: {line}")

camera = read('client/camera.cpp')
for n, line in find_lines(camera, r'GLFW_CURSOR'):
    print(f"  camera.cpp:{n}: {line}")

# Check game.cpp too
game = read('game/game.cpp')
for n, line in find_lines(game, r'GLFW_CURSOR'):
    print(f"  game.cpp:{n}: {line}")

# 2. Check processMouse per mode
print("\n--- Mouse Processing Per Mode ---")
mouse_section = re.search(r'void Camera::processMouse.*?\n\}', camera, re.DOTALL)
if mouse_section:
    lines = mouse_section.group().split('\n')
    current_mode = None
    for line in lines:
        if 'FirstPerson' in line: current_mode = 'FPS'
        elif 'ThirdPerson' in line: current_mode = 'TPS'
        elif 'RPG' in line and 'case' in line: current_mode = 'RPG'
        elif 'RTS' in line: current_mode = 'RTS'
        if current_mode and ('yaw' in line.lower() or 'pitch' in line.lower() or 'break' in line):
            print(f"  [{current_mode}] {line.strip()}")
            if 'break' in line: current_mode = None

# 3. Check when processMouse is called
print("\n--- When is processMouse called? ---")
for n, line in find_lines(camera, r'processMouse|processInput'):
    print(f"  camera.cpp:{n}: {line}")
for n, line in find_lines(gameplay, r'processInput|processMouse'):
    print(f"  gameplay.cpp:{n}: {line}")

# 4. Check cursor state vs UI overlay
print("\n--- UI Overlay Cursor Handling ---")
for n, line in find_lines(game, r'wantsMouse|wantsKeyboard|showInventory|CURSOR'):
    print(f"  game.cpp:{n}: {line}")
for n, line in find_lines(gameplay, r'wantsMouse|wantsKeyboard'):
    print(f"  gameplay.cpp:{n}: {line}")

# 5. Check TPS player.yaw assignment
print("\n--- TPS Player Yaw (Bug: should NOT set yaw to orbit) ---")
for n, line in find_lines(camera, r'player\.yaw'):
    print(f"  camera.cpp:{n}: {line}")

# 6. Check crosshair rendering
print("\n--- Crosshair Logic ---")
renderer = read('client/renderer.cpp')
for n, line in find_lines(renderer, r'crosshair|Crosshair'):
    print(f"  renderer.cpp:{n}: {line}")

# 7. RPG mode right-click handling
print("\n--- RPG Right-Click Camera ---")
for n, line in find_lines(gameplay, r'RPG|right.*click|rmb|MOUSE_BUTTON_RIGHT'):
    print(f"  gameplay.cpp:{n}: {line}")

# 8. Summary
print("\n" + "=" * 70)
print("BUG SUMMARY")
print("=" * 70)

# Check if cursor is ever freed when inventory opens
inv_cursor = any('showInventory' in line and 'CURSOR' in line
                 for _, line in find_lines(game, r'showInventory.*CURSOR|CURSOR.*showInventory'))
print(f"\n[BUG 1] Cursor freed when inventory opens: {'YES' if inv_cursor else 'NO ← BUG'}")

# Check if TPS sets player.yaw = orbitYaw
tps_yaw_bug = any('player.yaw = orbitYaw' in line for _, line in find_lines(camera, r'player\.yaw.*orbit'))
print(f"[BUG 2] TPS forces player.yaw = orbitYaw: {'YES ← BUG' if tps_yaw_bug else 'NO (fixed)'}")

# Check if RPG always captures cursor
rpg_always_capture = not any('RPG' in line and 'CURSOR_NORMAL' in line
                             for _, line in find_lines(gameplay, r'RPG.*CURSOR_NORMAL'))
print(f"[BUG 3] RPG always captures cursor: {'YES ← BUG' if rpg_always_capture else 'NO (WoW-style free cursor)'}")

# Check if RTS cursor is visible
rts_normal = any('RTS' in line and 'CURSOR_NORMAL' in line
                 for _, line in find_lines(gameplay, r'RTS.*CURSOR_NORMAL'))
print(f"[BUG 4] RTS shows cursor: {'YES' if rts_normal else 'NO ← BUG'}")

# Check if processInput skips mouse for RTS
rts_skip = any('RTS' in line for _, line in find_lines(camera, r'mode.*RTS'))
print(f"[BUG 5] RTS skips mouse processing: {'YES' if rts_skip else 'NO ← BUG'}")
