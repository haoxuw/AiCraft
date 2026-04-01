#include "client/ui.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

namespace aicraft {

bool UI::init(GLFWwindow* window) {
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

	// Dark theme with game-appropriate styling
	ImGui::StyleColorsDark();
	ImGuiStyle& style = ImGui::GetStyle();
	style.WindowRounding = 6.0f;
	style.FrameRounding = 4.0f;
	style.GrabRounding = 4.0f;
	style.WindowBorderSize = 1.0f;
	style.FrameBorderSize = 0.0f;
	style.WindowPadding = ImVec2(12, 12);
	style.ItemSpacing = ImVec2(8, 6);

	// Slightly transparent backgrounds for game overlay feel
	ImVec4* colors = style.Colors;
	colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.12f, 0.92f);
	colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.10f, 0.18f, 1.0f);
	colors[ImGuiCol_TitleBgActive] = ImVec4(0.15f, 0.15f, 0.28f, 1.0f);
	colors[ImGuiCol_Button] = ImVec4(0.20f, 0.22f, 0.35f, 1.0f);
	colors[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.30f, 0.48f, 1.0f);
	colors[ImGuiCol_ButtonActive] = ImVec4(0.35f, 0.38f, 0.58f, 1.0f);
	colors[ImGuiCol_Header] = ImVec4(0.20f, 0.22f, 0.35f, 0.7f);
	colors[ImGuiCol_HeaderHovered] = ImVec4(0.28f, 0.30f, 0.48f, 0.8f);

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

} // namespace aicraft
