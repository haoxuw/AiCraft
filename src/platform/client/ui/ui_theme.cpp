#include "client/ui/ui_theme.h"

#include <cmath>
#include <cstdio>

namespace civcraft::ui {

namespace {
	ImFont* g_header = nullptr;
	ImFont* g_body   = nullptr;
	ImFont* g_mono   = nullptr;
	ImFont* g_small  = nullptr;
}

void applyTheme(ImGuiStyle& s) {
	s.WindowPadding    = ImVec2(kPadding, kPadding);
	s.FramePadding     = ImVec2(kGap, 8.0f);
	s.ItemSpacing      = ImVec2(kGap, kGap);
	s.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
	s.IndentSpacing    = 20.0f;
	s.ScrollbarSize    = 14.0f;
	s.GrabMinSize      = 12.0f;

	s.WindowRounding    = kRadius;
	s.ChildRounding     = kRadius;
	s.FrameRounding     = kRadius;
	s.PopupRounding     = kRadius;
	s.ScrollbarRounding = kRadius;
	s.GrabRounding      = kRadius;
	s.TabRounding       = kRadius;

	s.WindowBorderSize = kBorderPx;
	s.ChildBorderSize  = kBorderPx;
	s.PopupBorderSize  = kBorderPx;
	s.FrameBorderSize  = 0.0f;
	s.TabBorderSize    = 0.0f;

	auto& c = s.Colors;
	c[ImGuiCol_Text]                  = kText;
	c[ImGuiCol_TextDisabled]          = kTextOff;

	c[ImGuiCol_WindowBg]              = kBgPanel;
	c[ImGuiCol_ChildBg]               = ImVec4(0, 0, 0, 0);
	c[ImGuiCol_PopupBg]               = kBgDeep;

	c[ImGuiCol_Border]                = kBorder;
	c[ImGuiCol_BorderShadow]          = ImVec4(0, 0, 0, 0);

	c[ImGuiCol_FrameBg]               = kBgRaised;
	c[ImGuiCol_FrameBgHovered]        = kBgHover;
	c[ImGuiCol_FrameBgActive]         = kBgActive;

	c[ImGuiCol_TitleBg]               = kBgDeep;
	c[ImGuiCol_TitleBgActive]         = kBgPanel;
	c[ImGuiCol_TitleBgCollapsed]      = kBgDeep;
	c[ImGuiCol_MenuBarBg]             = kBgDeep;

	c[ImGuiCol_ScrollbarBg]           = ImVec4(0, 0, 0, 0);
	c[ImGuiCol_ScrollbarGrab]         = kBgRaised;
	c[ImGuiCol_ScrollbarGrabHovered]  = kBgHover;
	c[ImGuiCol_ScrollbarGrabActive]   = kBgActive;

	c[ImGuiCol_CheckMark]             = kAccent;
	c[ImGuiCol_SliderGrab]            = kAccent;
	c[ImGuiCol_SliderGrabActive]      = kAccentGlow;

	c[ImGuiCol_Button]                = kBgRaised;
	c[ImGuiCol_ButtonHovered]         = kBgHover;
	c[ImGuiCol_ButtonActive]          = kBgActive;

	c[ImGuiCol_Header]                = ImVec4(kAccentDim.x, kAccentDim.y, kAccentDim.z, 0.40f);
	c[ImGuiCol_HeaderHovered]         = ImVec4(kAccentDim.x, kAccentDim.y, kAccentDim.z, 0.65f);
	c[ImGuiCol_HeaderActive]          = ImVec4(kAccent.x,    kAccent.y,    kAccent.z,    0.80f);

	c[ImGuiCol_Separator]             = kBorder;
	c[ImGuiCol_SeparatorHovered]      = kAccentDim;
	c[ImGuiCol_SeparatorActive]       = kAccent;

	c[ImGuiCol_ResizeGrip]            = kBgRaised;
	c[ImGuiCol_ResizeGripHovered]     = kBgHover;
	c[ImGuiCol_ResizeGripActive]      = kAccent;

	c[ImGuiCol_Tab]                   = kBgRaised;
	c[ImGuiCol_TabHovered]            = kBgHover;
	c[ImGuiCol_TabActive]             = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.35f);
	c[ImGuiCol_TabUnfocused]          = kBgDeep;
	c[ImGuiCol_TabUnfocusedActive]    = kBgPanel;

	c[ImGuiCol_PlotLines]             = kAccentDim;
	c[ImGuiCol_PlotLinesHovered]      = kAccent;
	c[ImGuiCol_PlotHistogram]         = kAccentDim;
	c[ImGuiCol_PlotHistogramHovered]  = kAccent;

	c[ImGuiCol_TextSelectedBg]        = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.35f);
	c[ImGuiCol_DragDropTarget]        = kAccentGlow;
	c[ImGuiCol_NavHighlight]          = kAccent;
	c[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.0f, 0.0f, 0.0f, 0.60f);
}

void loadFonts(ImGuiIO& io) {
	// CMake stages fonts/ next to the exe, but CWD varies (make game runs
	// from build-perf/). Try a small set of relatives before giving up.
	const char* kPaths[] = {
		"fonts/Roboto-Medium.ttf",
		"../fonts/Roboto-Medium.ttf",
		"../../fonts/Roboto-Medium.ttf",
		"src/platform/fonts/Roboto-Medium.ttf",
	};
	const char* found = nullptr;
	for (const char* p : kPaths) {
		if (FILE* f = std::fopen(p, "rb")) { std::fclose(f); found = p; break; }
	}

	io.Fonts->Clear();
	if (!found) {
		io.Fonts->AddFontDefault();
		g_body = g_header = g_mono = g_small = nullptr;
		return;
	}
	// The first font added becomes the ImGui default — use Body so every
	// untouched widget gets 18px instead of the 13px fallback.
	g_body   = io.Fonts->AddFontFromFileTTF(found, kFontBody);
	g_header = io.Fonts->AddFontFromFileTTF(found, kFontHeader);
	g_small  = io.Fonts->AddFontFromFileTTF(found, kFontSmall);
	g_mono   = io.Fonts->AddFontFromFileTTF(found, kFontMono);
}

ImFont* header() { return g_header; }
ImFont* body()   { return g_body;   }
ImFont* mono()   { return g_mono;   }
ImFont* small_() { return g_small;  }

void accentGold(float t, float out[4]) {
	float pulse = 0.85f + 0.15f * std::sin(t * 1.8f);
	out[0] = kAccent.x * pulse;
	out[1] = kAccent.y * pulse;
	out[2] = kAccent.z * pulse;
	out[3] = kAccent.w;
}

}  // namespace civcraft::ui
