BUILD_DIR := build
BUILD_WEB := build-web
BUILD_TYPE := Debug
HOST := 127.0.0.1
PORT := 7777
WEB_PORT := 8080
EMSDK := $(HOME)/emsdk

.PHONY: game build configure clean server client web web-build web-configure web-clean

# ── Native (Linux) ─────────────────────────────────────────

stop:
	@-pkill -f "agentworld" 2>/dev/null; sleep 1

game: build stop
	./$(BUILD_DIR)/agentworld

server: build stop
	./$(BUILD_DIR)/agentworld-server --port $(PORT)

client: build stop
	./$(BUILD_DIR)/agentworld-client --host $(HOST) --port $(PORT)

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
