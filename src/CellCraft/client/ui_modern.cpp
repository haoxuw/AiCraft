// CellCraft modern UI primitives — see ui_modern.h.
//
// All primitives convert pixel-space coords to the NDC space the platform
// TextRenderer expects, then call drawRect / drawArc / drawText. Rounded
// corners are built from a small triangle-fan helper that emits triangles
// directly through TextRenderer::drawArc (which accepts arbitrary angle
// ranges and ring widths — convenient for both corner fans and ring
// progress).

#include "CellCraft/client/ui_modern.h"
#include "CellCraft/client/sdf_font.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

namespace civcraft::cellcraft::ui::modern {

namespace {
::civcraft::TextRenderer* g_text = nullptr;
int g_screen_w = 1280;
int g_screen_h = 800;

// SDF fonts — owned here, initialized once at app boot.
std::unique_ptr<::civcraft::cellcraft::SdfFont> g_font_body;    // Inter-Regular
std::unique_ptr<::civcraft::cellcraft::SdfFont> g_font_bold;    // Inter-Bold
std::unique_ptr<::civcraft::cellcraft::SdfFont> g_font_display; // Audiowide
bool g_fonts_ready = false;

struct RoleSpec {
	::civcraft::cellcraft::SdfFont* font;
	int   pxSize;
	bool  uppercase;
	float tracking_px;
	glm::vec4 defaultFill;
	glm::vec4 defaultGlow;
	float glowRadiusPx;
	float glowIntensity;
};

RoleSpec specFor(Role r) {
	using F = ::civcraft::cellcraft::SdfFont*;
	F body = g_font_body.get();
	F bold = g_font_bold.get();
	F disp = g_font_display.get();
	const glm::vec4 NONE(0.0f);
	switch (r) {
	case Role::BODY:
		return { body, TYPE_BODY,     false, 0.0f, TEXT_PRIMARY, NONE, 0.f, 0.f };
	case Role::LABEL:
		return { bold, TYPE_LABEL,    true,  1.2f, TEXT_SECONDARY, NONE, 0.f, 0.f };
	case Role::TITLE_SM:
		return { bold, TYPE_TITLE_SM, false, 0.0f, TEXT_PRIMARY, NONE, 0.f, 0.f };
	case Role::TITLE_MD:
		return { bold, TYPE_TITLE_MD, false, 0.0f, TEXT_PRIMARY, NONE, 0.f, 0.f };
	case Role::TITLE_LG:
		return { bold, TYPE_TITLE_LG, false, 0.0f, TEXT_PRIMARY,
		         glm::vec4(ACCENT_CYAN_GLOW.r, ACCENT_CYAN_GLOW.g, ACCENT_CYAN_GLOW.b, 0.5f),
		         4.0f, 0.8f };
	case Role::DISPLAY:
		return { disp, TYPE_DISPLAY,  false, 0.0f, TEXT_PRIMARY,
		         glm::vec4(ACCENT_CYAN_GLOW.r, ACCENT_CYAN_GLOW.g, ACCENT_CYAN_GLOW.b, 0.55f),
		         8.0f, 1.0f };
	case Role::HERO_NEON:
		// Caller chooses color via drawTextRole's rgba; glow mirrors it unless
		// glow_override is provided.
		return { disp, TYPE_DISPLAY,  false, 0.0f, TEXT_PRIMARY,
		         glm::vec4(1.0f, 0.3f, 0.3f, 0.85f), 14.0f, 1.4f };
	}
	return { body, TYPE_BODY, false, 0.0f, TEXT_PRIMARY, NONE, 0.f, 0.f };
}

// Apply uppercase if needed.
std::string maybeUpper(const std::string& s, bool upper) {
	if (!upper) return s;
	std::string r; r.reserve(s.size());
	for (char c : s) r.push_back((c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c);
	return r;
}

// Pixel → NDC. Origin top-left, y-down → NDC origin center, y-up.
inline float px_x(int x) { return (float)x * 2.0f / (float)g_screen_w - 1.0f; }
inline float px_y(int y) { return 1.0f - (float)y * 2.0f / (float)g_screen_h; }
inline float px_w(int w) { return (float)w * 2.0f / (float)g_screen_w; }
inline float px_h(int h) { return (float)h * 2.0f / (float)g_screen_h; }

// Aspect for drawArc: drawArc treats r as NDC-x and multiplies y by
// `aspect`. To get a circular arc we want NDC_y_per_unit = aspect *
// NDC_x_per_unit. Since 1px in y = 2/h NDC, 1px in x = 2/w NDC, the y/x
// ratio for equal pixels is (w/h). So aspect = w/h.
inline float arc_aspect() { return (float)g_screen_w / (float)g_screen_h; }
inline float r_px_to_ndc_x(int r) { return (float)r * 2.0f / (float)g_screen_w; }

// Filled rect (no rounding) in pixel space.
inline void rect_px(int x, int y, int w, int h, const glm::vec4& c) {
	if (!g_text || c.a <= 0.0f) return;
	// drawRect uses (x,y) as bottom-left and grows up in NDC. Our pixel
	// origin is top-left, so the rect's bottom-left in NDC corresponds to
	// pixel (x, y+h).
	g_text->drawRect(px_x(x), px_y(y + h), px_w(w), px_h(h), c);
}

// Build text scale that yields glyphs `size_px` tall.
inline float text_scale_for(int size_px) {
	// charH_ndc = scale * 0.032; px = ndc * h / 2 → scale = px / (0.016*h)
	return (float)size_px / (0.016f * (float)g_screen_h);
}
inline int char_advance_px(int size_px) {
	// charW_ndc = scale * 0.018; px = ndc * w / 2
	float scale = text_scale_for(size_px);
	return (int)std::round(scale * 0.018f * (float)g_screen_w * 0.5f * 2.0f);
	// = scale * 0.018 * w  → integer pixel advance per glyph
}

// Draw a quarter-circle filled fan around (cx, cy) covering `angle0..angle1`
// (radians, 0=right, +y=down in screen). Internally we flip y because
// drawArc uses NDC math (y-up).
void draw_corner_fan(int cx, int cy, int radius,
                     float angle0_deg, float angle1_deg,
                     const glm::vec4& color, int segments = 8) {
	if (radius <= 0 || color.a <= 0.0f || !g_text) return;
	float a0 = (float)(-angle0_deg) * (float)M_PI / 180.0f;
	float a1 = (float)(-angle1_deg) * (float)M_PI / 180.0f;
	float r_ndc = r_px_to_ndc_x(radius);
	g_text->drawArc(px_x(cx), px_y(cy), 0.0f, r_ndc,
		std::min(a0, a1), std::max(a0, a1), color, arc_aspect(), segments);
}

// Filled rounded rect (no stroke). Built from one center rect, two side
// rects, and four quarter-circle fans.
void filled_rounded(int x, int y, int w, int h, int r, const glm::vec4& c) {
	if (c.a <= 0.0f) return;
	r = std::min(r, std::min(w, h) / 2);
	if (r <= 0) { rect_px(x, y, w, h, c); return; }
	// Center cross.
	rect_px(x + r, y,         w - 2*r, h,        c); // full-height middle band
	rect_px(x,     y + r,     r,       h - 2*r, c);  // left band
	rect_px(x + w - r, y + r, r,       h - 2*r, c);  // right band
	// Corners (screen space: 0deg = +x right, 90deg = +y down).
	draw_corner_fan(x + r,         y + r,         r, 180.f, 270.f, c); // TL
	draw_corner_fan(x + w - r,     y + r,         r, 270.f, 360.f, c); // TR
	draw_corner_fan(x + w - r,     y + h - r,     r,   0.f,  90.f, c); // BR
	draw_corner_fan(x + r,         y + h - r,     r,  90.f, 180.f, c); // BL
}

// 1px stroke approximation: draw 4 thin rects on the rounded rect's bbox.
// Cheap, looks fine at 1-2 px.
void stroke_rounded(int x, int y, int w, int h, int r,
                    const glm::vec4& c, int t) {
	(void)r;
	if (c.a <= 0.0f || t <= 0) return;
	rect_px(x,         y,           w, t, c);          // top
	rect_px(x,         y + h - t,   w, t, c);          // bottom
	rect_px(x,         y + t,       t, h - 2*t, c);    // left
	rect_px(x + w - t, y + t,       t, h - 2*t, c);    // right
}

} // namespace

// =====================================================================

void beginFrame(::civcraft::TextRenderer* text, int sw, int sh) {
	g_text = text;
	g_screen_w = sw > 0 ? sw : 1;
	g_screen_h = sh > 0 ? sh : 1;
}

bool initFonts(const std::string& fontDir, const std::string& shaderDir) {
	using F = ::civcraft::cellcraft::SdfFont;
	g_font_body    = std::make_unique<F>();
	g_font_bold    = std::make_unique<F>();
	g_font_display = std::make_unique<F>();
	bool ok = true;
	ok &= g_font_body->init(fontDir + "/Inter-Regular.ttf", shaderDir, 64.0f);
	ok &= g_font_bold->init(fontDir + "/Inter-Bold.ttf",    shaderDir, 64.0f);
	// Audiowide is naturally wide — bake larger so TYPE_DISPLAY is crisp.
	ok &= g_font_display->init(fontDir + "/Audiowide-Regular.ttf", shaderDir, 96.0f);
	g_fonts_ready = ok;
	if (!ok) {
		std::fprintf(stderr, "[ui_modern] SDF font init failed — falling back to bitmap font\n");
	}
	return ok;
}

void shutdownFonts() {
	if (g_font_body)    g_font_body->shutdown();
	if (g_font_bold)    g_font_bold->shutdown();
	if (g_font_display) g_font_display->shutdown();
	g_font_body.reset();
	g_font_bold.reset();
	g_font_display.reset();
	g_fonts_ready = false;
}

// ---- Surfaces -------------------------------------------------------

void drawScrim(int x, int y, int w, int h,
               const glm::vec4& top, const glm::vec4& bot) {
	const int STRIPES = 24;
	for (int i = 0; i < STRIPES; ++i) {
		float t0 = (float)i / STRIPES;
		float t1 = (float)(i + 1) / STRIPES;
		glm::vec4 c = glm::mix(top, bot, 0.5f * (t0 + t1));
		int sy = y + (int)(h * t0);
		int sh = (int)(h * t1) - (int)(h * t0) + 1;
		rect_px(x, sy, w, sh, c);
	}
}

void drawRoundedRect(int x, int y, int w, int h, int radius,
                     const glm::vec4& fill,
                     const glm::vec4& stroke, int stroke_px_) {
	filled_rounded(x, y, w, h, radius, fill);
	if (stroke.a > 0.0f && stroke_px_ > 0) {
		stroke_rounded(x, y, w, h, radius, stroke, stroke_px_);
	}
}

void drawGlassPanel(int x, int y, int w, int h, int radius) {
	// 1) Base translucent fill.
	filled_rounded(x, y, w, h, radius, SURFACE_PANEL);
	// 2) Top 40% highlight gradient toward SURFACE_PANEL_HI. Approximate
	// by stacking thin horizontal stripes inside the rect (no rounding —
	// the dark fill below already establishes the corner mask, and the
	// highlight alpha is low enough that any sliver overhang at corners
	// reads as "frosted gradient" rather than a square edge).
	int hi_h = (int)(h * 0.40f);
	const int STRIPES = 12;
	for (int i = 0; i < STRIPES; ++i) {
		float t0 = (float)i / STRIPES;
		float t1 = (float)(i + 1) / STRIPES;
		// Top → bottom of highlight zone, fading from PANEL_HI to PANEL.
		glm::vec4 c = glm::mix(SURFACE_PANEL_HI, SURFACE_PANEL, 0.5f * (t0 + t1));
		c.a *= (1.0f - 0.5f * (t0 + t1)) * 0.6f; // taper toward bottom of band
		int sy = y + (int)(hi_h * t0);
		int sh = (int)(hi_h * t1) - (int)(hi_h * t0) + 1;
		// Inset by `radius` horizontally so it doesn't paint over corner cutouts.
		rect_px(x + radius, sy, w - 2 * radius, sh, c);
	}
	// 3) 1px inner stroke (subtle).
	stroke_rounded(x, y, w, h, radius, STROKE_SUBTLE, 1);
	// 4) "Glass lip" — strong 1px highlight just inside the top edge.
	rect_px(x + radius, y + 1, w - 2 * radius, 1, STROKE_STRONG);
}

void drawSoftShadow(int x, int y, int w, int h, int radius,
                    int spread, float alpha) {
	if (spread <= 0 || alpha <= 0.0f) return;
	// 6 concentric expanding rings, fading out.
	const int RINGS = 6;
	for (int i = RINGS; i >= 1; --i) {
		float t = (float)i / RINGS;            // 1..1/RINGS, outer first
		int   inset = (int)(spread * t);
		float a = alpha * 0.18f * (1.0f - t);  // strongest near the panel
		glm::vec4 c{0.0f, 0.0f, 0.0f, a};
		filled_rounded(x - inset, y - inset + spread / 4,
		               w + inset * 2, h + inset * 2,
		               radius + inset, c);
	}
}

void drawInnerGlow(int x, int y, int w, int h, int radius,
                   const glm::vec4& rgba, int spread) {
	if (spread <= 0 || rgba.a <= 0.0f) return;
	for (int i = 0; i < spread; ++i) {
		glm::vec4 c = rgba;
		c.a *= 0.15f * (1.0f - (float)i / (float)spread);
		int rr = std::max(0, radius - i);
		stroke_rounded(x + i, y + i, w - 2*i, h - 2*i, rr, c, 1);
	}
}

// ---- Text -----------------------------------------------------------

// Map a raw pixel size to a reasonable role. Used by measureTextPx /
// drawTextModern so legacy call sites still benefit from real fonts.
static Role roleForPxSize(int size_px) {
	if (size_px <= TYPE_CAPTION + 1) return Role::BODY;
	if (size_px <= TYPE_BODY + 1)    return Role::BODY;
	if (size_px <= TYPE_LABEL + 1)   return Role::BODY; // caller picks LABEL explicitly
	if (size_px <= TYPE_TITLE_SM + 2) return Role::TITLE_SM;
	if (size_px <= TYPE_TITLE_MD + 2) return Role::TITLE_MD;
	if (size_px <= TYPE_TITLE_LG + 2) return Role::TITLE_LG;
	return Role::DISPLAY;
}

int measureTextPx(const std::string& s, int size_px) {
	if (g_fonts_ready) {
		// Special-case TYPE_LABEL — all LABEL renders go through Role::LABEL
		// (uppercase + tracking). Measuring with Role::BODY would under-
		// estimate width and cause centering drift. Check size first.
		if (size_px == TYPE_LABEL) {
			RoleSpec sp = specFor(Role::LABEL);
			if (sp.font && sp.font->ready()) {
				std::string up = maybeUpper(s, sp.uppercase);
				return (int)std::round(sp.font->measureWidth(up, (float)sp.pxSize, sp.tracking_px));
			}
		}
		RoleSpec sp = specFor(roleForPxSize(size_px));
		if (sp.font && sp.font->ready()) {
			return (int)std::round(sp.font->measureWidth(s, (float)size_px, 0.0f));
		}
	}
	float scale = text_scale_for(size_px);
	float ndc = scale * 0.018f * (float)s.size();
	return (int)std::round(ndc * 0.5f * (float)g_screen_w);
}

int measureTextRole(const std::string& s, Role role) {
	if (g_fonts_ready) {
		RoleSpec sp = specFor(role);
		if (sp.font && sp.font->ready()) {
			std::string t = maybeUpper(s, sp.uppercase);
			return (int)std::round(sp.font->measureWidth(t, (float)sp.pxSize, sp.tracking_px));
		}
	}
	// Fallback — approximate tracking with inserted spaces for LABEL.
	if (role == Role::LABEL) {
		return measureTextPx(maybeUpper(s, true), TYPE_LABEL) + TYPE_LABEL / 2 * ((int)s.size() - 1);
	}
	RoleSpec sp = specFor(role);
	return measureTextPx(s, sp.pxSize);
}

void drawTextRole(int x, int y, const std::string& text, Role role,
                  const glm::vec4& rgba, Align align,
                  const glm::vec4& glow_override) {
	if (text.empty() || rgba.a <= 0.0f) return;
	RoleSpec sp = specFor(role);
	std::string s = maybeUpper(text, sp.uppercase);

	if (g_fonts_ready && sp.font && sp.font->ready()) {
		float w = sp.font->measureWidth(s, (float)sp.pxSize, sp.tracking_px);
		int draw_x = x;
		if (align == Align::CENTER) draw_x = x - (int)(w * 0.5f);
		else if (align == Align::RIGHT) draw_x = x - (int)w;
		// y param is top-of-caps; convert to baseline via ascent.
		float baselineY = (float)y + sp.font->ascent((float)sp.pxSize);
		glm::vec4 glow = (glow_override.a > 0.0f) ? glow_override : sp.defaultGlow;
		if (role == Role::HERO_NEON && glow_override.a <= 0.0f) {
			// Tint glow to match fill.
			glow = glm::vec4(rgba.r, rgba.g, rgba.b, 0.85f);
		}
		sp.font->draw(s, (float)draw_x, baselineY, (float)sp.pxSize,
			rgba, glow, sp.glowRadiusPx, sp.glowIntensity, sp.tracking_px,
			g_screen_w, g_screen_h);
		return;
	}
	// Fallback to platform bitmap font.
	drawTextModern(x, y, s, sp.pxSize, rgba, align);
}

void drawTextModern(int x, int y, const std::string& text, int size_px,
                    const glm::vec4& rgba, Align align) {
	if (text.empty() || rgba.a <= 0.0f) return;

	// Route through SDF font when available.
	if (g_fonts_ready) {
		Role r = roleForPxSize(size_px);
		RoleSpec sp = specFor(r);
		if (sp.font && sp.font->ready()) {
			float w = sp.font->measureWidth(text, (float)size_px, 0.0f);
			int draw_x = x;
			if (align == Align::CENTER) draw_x = x - (int)(w * 0.5f);
			else if (align == Align::RIGHT) draw_x = x - (int)w;
			float baselineY = (float)y + sp.font->ascent((float)size_px);
			sp.font->draw(text, (float)draw_x, baselineY, (float)size_px,
				rgba, sp.defaultGlow, sp.glowRadiusPx, sp.glowIntensity,
				0.0f, g_screen_w, g_screen_h);
			return;
		}
	}

	if (!g_text) return;
	float scale = text_scale_for(size_px);
	int   tw    = measureTextPx(text, size_px);
	int   draw_x = x;
	if (align == Align::CENTER) draw_x = x - tw / 2;
	else if (align == Align::RIGHT) draw_x = x - tw;
	float ndc_x = px_x(draw_x);
	float ndc_y = px_y(y + size_px);
	g_text->drawText(text, ndc_x, ndc_y, scale, rgba, 1.0f);
}

void drawTextLabel(int x, int y, const std::string& s, const glm::vec4& rgba) {
	// Modern path: real tracking via SdfFont. No more inserted-space hack.
	drawTextRole(x, y, s, Role::LABEL, rgba);
}

void drawTextDisplay(int x, int y, const std::string& s,
                     const glm::vec4& rgba, Align align) {
	drawTextRole(x, y, s, Role::DISPLAY, rgba, align);
}

// ---- Buttons --------------------------------------------------------

bool buttonPrimary(int x, int y, int w, int h, const std::string& label,
                   bool hovered, bool pressed) {
	int dx = x, dy = y, dw = w, dh = h;
	if (pressed) {
		// Scale 0.98 around center.
		int new_w = (int)(w * 0.98f), new_h = (int)(h * 0.98f);
		dx = x + (w - new_w) / 2;
		dy = y + (h - new_h) / 2;
		dw = new_w; dh = new_h;
	}
	// Outer glow on hover.
	if (hovered && !pressed) {
		drawInnerGlow(dx - 4, dy - 4, dw + 8, dh + 8, RADIUS_MD + 4,
		              ACCENT_CYAN_GLOW, 6);
	}
	// Cyan vertical gradient (top GLOW → bottom CYAN). Build with rounded
	// stripes by stacking a few rounded fills with progressively lighter
	// color and reduced height — cheap but reads as a gradient.
	const int BANDS = 6;
	for (int i = 0; i < BANDS; ++i) {
		float t0 = (float)i / BANDS;
		float t1 = (float)(i + 1) / BANDS;
		glm::vec4 c = glm::mix(ACCENT_CYAN_GLOW, ACCENT_CYAN, 0.5f * (t0 + t1));
		// First and last band keep rounded corners by clipping radius for
		// middle bands to 0; outer two carry the rounded shape.
		int sy = dy + (int)(dh * t0);
		int sh = (int)(dh * t1) - (int)(dh * t0) + 1;
		if (i == 0) {
			// Top band: round only top corners — emulate by drawing a
			// rounded rect that covers the top 50% then a square cover for
			// the bottom slice. Simpler: draw full rounded fill underneath
			// with the average color (done below) and overlay solid bands.
		}
		rect_px(dx + RADIUS_MD, sy, dw - 2 * RADIUS_MD, sh, c);
	}
	// Underlay rounded shape with a base color so the corner radii read.
	// (We drew the bands in the inset rectangle; now draw the rounded
	// silhouette over the top with a transparent overlay — instead just
	// draw rounded fills for the corner caps using the average color.)
	{
		glm::vec4 avg = glm::mix(ACCENT_CYAN_GLOW, ACCENT_CYAN, 0.5f);
		// Left + right rounded caps.
		filled_rounded(dx, dy, RADIUS_MD * 2, dh, RADIUS_MD, avg);
		filled_rounded(dx + dw - RADIUS_MD * 2, dy,
		               RADIUS_MD * 2, dh, RADIUS_MD, avg);
	}
	// Label centered.
	if (!label.empty()) {
		int label_size = TYPE_TITLE_SM;
		int tw = measureTextPx(label, label_size);
		drawTextModern(dx + (dw - tw) / 2,
		               dy + (dh - label_size) / 2,
		               label, label_size, TEXT_ON_ACCENT);
	}
	return hovered && pressed;
}

bool buttonGhost(int x, int y, int w, int h, const std::string& label,
                 bool hovered, bool pressed) {
	glm::vec4 fill{0.0f};
	if (hovered) { fill = ACCENT_CYAN; fill.a = 0.08f; }
	int dx = x, dy = y, dw = w, dh = h;
	if (pressed) {
		int new_w = (int)(w * 0.98f), new_h = (int)(h * 0.98f);
		dx += (w - new_w) / 2; dy += (h - new_h) / 2; dw = new_w; dh = new_h;
	}
	drawRoundedRect(dx, dy, dw, dh, RADIUS_MD, fill, ACCENT_CYAN, 1);
	if (!label.empty()) {
		int label_size = TYPE_TITLE_SM;
		int tw = measureTextPx(label, label_size);
		drawTextModern(dx + (dw - tw) / 2,
		               dy + (dh - label_size) / 2,
		               label, label_size, ACCENT_CYAN);
	}
	return hovered && pressed;
}

bool buttonIcon(int x, int y, int size, const std::string& icon,
                const std::string& tooltip, bool hovered, bool pressed) {
	(void)tooltip; // tooltip rendering is not part of this commit
	int dx = x, dy = y, ds = size;
	if (pressed) { ds = (int)(size * 0.96f); dx += (size - ds) / 2; dy += (size - ds) / 2; }
	drawGlassPanel(dx, dy, ds, ds, RADIUS_MD);
	if (hovered) drawInnerGlow(dx, dy, ds, ds, RADIUS_MD, ACCENT_CYAN_GLOW, 8);
	if (!icon.empty()) {
		int gs = TYPE_TITLE_SM;
		int tw = measureTextPx(icon, gs);
		drawTextModern(dx + (ds - tw) / 2, dy + (ds - gs) / 2,
		               icon, gs, hovered ? ACCENT_CYAN_GLOW : TEXT_PRIMARY);
	}
	return hovered && pressed;
}

// ---- Stat bar -------------------------------------------------------

void drawStatBar(int x, int y, int w, const std::string& label,
                 float value_0_1, const std::string& numeric,
                 const glm::vec4& color) {
	if (value_0_1 < 0) value_0_1 = 0;
	if (value_0_1 > 1) value_0_1 = 1;
	// Top row: label left, numeric right.
	drawTextLabel(x, y, label, TEXT_SECONDARY);
	if (!numeric.empty()) {
		int nw = measureTextPx(numeric, TYPE_BODY);
		drawTextModern(x + w - nw, y - 1, numeric, TYPE_BODY, TEXT_PRIMARY);
	}
	// Track + fill.
	int track_y = y + TYPE_LABEL + SPACE_XS;
	int track_h = 6;
	drawRoundedRect(x, track_y, w, track_h, 3, TRACK_BG);
	int fill_w = (int)(w * value_0_1);
	if (fill_w > 0) {
		// 4px outer "glow" — a slightly larger lower-alpha bar behind.
		glm::vec4 glow = color; glow.a = 0.35f;
		drawRoundedRect(x - 2, track_y - 2, fill_w + 4, track_h + 4, 5, glow);
		drawRoundedRect(x, track_y, fill_w, track_h, 3, color);
	}
}

// ---- Pill badge -----------------------------------------------------

void drawPillBadge(int x, int y, const std::string& text,
                   const glm::vec4& fg, const glm::vec4& bg,
                   const glm::vec4& accent) {
	int pad_x = SPACE_MD;
	int pad_y = 4;
	int text_h = TYPE_BODY;
	int text_w = measureTextPx(text, text_h);
	int w = text_w + pad_x * 2;
	int h = text_h + pad_y * 2;
	int r = h / 2;
	drawRoundedRect(x, y, w, h, r, bg);
	drawTextModern(x + pad_x, y + pad_y, text, text_h, fg);
	if (accent.a > 0.0f) {
		// 2px underline inside the bottom edge.
		rect_px(x + r, y + h - 4, w - 2 * r, 2, accent);
	}
}

// ---- Ring progress --------------------------------------------------

void drawRingProgress(int cx, int cy, int radius, int thickness,
                      float value_0_1, const glm::vec4& fg,
                      const glm::vec4& bg) {
	if (radius <= 0 || thickness <= 0) return;
	if (value_0_1 < 0) value_0_1 = 0;
	if (value_0_1 > 1) value_0_1 = 1;
	float r_outer = r_px_to_ndc_x(radius);
	float r_inner = r_px_to_ndc_x(radius - thickness);
	if (r_inner < 0) r_inner = 0;
	float aspect = arc_aspect();
	// Background full ring.
	g_text->drawArc(px_x(cx), px_y(cy), r_inner, r_outer,
		0.0f, 2.0f * (float)M_PI, bg, aspect, 64);
	if (value_0_1 > 0.0f) {
		// Foreground arc starting at 12 o'clock going clockwise.
		// Screen "up" = -y; drawArc uses NDC math (y-up). The "up" angle
		// in NDC is +pi/2. Clockwise on screen = decreasing NDC angle.
		float start = (float)M_PI * 0.5f;
		float end   = start - value_0_1 * 2.0f * (float)M_PI;
		// drawArc requires endAngle > startAngle implicitly via sweep sign.
		// It handles negative sweep fine as long as we just pass them.
		g_text->drawArc(px_x(cx), px_y(cy), r_inner, r_outer,
			start, end, fg, aspect, 64);
	}
}

// ---- Divider --------------------------------------------------------

void drawDivider(int x, int y, int length, DividerAxis axis,
                 const glm::vec4& rgba) {
	if (axis == DividerAxis::HORIZONTAL) rect_px(x, y, length, 1, rgba);
	else                                  rect_px(x, y, 1, length, rgba);
}

} // namespace civcraft::cellcraft::ui::modern
