# Solarium -- Web Client Design

Dual-target architecture: same C++ source builds natively (Linux/Mac/Win) and as WebAssembly for browsers. Both connect to the same server.

---

## 1. Architecture Overview

```
                    ┌─────────────────────────────────────┐
                    │         Solarium Server (C++)         │
                    │                                     │
                    │   World ─ Physics ─ Behaviors       │
                    │   Action Queue ─ Entity Manager     │
                    │   Python Runtime (pybind11/CPython)  │
                    │                                     │
                    │          TCP port 7777               │
                    └──────────┬──────────┬───────────────┘
                               │          │
                    Binary TCP │          │ WebSocket
                    (native)   │          │ (browser)
                               │          │
               ┌───────────────┘          └───────────────┐
               │                                          │
  ┌────────────▼──────────────┐          ┌────────────────▼────────────┐
  │   Native Client (C++)     │          │   Web Client (WASM+WebGL)   │
  │                           │          │                             │
  │  GLFW ─ OpenGL 4.1        │          │  Emscripten GLFW ─ WebGL 2  │
  │  File I/O ─ TCP socket    │          │  VFS ─ WebSocket            │
  │  pybind11 (item visuals)  │          │  Pyodide (item visuals)     │
  │                           │          │                             │
  │  Linux / Mac / Windows    │          │  Chrome / Firefox / Safari  │
  └───────────────────────────┘          └─────────────────────────────┘
```

**Key principle:** One codebase, two build targets. All rendering code compiles with both `g++` and `emcc`. Platform-specific code is isolated behind thin abstraction layers.

---

## 2. Build System

### Native build (existing)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### Web build (new)

```bash
source /path/to/emsdk/emsdk_env.sh
emcmake cmake -B build-web \
  -DCMAKE_BUILD_TYPE=Release \
  -DAGENTWORLD_TARGET=web
cmake --build build-web -j$(nproc)
# Outputs: solarium.html, solarium.js, solarium.wasm, solarium.data
```

### CMake target detection

```cmake
option(AGENTWORLD_TARGET "Build target: native or web" "native")

if(AGENTWORLD_TARGET STREQUAL "web")
  set(CMAKE_EXECUTABLE_SUFFIX ".html")
  set(AGENTWORLD_WEB ON)
  # Emscripten provides GLFW, OpenGL ES
  set(USE_FLAGS "-sUSE_GLFW=3 -sMAX_WEBGL_VERSION=2 -sFULL_ES3=1")
  set(LINK_FLAGS "-sALLOW_MEMORY_GROWTH=1 --preload-file shaders --preload-file config")
else()
  set(AGENTWORLD_WEB OFF)
  # Native: GLFW, GLAD, system OpenGL
  find_package(OpenGL REQUIRED)
  FetchContent_MakeAvailable(glfw glad)
endif()
```

---

## 3. Rendering: OpenGL 4.1 → WebGL 2.0

### GLSL shader porting

WebGL 2.0 uses GLSL ES 3.00. Differences from GLSL 4.10:

| GLSL 4.10 | GLSL ES 3.00 | Action |
|------------|-------------|--------|
| `#version 410 core` | `#version 300 es` | Replace |
| No precision | `precision mediump float;` required | Add |
| `texture()` | `texture()` (same) | No change |
| `layout(location=N)` | Supported in ES 3.00 | No change |
| `fwidth()` | Supported with `OES_standard_derivatives` (default in WebGL2) | No change |

**Strategy:** Use `#ifdef` at the top of each shader, or preprocess at load time:

```glsl
#ifdef GL_ES
  #version 300 es
  precision mediump float;
#else
  #version 410 core
#endif
```

Only 6 shader files need this header change: `terrain`, `sky`, `crosshair`, `highlight`, `particle`, `text`.

### GL loader

- **Native:** GLAD generates GL 4.1 Core loader
- **Web:** Emscripten provides GL ES 3.0 headers directly (`<GLES3/gl3.h>`)

Abstraction: `#ifdef AGENTWORLD_WEB` chooses the include.

```cpp
#ifdef AGENTWORLD_WEB
  #include <GLES3/gl3.h>
#else
  #include <glad/gl.h>
#endif
```

### GLFW

Emscripten has built-in GLFW 3 support (`-sUSE_GLFW=3`). Our GLFW usage is standard -- no platform-specific extensions. Compile flag handles it.

For the enhanced version, [emscripten-glfw](https://github.com/pongasoft/emscripten-glfw) provides better pointer lock and high-DPI support.

---

## 4. Game Loop

### Native (current)

```cpp
while (!window.shouldClose()) {
    float dt = beginFrame();
    handleGlobalInput();
    updateAndRender(dt, aspect);
    endFrame();
}
```

### Web (Emscripten)

```cpp
#ifdef AGENTWORLD_WEB
  emscripten_set_main_loop_arg([](void* arg) {
      auto* game = static_cast<Game*>(arg);
      float dt = game->beginFrame();
      game->handleGlobalInput();
      game->updateAndRender(dt, game->aspect());
      game->endFrame();
  }, this, 0, true);
#else
  while (!m_window.shouldClose()) { ... }
#endif
```

Our `Game` class already has `beginFrame()`, `updateAndRender()`, `endFrame()` split. The refactor is minimal.

---

## 5. Networking

### Native client: TCP sockets (existing)

Binary protocol over TCP. Already implemented in `shared/net_protocol.h` and `shared/net_socket.h`.

### Web client: WebSocket

Browsers cannot open raw TCP sockets. Two options:

**Option A: WebSocket proxy (simple)**
```
Browser ──WebSocket──► Proxy (ws→tcp) ──TCP──► Solarium Server
```
A lightweight proxy (e.g., `websockify`) converts WebSocket to TCP. Server unchanged.

**Option B: Native WebSocket support in server (better)**
```
Browser ──WebSocket──► Solarium Server (listens on both TCP and WS)
```
Server accepts both TCP (native clients) and WebSocket (browser clients) on different ports. Same binary protocol over both transports.

**Recommendation:** Option B. Add a WebSocket listener to `GameServer` using a lightweight C library (e.g., `libwebsockets` or `uWebSockets`). The message format stays identical -- only the transport layer differs.

### Emscripten WebSocket API

```cpp
#ifdef AGENTWORLD_WEB
  #include <emscripten/websocket.h>
  // Connect: emscripten_websocket_new()
  // Send:    emscripten_websocket_send_binary()
  // Recv:    callback-based (emscripten_websocket_set_onmessage_callback)
#else
  // TCP: connect(), send(), recv() via shared/net_socket.h
#endif
```

Abstraction: `NetConnection` interface with `TCPConnection` and `WebSocketConnection` implementations.

---

## 6. Python in the Browser

### Server-side Python (unchanged)
The server runs CPython via pybind11. All behavior code (`decide()`, `validate()`, `execute()`) runs server-side. No change needed.

### Client-side Python (item visuals, UI scripts)

**Option A: Pyodide (full CPython in WASM)**
- Runs the same Python `.py` files as native client
- ~10MB download for Pyodide runtime
- 3-5x slower than native Python
- Good for: item visual definitions, UI scripting
- Bad for: performance-critical code (but item visuals are loaded once, not per-frame)

**Option B: No client-side Python (simpler)**
- Item visuals compiled into C++ builtins (current approach)
- Browser client uses the same C++ builtin definitions
- No Pyodide dependency, smaller download
- Players can still create Python content -- they upload to server, server loads it

**Recommendation:** Start with Option B. Add Pyodide later when the in-game editor ships. The C++ builtin definitions already mirror the Python files faithfully.

---

## 7. File I/O

### Shaders and config

Emscripten bundles files into a virtual filesystem:

```bash
emcc ... --preload-file shaders --preload-file config
```

`std::ifstream("shaders/terrain.vert")` works unchanged -- Emscripten's VFS intercepts it.

### Screenshots

Replace `glReadPixels` → PPM file with a browser download:

```cpp
#ifdef AGENTWORLD_WEB
  // Use emscripten_run_script() to trigger JS download of canvas
#else
  // Existing PPM save
#endif
```

---

## 8. Implementation Plan

### Phase 1: Shader portability (1 day)
- Add `#ifdef GL_ES` header to all 6 shader files
- Add `#ifdef AGENTWORLD_WEB` for GL include in client code
- Test both paths compile

### Phase 2: Emscripten build target (2 days)
- Add `AGENTWORLD_TARGET=web` to CMakeLists.txt
- Configure Emscripten GLFW, WebGL 2, VFS
- Refactor game loop for `emscripten_set_main_loop`
- First render in browser (terrain + sky)

### Phase 3: WebSocket networking (1-2 weeks)
- Create `NetConnection` abstraction (TCP vs WebSocket)
- Add WebSocket listener to server (alongside TCP)
- Emscripten WebSocket client implementation
- Test: browser client connects to dedicated server

### Phase 4: Browser polish (1 week)
- Pointer lock for mouse capture
- Fullscreen toggle
- High-DPI / canvas resize handling
- Loading screen while WASM downloads
- Touch input for mobile (future)

### Phase 5: Pyodide integration (optional, 1-2 weeks)
- Load Pyodide runtime on first use of in-game editor
- Hot-load item visual Python files from server
- Sandbox Python execution in browser

---

## 9. Deployment

### Static hosting

The web client is a static site:
```
dist/
  index.html          ← entry point
  solarium.js          ← Emscripten glue
  solarium.wasm        ← compiled game (~3-5MB)
  solarium.data        ← bundled shaders + config (~100KB)
```

Host on any CDN (Cloudflare Pages, Vercel, S3). No server-side rendering needed.

### Global server

Run the dedicated Solarium server on a cloud VM:
```bash
./solarium-server --port 7777 --ws-port 8080
```

Browser clients connect via WebSocket to `wss://play.solarium.io:8080`.
Native clients connect via TCP to `play.solarium.io:7777`.
Both use the same binary protocol.

---

## 10. Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| WebGL 2 not available on old browsers | Can't play | Require Chrome 56+ / Firefox 51+ / Safari 15+ (99%+ of users) |
| WASM binary too large | Slow first load | Streaming compilation, gzip compression (~1-2MB compressed) |
| Pointer lock denied | Can't play FPS mode | Fallback to RPG mode (works without pointer lock) |
| WebSocket latency | Laggy gameplay | Client-side prediction (same physics code runs locally) |
| Mobile browsers | Touch input missing | Phase 5 -- virtual joystick overlay |
