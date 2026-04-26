#pragma once

// CefHost — owns one off-screen-rendered CEF browser and surfaces its
// last painted frame as a CPU-side BGRA buffer.
//
// Lifetime model:
//   - CefInitialize() is called by main() before any host is constructed.
//   - The host's `start()` schedules a CefHost::CreateBrowserSync (the
//     CEF API, not us) on the UI thread; OnAfterCreated stashes the browser
//     ref. From then on, the browser drives its own paint cadence and our
//     RenderHandler::OnPaint copies pixels under m_mtx.
//   - Tear-down: shutdown() asks the browser to close, then we wait for
//     OnBeforeClose. main() then calls CefShutdown after all hosts are gone.
//
// Thread model: CEF calls into RenderHandler from its UI thread. The main
// loop reads m_pixels under m_mtx for upload to Vulkan (or PPM dump). Keep
// the critical section to a memcpy.
//
// We deliberately keep the host policy-free: it loads a URL, holds pixels,
// and exits. JS↔C++ bridge, mouse/keyboard forwarding, and the multi-browser
// router all live in later phases (see docs/CEF_UI_PLAN.md).

#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_render_handler.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"

#include "include/wrapper/cef_message_router.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace solarium::vk {

class CefHost {
public:
	CefHost(int width, int height);
	~CefHost();

	CefHost(const CefHost&) = delete;
	CefHost& operator=(const CefHost&) = delete;

	// Create the off-screen browser and load `url`. Must be called on the
	// main thread, after CefInitialize. Returns false if browser creation
	// failed. The first OnPaint is async — poll frameCounter() to wait.
	bool start(const std::string& url);

	// Ask the browser to close and block until OnBeforeClose has fired or
	// `timeoutMs` elapses. Idempotent.
	void shutdown(int timeoutMs = 2000);

	// Snapshot the most recent paint into `out` (resized as needed).
	// Returns the frame counter at snapshot time; 0 means "never painted".
	uint64_t snapshotPixels(std::vector<uint8_t>& out, int& w, int& h);

	// Ask CEF to issue a fresh OnPaint even if the DOM hasn't changed.
	// Useful as a kick when the first frames arrive empty.
	void invalidate();

	// Navigate the main frame to a new URL (data: or file: or scheme).
	// Used to switch between menu pages (main → character-select → playing)
	// without tearing down the browser. No-op until OnAfterCreated has fired.
	void loadUrl(const std::string& url);

	// Forward GLFW input to the browser. (x,y) are pixel coords inside the
	// view. mouseLeave=true issues an OnMouseMove with leaveBoundary=true.
	void sendMouseMove(int x, int y, bool mouseLeave = false);
	// button: 0 = left, 1 = middle, 2 = right.
	void sendMouseClick(int x, int y, int button, bool down, int clickCount = 1);
	void sendMouseWheel(int x, int y, int deltaX, int deltaY);

	// JS bridge: HTML calls window.cefQuery({request:"action:foo"}) and `cb`
	// fires with "foo" on the browser UI thread. Set before start() so the
	// callback is in place by the time the page first executes JS.
	using ActionCallback = std::function<void(const std::string&)>;
	void setActionCallback(ActionCallback cb) { m_actionCb = std::move(cb); }

	// Monotonically increasing paint counter. Useful for "wait for first
	// frame" and for skipping uploads when the texture is current.
	uint64_t frameCounter() const { return m_frameCounter.load(); }

	int width()  const { return m_w; }
	int height() const { return m_h; }

private:
	class Client;
	class RenderHandler;

	int                       m_w;
	int                       m_h;

	std::mutex                m_mtx;
	std::vector<uint8_t>      m_pixels;     // BGRA, m_w*m_h*4
	std::atomic<uint64_t>     m_frameCounter{0};

	CefRefPtr<Client>         m_client;
	CefRefPtr<CefBrowser>     m_browser;
	std::atomic<bool>         m_closed{false};
	ActionCallback            m_actionCb;
};

} // namespace solarium::vk
