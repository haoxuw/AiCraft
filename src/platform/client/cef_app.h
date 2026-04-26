#pragma once

// Tiny CefApp implementation shared by the browser process (civcraft-ui-vk)
// and every Chromium subprocess (civcraft-cef-subprocess). The same factory
// runs in both so command-line switches (ANGLE backend, GPU compositing
// toggle) propagate everywhere.
//
// Kept separate from cef_browser_host so the subprocess shim doesn't need
// to pull in std::vector / std::mutex / the OSR pixel-buffer plumbing.

#include "include/cef_app.h"

namespace civcraft::vk {

// Returns a new instance each call — caller transfers ownership to CEF
// (CefRefPtr handles the refcount).
CefRefPtr<CefApp> makeOsrApp();

} // namespace civcraft::vk
