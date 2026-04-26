#pragma once

// Tiny CefApp implementation shared by the browser process (solarium-ui-vk)
// and every Chromium subprocess (solarium-cef-subprocess). The same factory
// runs in both so command-line switches (ANGLE backend, GPU compositing
// toggle) propagate everywhere.
//
// Kept separate from cef_browser_host so the subprocess shim doesn't need
// to pull in std::vector / std::mutex / the OSR pixel-buffer plumbing.

#include "include/cef_app.h"

namespace solarium::vk {

// Returns a new instance each call — caller transfers ownership to CEF
// (CefRefPtr handles the refcount).
CefRefPtr<CefApp> makeOsrApp();

} // namespace solarium::vk
