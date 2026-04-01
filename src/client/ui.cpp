#include "client/ui.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

namespace agentworld {

bool UI::init(GLFWwindow* window) {
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

	// Load Roboto font (Google's friendly, readable font)
	io.Fonts->AddFontFromFileTTF("fonts/Roboto-Medium.ttf", 18.0f);

	// Google-inspired bright, clean theme
	ImGui::StyleColorsLight();
	ImGuiStyle& style = ImGui::GetStyle();
	style.WindowRounding = 12.0f;
	style.FrameRounding = 8.0f;
	style.GrabRounding = 8.0f;
	style.TabRounding = 8.0f;
	style.ScrollbarRounding = 8.0f;
	style.ChildRounding = 8.0f;
	style.PopupRounding = 8.0f;
	style.WindowBorderSize = 0.0f;
	style.FrameBorderSize = 0.0f;
	style.WindowPadding = ImVec2(16, 16);
	style.FramePadding = ImVec2(14, 8);
	style.ItemSpacing = ImVec2(10, 8);
	style.ScrollbarSize = 12.0f;

	// Warm orange theme with subtle shading — friendly, not sterile
	// Theme color: light orange (#F4A626) with warm gray backgrounds
	ImVec4* colors = style.Colors;
	colors[ImGuiCol_WindowBg]         = ImVec4(0.97f, 0.96f, 0.94f, 0.98f); // warm off-white
	colors[ImGuiCol_ChildBg]          = ImVec4(0.94f, 0.93f, 0.90f, 1.00f); // warm light gray
	colors[ImGuiCol_PopupBg]          = ImVec4(0.98f, 0.97f, 0.95f, 0.98f);
	colors[ImGuiCol_Border]           = ImVec4(0.85f, 0.82f, 0.78f, 0.60f);
	colors[ImGuiCol_FrameBg]          = ImVec4(0.92f, 0.90f, 0.87f, 1.00f); // shaded input bg
	colors[ImGuiCol_FrameBgHovered]   = ImVec4(0.95f, 0.90f, 0.82f, 1.00f); // warm hover
	colors[ImGuiCol_FrameBgActive]    = ImVec4(0.98f, 0.88f, 0.72f, 1.00f); // orange tint
	colors[ImGuiCol_TitleBg]          = ImVec4(0.94f, 0.93f, 0.90f, 1.00f);
	colors[ImGuiCol_TitleBgActive]    = ImVec4(0.97f, 0.95f, 0.92f, 1.00f);
	colors[ImGuiCol_MenuBarBg]        = ImVec4(0.94f, 0.93f, 0.90f, 1.00f);

	// Orange (#F4A626) for interactive elements
	colors[ImGuiCol_Button]           = ImVec4(0.96f, 0.65f, 0.15f, 1.00f); // warm orange
	colors[ImGuiCol_ButtonHovered]    = ImVec4(0.98f, 0.72f, 0.28f, 1.00f); // lighter
	colors[ImGuiCol_ButtonActive]     = ImVec4(0.90f, 0.55f, 0.10f, 1.00f); // darker

	// Tabs: warm gray, orange when active
	colors[ImGuiCol_Tab]              = ImVec4(0.90f, 0.88f, 0.85f, 1.00f);
	colors[ImGuiCol_TabHovered]       = ImVec4(0.96f, 0.65f, 0.15f, 0.60f);
	colors[ImGuiCol_TabActive]        = ImVec4(0.96f, 0.65f, 0.15f, 1.00f);
	colors[ImGuiCol_TabUnfocused]     = ImVec4(0.90f, 0.88f, 0.85f, 1.00f);
	colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.98f, 0.88f, 0.72f, 1.00f);

	// Headers — subtle warm shading
	colors[ImGuiCol_Header]           = ImVec4(0.94f, 0.90f, 0.84f, 1.00f);
	colors[ImGuiCol_HeaderHovered]    = ImVec4(0.96f, 0.88f, 0.76f, 1.00f);
	colors[ImGuiCol_HeaderActive]     = ImVec4(0.98f, 0.84f, 0.68f, 1.00f);

	// Text: warm dark brown-gray
	colors[ImGuiCol_Text]             = ImVec4(0.22f, 0.20f, 0.18f, 1.00f);
	colors[ImGuiCol_TextDisabled]     = ImVec4(0.55f, 0.52f, 0.48f, 1.00f);

	// Scrollbar
	colors[ImGuiCol_ScrollbarBg]      = ImVec4(0.94f, 0.93f, 0.90f, 1.00f);
	colors[ImGuiCol_ScrollbarGrab]    = ImVec4(0.78f, 0.75f, 0.70f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.70f, 0.66f, 0.60f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.62f, 0.58f, 0.52f, 1.00f);

	// Separator
	colors[ImGuiCol_Separator]        = ImVec4(0.82f, 0.80f, 0.76f, 0.70f);
	colors[ImGuiCol_SeparatorHovered] = ImVec4(0.96f, 0.65f, 0.15f, 0.60f);
	colors[ImGuiCol_SeparatorActive]  = ImVec4(0.96f, 0.65f, 0.15f, 1.00f);

	// Checkmarks: warm green
	colors[ImGuiCol_CheckMark]        = ImVec4(0.30f, 0.68f, 0.35f, 1.00f);
	colors[ImGuiCol_SliderGrab]       = ImVec4(0.96f, 0.65f, 0.15f, 1.00f);
	colors[ImGuiCol_SliderGrabActive] = ImVec4(0.90f, 0.55f, 0.10f, 1.00f);

	// Table colors — warm alternating rows
	colors[ImGuiCol_TableHeaderBg]    = ImVec4(0.90f, 0.88f, 0.84f, 1.00f);
	colors[ImGuiCol_TableBorderStrong]= ImVec4(0.82f, 0.80f, 0.76f, 0.70f);
	colors[ImGuiCol_TableBorderLight] = ImVec4(0.88f, 0.86f, 0.82f, 0.50f);
	colors[ImGuiCol_TableRowBg]       = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_TableRowBgAlt]    = ImVec4(0.92f, 0.90f, 0.87f, 0.50f);

	// Platform + renderer backends
	ImGui_ImplGlfw_InitForOpenGL(window, true);

#ifdef __EMSCRIPTEN__
	ImGui_ImplOpenGL3_Init("#version 300 es");
#else
	ImGui_ImplOpenGL3_Init("#version 410 core");
#endif

	return true;
}

void UI::shutdown() {
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
}

void UI::beginFrame() {
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
}

void UI::endFrame() {
	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

bool UI::wantsKeyboard() const {
	return ImGui::GetIO().WantCaptureKeyboard;
}

bool UI::wantsMouse() const {
	return ImGui::GetIO().WantCaptureMouse;
}

} // namespace agentworld
