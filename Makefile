SHELL := /bin/bash
BUILD_DIR := build
BUILD_WEB := build-web
BUILD_TYPE := Debug
HOST := 127.0.0.1
PORT := 7777
WEB_PORT := 8080
EMSDK := $(HOME)/emsdk

.PHONY: game play build configure clean server client stop test_e2e web web-build web-configure web-clean

# ── Native ─────────────────────────────────────────────────
#
# Quick reference:
#   make game             New village world on a random port (singleplayer)
#   make play             Same, but on fixed port 7777 (easy for a second client to join)
#   make server           Dedicated server (interactive world select)
#   make server PORT=N    Dedicated server on port N
#   make client           Network client → localhost:7777
#   make client PORT=N    Network client → localhost:N
#   make stop             Kill all agentworld processes
#
# Multiplayer (separate terminals):
#   Terminal 1: make play
#   Terminal 2: make client
#   Terminal 3: make client   (second player)
#

# Singleplayer: new village world on a random port, server + client, auto-cleanup
game: build
	@P=$$((7800 + $$RANDOM % 100)); rm -f /tmp/agentworld_ready_$$P; ./$(BUILD_DIR)/agentworld-server --port $$P --template 1 & SERVER=$$! && until [ -f /tmp/agentworld_ready_$$P ]; do sleep 0.05; done && rm -f /tmp/agentworld_ready_$$P && ./$(BUILD_DIR)/agentworld-client --host 127.0.0.1 --port $$P --skip-menu; kill $$SERVER 2>/dev/null

# Same as game but on fixed port 7777 — useful when a second client needs to join
play: build
	@rm -f /tmp/agentworld_ready_$(PORT); ./$(BUILD_DIR)/agentworld-server --port $(PORT) --template 1 & SERVER=$$! && until [ -f /tmp/agentworld_ready_$(PORT) ]; do sleep 0.05; done && rm -f /tmp/agentworld_ready_$(PORT) && ./$(BUILD_DIR)/agentworld-client --host 127.0.0.1 --port $(PORT) --skip-menu; kill $$SERVER 2>/dev/null

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

# Run headless E2E gameplay tests
test_e2e: build
	@echo "[test_e2e] Running headless gameplay tests..."
	cd $(BUILD_DIR) && ./agentworld-test
	@echo "[test_e2e] Done."

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
