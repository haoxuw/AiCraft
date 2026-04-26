#include "client/cef_browser_host.h"

#include "include/wrapper/cef_helpers.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>

namespace solarium::vk {

// ── RenderHandler ────────────────────────────────────────────────────────────
// CEF calls GetViewRect to learn the canvas size and OnPaint with a BGRA
// buffer when content changes. We currently snapshot the FULL viewport every
// time and ignore dirty rects — at menu sizes this is ~3 MB per paint, well
// under the cost of the Vulkan upload it'll feed.
class CefHost::RenderHandler : public CefRenderHandler {
public:
	RenderHandler(CefHost* host) : m_host(host) {}

	void GetViewRect(CefRefPtr<CefBrowser>, CefRect& rect) override {
		rect = CefRect(0, 0, m_host->m_w, m_host->m_h);
	}

	void OnPaint(CefRefPtr<CefBrowser>, PaintElementType type,
	             const RectList& /*dirty*/, const void* buffer,
	             int width, int height) override {
		if (type != PET_VIEW) return;
		const size_t byteCount = (size_t)width * (size_t)height * 4u;
		{
			std::lock_guard<std::mutex> lk(m_host->m_mtx);
			m_host->m_pixels.assign(
				static_cast<const uint8_t*>(buffer),
				static_cast<const uint8_t*>(buffer) + byteCount);
			m_host->m_w = width;
			m_host->m_h = height;
		}
		uint64_t f = m_host->m_frameCounter.fetch_add(1, std::memory_order_acq_rel) + 1;

		// Drop a PPM each paint (overwriting) so the final-state file always
		// reflects the most recent content. Chromium's first paint is often
		// an empty zero-buffer (GPU still initializing); the second/third is
		// where the rendered HTML actually lands. Logging only when the
		// buffer first becomes non-empty surfaces the moment that happens.
		const uint8_t* src = static_cast<const uint8_t*>(buffer);
		bool nonEmpty = false;
		for (int i = 0; i < width * height * 4 && !nonEmpty; ++i)
			if (src[i] != 0) nonEmpty = true;

		const char* path = "/tmp/solarium_cef_paint.ppm";
		if (FILE* fp = std::fopen(path, "wb")) {
			std::fprintf(fp, "P6\n%d %d\n255\n", width, height);
			std::vector<uint8_t> rgb((size_t)width * height * 3);
			for (int i = 0; i < width * height; ++i) {
				// CEF emits BGRA; PPM wants RGB.
				rgb[i*3 + 0] = src[i*4 + 2];
				rgb[i*3 + 1] = src[i*4 + 1];
				rgb[i*3 + 2] = src[i*4 + 0];
			}
			std::fwrite(rgb.data(), 1, rgb.size(), fp);
			std::fclose(fp);
		}
		if (nonEmpty && !m_loggedFirstReal) {
			m_loggedFirstReal = true;
			std::fprintf(stderr,
				"[cef] first non-empty paint at frame %llu (%dx%d) → %s\n",
				(unsigned long long)f, width, height, path);
		}
		// Always log the first 5 paints so we can see what CEF is sending.
		if (f <= 5) {
			std::fprintf(stderr,
				"[cef] paint %llu (%dx%d) buffer[0..15]=%02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x nonEmpty=%d\n",
				(unsigned long long)f, width, height,
				src[0], src[1], src[2], src[3],
				src[4], src[5], src[6], src[7],
				src[8], src[9], src[10], src[11],
				src[12], src[13], src[14], src[15],
				nonEmpty ? 1 : 0);
		}
	}

private:
	CefHost* m_host;
	bool     m_loggedFirstReal = false;
	IMPLEMENT_REFCOUNTING(RenderHandler);
};

// ── Query handler — JS → C++ bridge. Parses "action:NAME" and dispatches
// the host's action callback, then Success("ok"). Other formats fall through.
class CefHostQueryHandler : public CefMessageRouterBrowserSide::Handler {
public:
	CefHostQueryHandler(CefHost::ActionCallback cb) : m_cb(std::move(cb)) {}
	bool OnQuery(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
	             int64_t /*query_id*/, const CefString& request,
	             bool /*persistent*/, CefRefPtr<Callback> callback) override {
		const std::string r = request.ToString();
		const std::string prefix = "action:";
		if (r.rfind(prefix, 0) != 0) return false;
		std::string action = r.substr(prefix.size());
		if (m_cb) m_cb(action);
		callback->Success("ok");
		return true;
	}
private:
	CefHost::ActionCallback m_cb;
};

// ── Client ───────────────────────────────────────────────────────────────────
class CefHost::Client : public CefClient,
                               public CefLifeSpanHandler,
                               public CefLoadHandler {
public:
	Client(CefHost* host) : m_host(host) {
		m_renderHandler = new RenderHandler(host);
	}

	CefRefPtr<CefRenderHandler>   GetRenderHandler() override { return m_renderHandler; }
	CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
	CefRefPtr<CefLoadHandler>     GetLoadHandler() override { return this; }

	bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
	                              CefRefPtr<CefFrame> frame,
	                              CefProcessId src,
	                              CefRefPtr<CefProcessMessage> msg) override {
		if (m_router && m_router->OnProcessMessageReceived(browser, frame, src, msg))
			return true;
		return false;
	}

	void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
		CEF_REQUIRE_UI_THREAD();
		m_host->m_browser = browser;
		// Lazy-create the message router on first browser create.
		CefMessageRouterConfig cfg;
		m_router = CefMessageRouterBrowserSide::Create(cfg);
		m_queryHandler = std::make_unique<CefHostQueryHandler>(m_host->m_actionCb);
		m_router->AddHandler(m_queryHandler.get(), /*first=*/false);
		std::fprintf(stderr, "[cef] browser created (id=%d)\n",
			browser->GetIdentifier());
	}

	bool DoClose(CefRefPtr<CefBrowser>) override {
		CEF_REQUIRE_UI_THREAD();
		return false;
	}

	// Cancel every popup attempt: target=_blank, window.open, JS navigation
	// to external URLs, etc. We're an embedded UI, not a browser — no extra
	// windows allowed.
	bool OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
	                   int /*popup_id*/,
	                   const CefString& /*target_url*/,
	                   const CefString& /*target_frame_name*/,
	                   CefLifeSpanHandler::WindowOpenDisposition /*target_disposition*/,
	                   bool /*user_gesture*/,
	                   const CefPopupFeatures& /*popup_features*/,
	                   CefWindowInfo& /*window_info*/,
	                   CefRefPtr<CefClient>& /*client*/,
	                   CefBrowserSettings& /*settings*/,
	                   CefRefPtr<CefDictionaryValue>& /*extra_info*/,
	                   bool* /*no_javascript_access*/) override {
		return true;  // true == cancel.
	}

	void OnBeforeClose(CefRefPtr<CefBrowser> browser) override {
		CEF_REQUIRE_UI_THREAD();
		if (m_router) m_router->OnBeforeClose(browser);
		m_host->m_closed.store(true);
		m_host->m_browser = nullptr;
		std::fprintf(stderr, "[cef] browser closed\n");
	}

	void OnLoadError(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
	                 ErrorCode errorCode, const CefString& errorText,
	                 const CefString& failedUrl) override {
		std::fprintf(stderr,
			"[cef] load error: code=%d url=%s msg=%s\n",
			(int)errorCode, failedUrl.ToString().c_str(),
			errorText.ToString().c_str());
	}

private:
	CefHost*                                       m_host;
	CefRefPtr<RenderHandler>                       m_renderHandler;
	CefRefPtr<CefMessageRouterBrowserSide>         m_router;
	std::unique_ptr<CefHostQueryHandler>           m_queryHandler;
	IMPLEMENT_REFCOUNTING(Client);
};

// ── CefHost ───────────────────────────────────────────────────────────
CefHost::CefHost(int width, int height)
	: m_w(width), m_h(height) {
	m_pixels.resize((size_t)width * height * 4u, 0);
}

CefHost::~CefHost() {
	shutdown();
}

bool CefHost::start(const std::string& url) {
	m_client = new Client(this);

	CefBrowserSettings bs;
	bs.windowless_frame_rate = 30;
	// Transparent default so HTML can let the game show through. The actual
	// pixels still come back BGRA — alpha=0 means "fully transparent" in our
	// alpha-blend overlay pipeline.
	bs.background_color = 0x00000000;

	CefWindowInfo wi;
	wi.SetAsWindowless(/*parent=*/0);

	// Note: this is CEF's `::CefBrowserHost::CreateBrowser` static factory,
	// not our class — the leading `::` escapes our namespace shadowing.
	bool ok = ::CefBrowserHost::CreateBrowser(
		wi, m_client, url, bs, /*extra_info=*/nullptr,
		/*request_context=*/nullptr);
	if (!ok) {
		std::fprintf(stderr, "[cef] CreateBrowser failed for %s\n", url.c_str());
		return false;
	}
	std::fprintf(stderr, "[cef] CreateBrowser scheduled for %s\n", url.c_str());
	return true;
}

void CefHost::shutdown(int timeoutMs) {
	if (!m_browser || m_closed.load()) return;
	m_browser->GetHost()->CloseBrowser(/*force_close=*/true);
	auto deadline = std::chrono::steady_clock::now()
	                + std::chrono::milliseconds(timeoutMs);
	// CEF owns its own UI thread (multi_threaded_message_loop=true); just
	// poll the close-flag set from OnBeforeClose. No need to pump here.
	while (!m_closed.load()
	       && std::chrono::steady_clock::now() < deadline) {
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
}

void CefHost::invalidate() {
	if (m_browser) m_browser->GetHost()->Invalidate(PET_VIEW);
}

void CefHost::sendMouseMove(int x, int y, bool mouseLeave) {
	if (!m_browser) return;
	CefMouseEvent ev;
	ev.x = x; ev.y = y; ev.modifiers = 0;
	m_browser->GetHost()->SendMouseMoveEvent(ev, mouseLeave);
}

void CefHost::sendMouseClick(int x, int y, int button, bool down, int clickCount) {
	if (!m_browser) return;
	CefMouseEvent ev;
	ev.x = x; ev.y = y; ev.modifiers = 0;
	cef_mouse_button_type_t btn = MBT_LEFT;
	if (button == 1) btn = MBT_MIDDLE;
	else if (button == 2) btn = MBT_RIGHT;
	m_browser->GetHost()->SendMouseClickEvent(ev, btn, !down, clickCount);
}

void CefHost::sendMouseWheel(int x, int y, int dx, int dy) {
	if (!m_browser) return;
	CefMouseEvent ev;
	ev.x = x; ev.y = y; ev.modifiers = 0;
	m_browser->GetHost()->SendMouseWheelEvent(ev, dx, dy);
}

uint64_t CefHost::snapshotPixels(std::vector<uint8_t>& out,
                                        int& w, int& h) {
	std::lock_guard<std::mutex> lk(m_mtx);
	out = m_pixels;
	w = m_w;
	h = m_h;
	return m_frameCounter.load();
}

} // namespace solarium::vk
