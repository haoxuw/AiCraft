# CEF HTML UI

Status: phase 1 complete (main menu replaced). Loading screen & in-game panels still
native. See `CEF_UI_PLAN.md` for the multi-phase roadmap and
`CEF_LEGACY_CLEANUP.md` for what to delete once CEF is the only menu UI.

## What it is

The main menu is rendered by Chromium (CEF — Chromium Embedded Framework) as HTML/CSS/JS,
composited as an alpha-blended fullscreen overlay on top of the game's Vulkan rendering.
The 3D plaza scene keeps drawing underneath; the bitmap-font menu UI is suppressed.

```
HTML/CSS/JS  ──┐
                │  CefRenderHandler::OnPaint
                ▼
       CefHost CPU buffer (BGRA, premul alpha)
                │  blitCefImage → per-FIF staging
                ▼
       VkImage (B8G8R8A8_UNORM, sampled)
                │  cef_overlay.{vert,frag}, fullscreen triangle, ONE/ONE_MINUS_SRC_ALPHA
                ▼
       Swap image  ←  composite of game (Vulkan) + CEF overlay

Reverse path:
GLFW mouse → CefHost::sendMouse* → DOM event → JS handler
        → window.cefQuery({request:"action:NAME"})
        → CefMessageRouterRendererSide → IPC → CefMessageRouterBrowserSide
        → CefHostQueryHandler::OnQuery → ActionCallback → game state
```

## Run

```bash
make cef_setup     # one-time: download CEF 146 binary distribution to third_party/cef/
make demo          # build + visible window with CEF main menu over the plaza
```

`make demo` is the canonical entry point. Click Singleplayer to start the game;
click Quit to close. Multiplayer/Settings are stubs that dismiss to the native
panels underneath.

For automation, the binary takes:

```
--cef-menu             enable the CEF overlay
--cef-url URL          load a custom URL instead of the inline demo HTML
--cef-mouse X,Y        synthetic hover at (X,Y) ~4s after start (headless test)
--cef-click X,Y        synthetic click at (X,Y) ~6s after start (headless test)
```

## Files

| File                                              | Role                                                    |
| ------------------------------------------------- | ------------------------------------------------------- |
| `src/platform/client/cef_app.{h,cpp}`             | `CefApp` shared by browser + subprocess: command-line switches, renderer-side `CefMessageRouter` lifecycle |
| `src/platform/client/cef_browser_host.{h,cpp}`    | `CefHost` class — owns one OSR browser, surfaces pixels, mouse forwarding, action callback |
| `src/platform/client/cef_subprocess_main.cpp`     | `civcraft-cef-subprocess` binary: bare `CefExecuteProcess` shim |
| `src/platform/client/main.cpp`                    | `CefInitialize`, host creation, action wiring (Quit / Singleplayer), `--cef-*` flags |
| `src/platform/client/rhi/rhi_vk.{h,cpp}`          | `blitCefImage`, `recordCefUpload`, `recordCefOverlay`, sampled image + pipeline |
| `src/platform/shaders/vk/cef_overlay.{vert,frag}` | Fullscreen triangle + texture sample with premul alpha  |
| `third_party/cef/`                                | Vendored CEF binary distribution (gitignored, ~1.4 GB) |
| `src/platform/client/game_vk.{h,cpp}`             | `m_cefMenuActive` flag — gates `MenuRenderer::renderMenu()` |

## Process layout

```
civcraft-ui-vk            ← parent: Vulkan, GLFW, Python, audio, server-spawner
  └─ libcef.so            ← linked, calls CefInitialize, owns BrowserSide
       │ posix_spawn (--no-zygote)
       └─ civcraft-cef-subprocess --type=gpu-process    ← child
       └─ civcraft-cef-subprocess --type=renderer       ← child (one per browser)
       └─ civcraft-cef-subprocess --type=utility …      ← child (network, storage)
```

The subprocess is a separate binary so renderer/GPU/utility children don't re-enter
the main game (which would re-init Python/GLFW/audio/LLM). Both binaries share
`cef_app.cpp` so command-line switches propagate.

## Vulkan composite

The CEF buffer is BGRA premultiplied alpha (CEF's OSR convention with
`background_color = 0`). We:

1. **Stage** the buffer per frame in flight: `blitCefImage` memcpy's into
   `m_cefStaging[m_frame]`.
2. **Upload** in `beginFrame` (before any render pass): `vkCmdCopyBufferToImage`
   into `m_cefImage` with layout transitions UNDEFINED/SHADER_READ → TRANSFER_DST →
   SHADER_READ.
3. **Composite** in `endFrame` (inside the swapchain render pass): bind the
   `cef_overlay` pipeline, sample `m_cefImage`, fullscreen triangle, alpha-blend.

`endFrame` always forces `beginSwapchainPass()` so the world composite + CEF overlay
land in the swap image even when no other UI ran that frame.

Blend mode is **premul-alpha** (`srcColorBlendFactor=ONE`,
`dstColorBlendFactor=ONE_MINUS_SRC_ALPHA`). Required because CEF outputs premul when
`background_color` has alpha=0.

The `RUNPATH=/usr/lib/x86_64-linux-gnu:$ORIGIN` on `civcraft-ui-vk` ensures system
NVIDIA `libvulkan.so.1` wins over Chromium's bundled SwiftShader copy that ships
next to `libcef.so`. The SwiftShader libs are kept (CEF's GPU subprocess needs them
as fallback rasterizer); the main game just doesn't pick them up.

## JS↔C++ bridge

HTML calls `window.cefQuery({request: "action:NAME", onSuccess, onFailure})` — the
`CefMessageRouter` round-trips the request to `CefHostQueryHandler::OnQuery`, which
dispatches to a `std::function<void(string)>` set on the host. Today's actions:

| Action            | Effect                                                          |
| ----------------- | --------------------------------------------------------------- |
| `quit`            | `glfwSetWindowShouldClose(win, GLFW_TRUE)` — clean exit         |
| `singleplayer`    | `game.skipMenu()` — runs Connecting→Playing, dismisses overlay  |
| `multiplayer`     | (stub) dismiss overlay; native multiplayer screen takes over     |
| `settings`        | (stub) dismiss overlay; native settings screen takes over        |

Future actions (character pick, settings tabs, multiplayer connect form) plug into
the same `OnQuery` dispatch by adding more `if (action == ...)` branches.

## Build wiring

CMake: `find_package(CEF REQUIRED)` from `third_party/cef/cmake/`, then both targets
link `libcef_dll_wrapper` + `libcef.so`. Two `add_executable` targets:

- `civcraft-ui-vk` — the parent; links CEF + does everything else.
- `civcraft-cef-subprocess` — tiny binary; links CEF + nothing else; written into
  `$<TARGET_FILE_DIR:civcraft-ui-vk>` so they're peers at runtime.

`POST_BUILD` copies `CEF_BINARY_FILES` (libcef.so, chrome-sandbox, snapshot bin,
SwiftShader) and `CEF_RESOURCE_FILES` (.pak, icudtl.dat, locales/) into the binary
directory. CEF needs to find them as siblings of `libcef.so`.

## Known issues

1. **`Failed global descriptor lookup: 7`** logs from CEF subprocesses — non-fatal.
   `--no-zygote` mitigates the worst case (rendering used to fail entirely). Root
   cause: our parent's FD layout doesn't match Chromium's zygote expectations.
2. **No window resize support** — `m_cefImage` is created at the startup framebuffer
   size; resizing the window crops the overlay. Fix: rebuild the image on
   `onResize`.
3. **No keyboard input forwarded** — HTML `<input>` won't accept typing yet. Need
   `CefHost::sendKeyEvent` + GLFW char/key callback plumbing.
4. **CEF startup latency** ~1-2 s before first paint. The plaza renders alone in that
   window; user perceives it as a normal title screen warm-up.
5. **GameLogger init order is load-bearing** — must run *after* `CefInitialize` so it
   doesn't take FD 3, which Chromium's posix_spawn dup2 actions need free. See
   comment in `main.cpp` near `civcraft::GameLogger::instance().init(...)`.
6. **Native main-menu UI is suppressed via flag, not deleted.** Sub-screens
   (Multiplayer, Settings, Handbook) still draw via the dismiss-to-native fallback
   path. Removal blocked on CEF replacements — see `CEF_LEGACY_CLEANUP.md`.

## Performance

Per frame while overlay active:
- ~4 MB BGRA memcpy into staging (host).
- `vkCmdCopyBufferToImage` 1280×800 + two layout barriers.
- Fullscreen triangle draw (3 vertices, single texture sample).

Total ~0.5 ms on a modern GPU. Cheap enough to leave on permanently if we
eventually want CEF in-game (HUD, dialog). When CEF is dismissed
(`m_cefOverlayEnabled = false`), the upload + draw are skipped entirely — zero cost.
