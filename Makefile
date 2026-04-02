BUILD_DIR := build
BUILD_WEB := build-web
BUILD_TYPE := Debug
HOST := 127.0.0.1
PORT := 7777
WEB_PORT := 8080
EMSDK := $(HOME)/emsdk

# If a port number is passed as an extra arg (e.g., make game 7890),
# treat it as PORT and suppress "no rule" error
ifneq ($(filter-out game play server client build configure clean stop web web-build web-configure web-clean,$(MAKECMDGOALS)),)
  PORT := $(filter-out game play server client build configure clean stop web web-build web-configure web-clean,$(MAKECMDGOALS))
endif

# Suppress "no rule to make target '7890'" error
%:
	@:

.PHONY: game play build configure clean server client stop web web-build web-configure web-clean

# ── Native ─────────────────────────────────────────────────
#
# Quick reference:
#   make game             Singleplayer (server + client in one process)
#   make game 7890        LAN debug: server + client on port 7890, skip menu
#   make play             LAN debug on default port (7777)
#   make server           Dedicated server (interactive world select)
#   make server 7890      Dedicated server on port 7890
#   make client           Network client → localhost:7777
#   make client 7890      Network client → localhost:7890
#   make stop             Kill all agentworld processes
#
# Multiplayer (separate terminals):
#   Terminal 1: make server 7777
#   Terminal 2: make client 7777
#   Terminal 3: make client 7777   (second player)
#

# Singleplayer or LAN debug
# - make game      → singleplayer with menu
# - make game 7890 → server on 7890 + client auto-joins, skips menu
game: build
ifeq ($(PORT),7777)
	@-pkill -x "agentworld" 2>/dev/null; sleep 0.5
	./$(BUILD_DIR)/agentworld
else
	@-pkill -x "agentworld-server" 2>/dev/null; sleep 0.3
	@echo "[make] LAN debug: server on port $(PORT)..."
	@./$(BUILD_DIR)/agentworld-server --port $(PORT) --template 1 --seed 42 &
	@sleep 1
	@echo "[make] Client → 127.0.0.1:$(PORT)..."
	./$(BUILD_DIR)/agentworld-client --host 127.0.0.1 --port $(PORT)
	@echo "[make] Stopping server..."
	@-pkill -x "agentworld-server" 2>/dev/null
endif

# LAN debug on default port
play: build
	@-pkill -x "agentworld-server" 2>/dev/null; sleep 0.3
	@echo "[make] Starting server on port $(PORT)..."
	@./$(BUILD_DIR)/agentworld-server --port $(PORT) --template 1 --seed 42 &
	@sleep 1
	@echo "[make] Client → 127.0.0.1:$(PORT)..."
	./$(BUILD_DIR)/agentworld-client --host 127.0.0.1 --port $(PORT)
	@echo "[make] Stopping server..."
	@-pkill -x "agentworld-server" 2>/dev/null

# Dedicated server
server: build
	@-pkill -x "agentworld-server" 2>/dev/null; sleep 0.5
	./$(BUILD_DIR)/agentworld-server --port $(PORT)

# Network client
client: build
	./$(BUILD_DIR)/agentworld-client --host $(HOST) --port $(PORT)

# Kill everything
stop:
	@-pkill -f "agentworld" 2>/dev/null; sleep 1
	@echo "All agentworld processes stopped."

# Kill only servers (find by listening port)
killservers:
	@echo "Looking for agentworld server processes..."
	@-pgrep -fa "agentworld-server" 2>/dev/null && pkill -f "agentworld-server" && echo "Killed." || echo "No servers running."
	@-pgrep -fa "agentworld.*--port" 2>/dev/null && pkill -f "agentworld.*--port" && echo "Killed port processes." || true

build: configure
	cmake --build $(BUILD_DIR) -j$$(nproc)

configure:
	@if [ ! -f $(BUILD_DIR)/CMakeCache.txt ]; then \
		cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE); \
	fi

clean:
	rm -rf $(BUILD_DIR)

# ── Web (WASM + WebGL) ────────────────────────────────────

web: web-build
	python3 tools/serve_web.py $(WEB_PORT) $(BUILD_WEB)

web-build: web-configure
	cmake --build $(BUILD_WEB) -j$$(nproc)

web-configure:
	@if [ ! -f $(BUILD_WEB)/CMakeCache.txt ]; then \
		. $(EMSDK)/emsdk_env.sh 2>/dev/null && \
		emcmake cmake -B $(BUILD_WEB) -DCMAKE_BUILD_TYPE=Release; \
	fi

web-clean:
	rm -rf $(BUILD_WEB)
