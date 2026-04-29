#pragma once

// solarium::ui — single source of truth for the CEF design system.
//
// Every menu page (handbook, settings, char-select, lobby, mp, saves, mods,
// editor, …) builds a `data:text/html` URL. They all want the same theme
// palette, the same typography, the same `.btn`/`.tag`/`.note`/`.tile`/`.row`
// vocabulary, the same artistic scrollbar, the same paper-grained body.
//
// Before this file lived: each page redefined its CSS inline; touching
// anything visual meant editing 8 strings; theme drift was inevitable. Now
// pages call `ui::baseCss()` (or `ui::dexCss()` for the sidebar+detail
// shell) and only override what's truly local to them.
//
// Hex literals stay URL-encoded as `%23` because every page is shipped as a
// `data:` URL and a bare `#` would terminate the payload as a fragment.

#include <string>

namespace solarium::ui {

// Theme palette — a `:root{...}` block prepended to every page's <style>.
// One branch here + one tile on Settings → Theme = a new theme.
inline std::string themeRoot(const std::string& id) {
	if (id == "cobalt") {
		return ":root{--accent-hi:%237eb8e8;--accent-mid:%233a6890;"
		       "--ink:%23dde6ed;--ink-soft:%23a8b6c4;"
		       "--bg:%2308141a;--bg-2:%23142532}";
	}
	if (id == "lichen") {
		return ":root{--accent-hi:%23d4cf9f;--accent-mid:%238a9970;"
		       "--ink:%23ede5d0;--ink-soft:%23bfb89a;"
		       "--bg:%23120e07;--bg-2:%231f1a10}";
	}
	// "brass" — default + catch-all.
	return ":root{--accent-hi:%23f3c44c;--accent-mid:%23b88838;"
	       "--ink:%23f0e0c0;--ink-soft:%23b8a980;"
	       "--bg:%23140d08;--bg-2:%231f140b}";
}

// HTML/URL-escape for content embedded inside a data: URL.
// %, ", <, >, &, ', # are dangerous — encode each.
inline std::string enc(const std::string& s) {
	std::string o; o.reserve(s.size() + 16);
	for (char c : s) {
		switch (c) {
			case '"': o += "&quot;"; break;
			case '<': o += "&lt;";   break;
			case '>': o += "&gt;";   break;
			case '&': o += "&amp;";  break;
			case '\'':o += "&apos;"; break;
			case '%': o += "%25";    break;
			case '#': o += "%23";    break;
			default:  o += c;
		}
	}
	return o;
}

// Common CSS shared by every page. Emits typography, body backdrop, h1
// flourishes, ornamental subtitles (.tag/.note/.filters chips), buttons
// with octagonal cut corners, and the artistic scrollbar (so EVERY
// scrolling region looks consistent — handbook, settings overflow, mods
// list, editor minimap, etc.).
//
// "Less straight lines" choices baked in here:
//   - Buttons & tiles use clip-path octagon (cut corners) instead of
//     plain rectangles.
//   - Subtitles are flanked by small ornament glyphs (◆) plus fading
//     hairlines, not a hard rule.
//   - Body has paper-grain crosshatch + radial scrim vignette.
//   - Vertical brass-sheen gradient on buttons so they read as cast
//     plates, not flat boxes.
inline std::string baseCss() {
	return
		// ── Reset + body backdrop ────────────────────────────────────────
		"html,body{margin:0;height:100vh;background:transparent;"
		"color:var(--ink);font-family:Georgia,serif;"
		"display:flex;flex-direction:column;align-items:center;"
		"justify-content:center}"
		"body{background:"
		// Centre vignette — opaque core fading to translucent so the
		// menu plaza reads as soft-focus background, not foreground.
		"radial-gradient(ellipse 90%% 80%% at center,"
		"rgba(6,4,3,0.78) 0%%,rgba(6,4,3,0.55) 55%%,"
		"rgba(6,4,3,0.30) 100%%),"
		// Paper grain — two diagonal repeating gradients at very low
		// alpha simulate woven fibre + uneven ink absorption.
		"repeating-linear-gradient(45deg,transparent 0 5px,"
		"rgba(255,255,255,0.012) 5px 6px),"
		"repeating-linear-gradient(-45deg,transparent 0 7px,"
		"rgba(0,0,0,0.025) 7px 8px)}"

		// ── Headline ─────────────────────────────────────────────────────
		// Bracketing hairline rules + glow halo for an illuminated-
		// manuscript feel. Curved fade (gradient stop) softens the
		// straight rule so the eye reads it as ornament, not divider.
		"h1{color:var(--accent-hi);font-size:72px;letter-spacing:8px;"
		"margin:0 0 8px;font-weight:400;"
		"text-shadow:0 4px 24px rgba(0,0,0,0.85),"
		"0 0 18px rgba(243,196,76,0.35);"
		"position:relative;display:inline-block;padding:0 56px}"
		"h1::before,h1::after{content:'';position:absolute;top:50%%;"
		"width:36px;height:1px;background:linear-gradient(to right,"
		"transparent,var(--accent-mid));opacity:0.8}"
		"h1::before{left:0}"
		"h1::after{right:0;background:linear-gradient(to left,"
		"transparent,var(--accent-mid))}"

		// ── Subtitle chips (.tag / .note / .filters) ─────────────────────
		// Same ornament across all three so loose text floating on the
		// world preview always reads. ◆ glyphs flank for "embossed
		// brass tag" feel; cut corners keep with the retro ethos.
		".tag,.note,.filters{display:inline-block;font-size:14px;"
		"letter-spacing:4px;opacity:0.95;margin:0 0 36px;"
		"text-transform:uppercase;color:var(--accent-mid);"
		"padding:6px 22px;background:rgba(6,4,3,0.78);"
		"border:1px solid rgba(184,136,56,0.45);"
		"text-shadow:0 1px 2px rgba(0,0,0,0.85);"
		"box-shadow:inset 0 0 0 1px rgba(255,235,180,0.06),"
		"0 2px 8px rgba(0,0,0,0.55)}"
		".note{font-size:12px;letter-spacing:2px;margin:0 0 24px}"
		".filters{display:inline-flex;gap:18px;align-items:center;"
		"font-size:12px;margin-bottom:12px}"
		".filters label{display:flex;align-items:center;gap:8px;"
		"cursor:pointer}"
		".filters input[type='checkbox']{accent-color:var(--accent-hi)}"

		// ── Buttons ──────────────────────────────────────────────────────
		// Dark brass-trimmed plate. Vertical gradient (slightly lighter
		// at top → darker at bottom) reads as cast metal. Hover lifts
		// via translateY + filter brightness; no clip-path because that
		// turned every page's button row into ribbons in early iterations.
		".btn{display:block;width:280px;margin:6px 0;padding:12px 0;"
		"font-size:18px;color:var(--accent-hi);"
		"background:linear-gradient(to bottom,"
		"rgba(48,32,18,0.97),rgba(20,13,8,0.97));"
		"border:1px solid var(--accent-mid);"
		"font-family:inherit;cursor:pointer;letter-spacing:2px;"
		"transition:transform 0.12s,filter 0.12s,box-shadow 0.12s;"
		"box-shadow:inset 0 1px 0 rgba(255,235,180,0.18),"
		"inset 0 -2px 4px rgba(0,0,0,0.5),"
		"0 4px 12px rgba(0,0,0,0.55)}"
		".btn:hover{filter:brightness(1.18);"
		"transform:translateY(-1px);"
		"box-shadow:inset 0 1px 0 rgba(255,235,180,0.28),"
		"inset 0 -2px 4px rgba(0,0,0,0.55),"
		"0 6px 18px rgba(0,0,0,0.7)}"
		".btn:active{filter:brightness(0.92);"
		"transform:translateY(0);"
		"box-shadow:inset 0 2px 4px rgba(0,0,0,0.6),"
		"0 1px 3px rgba(0,0,0,0.4)}"
		".back{margin-top:32px;width:160px;font-size:14px;opacity:0.9}"

		// ── Footer version chip ──────────────────────────────────────────
		".version{position:fixed;bottom:20px;right:30px;font-size:13px;"
		"opacity:0.6;color:var(--accent-mid);"
		"text-shadow:0 1px 3px rgba(0,0,0,0.85)}"

		// ── Artistic scrollbar (applies to every overflow:auto region) ──
		"::-webkit-scrollbar{width:14px;height:14px}"
		"::-webkit-scrollbar-track{"
		"background:linear-gradient(to right,"
		"rgba(184,136,56,0.55) 0,rgba(184,136,56,0.55) 1px,"
		"rgba(0,0,0,0.92) 1px,rgba(0,0,0,0.92) 6px,"
		"rgba(184,136,56,0.18) 6px,rgba(184,136,56,0.18) 8px,"
		"rgba(0,0,0,0.92) 8px,rgba(0,0,0,0.92) 13px,"
		"rgba(184,136,56,0.55) 13px,rgba(184,136,56,0.55) 14px);"
		"box-shadow:inset 0 0 6px rgba(0,0,0,0.85)}"
		"::-webkit-scrollbar-thumb{"
		"background:"
		// Three horizontal grip notches at the centre.
		"linear-gradient(to bottom,transparent calc(50%% - 8px),"
		"rgba(0,0,0,0.55) calc(50%% - 8px),rgba(0,0,0,0.55) calc(50%% - 7px),"
		"rgba(243,196,76,0.5) calc(50%% - 7px),rgba(243,196,76,0.5) calc(50%% - 6px),"
		"transparent calc(50%% - 6px),transparent calc(50%% - 1px),"
		"rgba(0,0,0,0.55) calc(50%% - 1px),rgba(0,0,0,0.55) calc(50%%),"
		"rgba(243,196,76,0.5) calc(50%%),rgba(243,196,76,0.5) calc(50%% + 1px),"
		"transparent calc(50%% + 1px),transparent calc(50%% + 6px),"
		"rgba(0,0,0,0.55) calc(50%% + 6px),rgba(0,0,0,0.55) calc(50%% + 7px),"
		"rgba(243,196,76,0.5) calc(50%% + 7px),rgba(243,196,76,0.5) calc(50%% + 8px),"
		"transparent calc(50%% + 8px)),"
		// Cast-brass vertical sheen.
		"linear-gradient(to bottom,"
		"%23c08a3a 0%%,%23a07526 18%%,%236d4f1f 50%%,"
		"%234a3415 82%%,%23745525 100%%);"
		"border:1px solid %23b88838;"
		"box-shadow:inset 0 1px 0 rgba(243,196,76,0.85),"
		"inset 0 -1px 0 rgba(0,0,0,0.85),"
		"inset 1px 0 0 rgba(243,196,76,0.35),"
		"inset -1px 0 0 rgba(0,0,0,0.55),"
		"0 0 4px rgba(0,0,0,0.65)}"
		"::-webkit-scrollbar-thumb:hover{"
		"background:linear-gradient(to bottom,"
		"%23e0a64a 0%%,%23b88838 18%%,%23805d24 50%%,"
		"%2358391a 82%%,%238b6a2f 100%%);"
		"border-color:%23f3c44c}"
		"::-webkit-scrollbar-thumb:active{"
		"background:linear-gradient(to bottom,"
		"%23805d24 0%%,%2358391a 50%%,%234a3415 100%%)}"
		"::-webkit-scrollbar-button{height:14px;width:14px;"
		"background:linear-gradient(to bottom,%23a07526 0%%,%235e431e 100%%);"
		"border:1px solid %23b88838;"
		"box-shadow:inset 0 1px 0 rgba(243,196,76,0.5),"
		"inset 0 -1px 0 rgba(0,0,0,0.7)}"
		"::-webkit-scrollbar-button:vertical:start{"
		"background-image:"
		"linear-gradient(135deg,transparent 0 5px,%23f3c44c 5px 6px,transparent 6px 7px),"
		"linear-gradient(45deg,transparent 0 5px,%23f3c44c 5px 6px,transparent 6px 7px),"
		"linear-gradient(to bottom,%23a07526 0%%,%235e431e 100%%)}"
		"::-webkit-scrollbar-button:vertical:end{"
		"background-image:"
		"linear-gradient(45deg,%23f3c44c 0 1px,transparent 1px 2px),"
		"linear-gradient(135deg,%23f3c44c 0 1px,transparent 1px 2px),"
		"linear-gradient(to bottom,%23a07526 0%%,%235e431e 100%%)}"
		"::-webkit-scrollbar-button:hover{"
		"background:linear-gradient(to bottom,%23b88838 0%%,%237a5928 100%%)}"
		"::-webkit-scrollbar-corner{"
		"background:linear-gradient(135deg,rgba(0,0,0,0.92),"
		"rgba(184,136,56,0.4))}"

		// ── Reusable card primitives ─────────────────────────────────────
		// `.panel` is the canonical "thing in a brass-bordered box":
		// opaque, beveled, octagonal cut corners, paper-grain texture.
		// Tile/row/saves are its specialisations.
		".panel{background:"
		"linear-gradient(to bottom,"
		"rgba(28,18,11,0.97),"
		"rgba(20,13,8,0.97));"
		"border:1px solid var(--accent-mid);"
		"box-shadow:inset 0 0 0 1px rgba(0,0,0,0.85),"
		"inset 0 1px 0 rgba(243,196,76,0.18),"
		"0 6px 22px rgba(0,0,0,0.7)}"

		// Diamond-flanked ornament rule (use as <hr class='div'/>) —
		// curved gradient hairlines fading toward a centre brass bead.
		"hr.div{border:0;height:14px;margin:8px 0;"
		"background:"
		"radial-gradient(circle 3px at center,var(--accent-mid) 0,"
		"var(--accent-mid) 2px,transparent 2.5px),"
		"linear-gradient(to right,transparent 0%%,"
		"var(--accent-mid) 35%%,transparent 48%%,"
		"transparent 52%%,var(--accent-mid) 65%%,transparent 100%%);"
		"background-repeat:no-repeat;background-size:auto,100%% 1px;"
		"background-position:center,center}"
		;
}

} // namespace solarium::ui
