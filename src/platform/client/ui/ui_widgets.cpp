#include "client/ui/ui_widgets.h"
#include "client/ui/ui_theme.h"

#include <cstdarg>
#include <cstdio>

namespace civcraft::ui {

static float resolveWidth(float w) {
	return w > 0.0f ? w : ImGui::GetContentRegionAvail().x;
}

static bool styledButton(const char* label, float w, float h,
                         ImVec4 bg, ImVec4 hover, ImVec4 active,
                         ImVec4 textCol, ImFont* font) {
	if (font) ImGui::PushFont(font);
	ImGui::PushStyleColor(ImGuiCol_Button,        bg);
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
	ImGui::PushStyleColor(ImGuiCol_ButtonActive,  active);
	ImGui::PushStyleColor(ImGuiCol_Text,          textCol);
	bool clicked = ImGui::Button(label, ImVec2(resolveWidth(w), h));
	ImGui::PopStyleColor(4);
	if (font) ImGui::PopFont();
	return clicked;
}

bool PrimaryButton(const char* label, float w, float h) {
	return styledButton(label, w, h,
	                    kAccentDim, kAccent, kAccentGlow,
	                    kBgDeep,    header());
}

bool SecondaryButton(const char* label, float w, float h) {
	return styledButton(label, w, h,
	                    kBgRaised, kBgHover, kBgActive,
	                    kText,     body());
}

bool DangerButton(const char* label, float w, float h) {
	ImVec4 bg     {0.45f, 0.15f, 0.15f, 1.00f};
	ImVec4 hover  {0.70f, 0.25f, 0.25f, 1.00f};
	ImVec4 active {0.88f, 0.35f, 0.30f, 1.00f};
	return styledButton(label, w, h, bg, hover, active, kText, body());
}

bool GhostButton(const char* label, float w, float h) {
	ImVec4 bg     {0.0f, 0.0f, 0.0f, 0.0f};
	ImVec4 hover  {kAccent.x, kAccent.y, kAccent.z, 0.12f};
	ImVec4 active {kAccent.x, kAccent.y, kAccent.z, 0.28f};
	return styledButton(label, w, h, bg, hover, active, kTextDim, body());
}

void SectionHeader(const char* text) {
	if (auto* f = header()) ImGui::PushFont(f);
	ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
	ImGui::TextUnformatted(text);
	ImGui::PopStyleColor();
	if (header()) ImGui::PopFont();
	Divider();
}

void Divider(float thickness) {
	ImDrawList* dl = ImGui::GetWindowDrawList();
	ImVec2 p = ImGui::GetCursorScreenPos();
	float w = ImGui::GetContentRegionAvail().x;
	dl->AddLine(p, ImVec2(p.x + w, p.y), ImGui::GetColorU32(kBorder), thickness);
	ImGui::Dummy(ImVec2(w, thickness + 4.0f));
}

void VerticalSpace(float px) {
	ImGui::Dummy(ImVec2(0.0f, px));
}

void Hint(const char* fmt, ...) {
	if (auto* f = small_()) ImGui::PushFont(f);
	ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
	va_list args; va_start(args, fmt);
	ImGui::TextV(fmt, args);
	va_end(args);
	ImGui::PopStyleColor();
	if (small_()) ImGui::PopFont();
}

void ColoredText(const ImVec4& col, const char* fmt, ...) {
	ImGui::PushStyleColor(ImGuiCol_Text, col);
	va_list args; va_start(args, fmt);
	ImGui::TextV(fmt, args);
	va_end(args);
	ImGui::PopStyleColor();
}

void KeyValue(const char* key, const char* fmt, ...) {
	char buf[256];
	va_list args; va_start(args, fmt);
	std::vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
	ImGui::TextUnformatted(key);
	ImGui::PopStyleColor();

	ImGui::SameLine();
	float avail = ImGui::GetContentRegionAvail().x;
	float tw    = ImGui::CalcTextSize(buf).x;
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - tw));
	ImGui::TextUnformatted(buf);
}

void ProgressBar(float pct, const char* phase, const char* detail) {
	if (phase) {
		if (auto* f = small_()) ImGui::PushFont(f);
		ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
		ImGui::TextUnformatted(phase);
		ImGui::PopStyleColor();
		if (small_()) ImGui::PopFont();
	}
	if (pct < 0.0f) pct = 0.0f;
	if (pct > 1.0f) pct = 1.0f;
	ImGui::PushStyleColor(ImGuiCol_PlotHistogram, kAccent);
	ImGui::PushStyleColor(ImGuiCol_FrameBg,       kBgRaised);
	ImGui::ProgressBar(pct, ImVec2(-1.0f, 14.0f), "");
	ImGui::PopStyleColor(2);
	if (detail) Hint("%s", detail);
}

void Meter(float pct, const ImVec4* colorOverride) {
	if (pct < 0.0f) pct = 0.0f;
	if (pct > 1.0f) pct = 1.0f;
	ImVec4 col = colorOverride ? *colorOverride
	           : (pct >= 0.5f  ? kOk
	           :  pct >= 0.25f ? kWarn
	           :                 kBad);
	ImGui::PushStyleColor(ImGuiCol_PlotHistogram, col);
	ImGui::PushStyleColor(ImGuiCol_FrameBg,       kBgRaised);
	ImGui::ProgressBar(pct, ImVec2(-1.0f, 12.0f), "");
	ImGui::PopStyleColor(2);
}

}  // namespace civcraft::ui
