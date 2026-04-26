#include "client/cef_app.h"

#include "include/cef_browser_process_handler.h"
#include "include/cef_command_line.h"
#include "include/cef_render_process_handler.h"
#include "include/wrapper/cef_message_router.h"

namespace solarium::vk {

// CefApp lives in the browser process AND each subprocess (re-exec'd
// solarium-cef-subprocess). OnBeforeCommandLineProcessing fires before each
// process initializes its child Chromium, giving us one place to inject
// switches without touching the actual main() args.
//
// We force ANGLE + SwiftShader so off-screen rendering works on machines
// where the dedicated GPU process can't init (the "Failed global descriptor
// lookup: 7" error pattern paired with all-zero OnPaint buffers — that's
// Chromium's GPU subprocess bailing during fork). SwiftShader keeps GL
// rasterization in-process and fully software, which is fine for menu UI.
class OsrApp : public CefApp,
               public CefBrowserProcessHandler,
               public CefRenderProcessHandler {
public:
	CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
		return this;
	}
	CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override {
		return this;
	}
	void OnBeforeCommandLineProcessing(const CefString& /*processType*/,
	                                   CefRefPtr<CefCommandLine> cmd) override {
		cmd->AppendSwitch("no-zygote");
	}

	// ── Renderer process — install MessageRouter so window.cefQuery exists ──
	void OnWebKitInitialized() override {
		CefMessageRouterConfig cfg;  // defaults: window.cefQuery / cefQueryCancel
		m_renderRouter = CefMessageRouterRendererSide::Create(cfg);
	}
	void OnContextCreated(CefRefPtr<CefBrowser> browser,
	                      CefRefPtr<CefFrame> frame,
	                      CefRefPtr<CefV8Context> context) override {
		if (m_renderRouter) m_renderRouter->OnContextCreated(browser, frame, context);
	}
	void OnContextReleased(CefRefPtr<CefBrowser> browser,
	                       CefRefPtr<CefFrame> frame,
	                       CefRefPtr<CefV8Context> context) override {
		if (m_renderRouter) m_renderRouter->OnContextReleased(browser, frame, context);
	}
	bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
	                              CefRefPtr<CefFrame> frame,
	                              CefProcessId src,
	                              CefRefPtr<CefProcessMessage> msg) override {
		if (m_renderRouter)
			return m_renderRouter->OnProcessMessageReceived(browser, frame, src, msg);
		return false;
	}

private:
	CefRefPtr<CefMessageRouterRendererSide> m_renderRouter;
	IMPLEMENT_REFCOUNTING(OsrApp);
};

CefRefPtr<CefApp> makeOsrApp() { return new OsrApp(); }

} // namespace solarium::vk
