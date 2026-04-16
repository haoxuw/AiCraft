// GL backend for the RHI — wraps existing OpenGL lifecycle so the game can
// run through the same IRhi interface as the Vulkan backend.
//
// Phase 0: init/shutdown, frame lifecycle, ImGui, screenshot.
// Phase 2: drawUi2D — full SDF text / rect / title pipeline (shared font
// atlas from rhi/ui_font_8x8.h, shared tessellation from rhi/rhi_ui.cpp).
// The existing legacy TextRenderer is kept alive for callers that still use
// it directly (hud, code_editor); FloatingTextManager + LightbulbDrawer now
// route through this RHI path so both backends render pixel-identical text.

#include "rhi.h"
#include "ui_font_8x8.h"

#ifdef __EMSCRIPTEN__
  #include <GLES3/gl3.h>
  #include <GLFW/glfw3.h>
#else
  #include <glad/gl.h>
  #define GLFW_INCLUDE_NONE
  #include <GLFW/glfw3.h>
#endif

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

namespace civcraft::rhi {

namespace {

// Compile a single GLSL stage. Returns 0 on failure, logs the shader error.
GLuint compileStage(GLenum type, const char* src, const char* tag) {
	GLuint s = glCreateShader(type);
	glShaderSource(s, 1, &src, nullptr);
	glCompileShader(s);
	GLint ok = 0;
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[2048];
		glGetShaderInfoLog(s, sizeof(log), nullptr, log);
		fprintf(stderr, "[gl-rhi] %s shader compile failed: %s\n", tag, log);
		glDeleteShader(s);
		return 0;
	}
	return s;
}

// Link a vert+frag pair into a program. Returns 0 on failure.
GLuint linkProgram(GLuint vs, GLuint fs, const char* tag) {
	GLuint p = glCreateProgram();
	glAttachShader(p, vs);
	glAttachShader(p, fs);
	glLinkProgram(p);
	GLint ok = 0;
	glGetProgramiv(p, GL_LINK_STATUS, &ok);
	if (!ok) {
		char log[2048];
		glGetProgramInfoLog(p, sizeof(log), nullptr, log);
		fprintf(stderr, "[gl-rhi] %s program link failed: %s\n", tag, log);
		glDeleteProgram(p);
		return 0;
	}
	// Shaders can be detached + deleted after linking; the program keeps
	// its own ref count.
	glDeleteShader(vs);
	glDeleteShader(fs);
	return p;
}

// UI shader — GLSL source inlined so the RHI has no external shader-file
// dependency. Behaviorally identical to src/platform/shaders/text.frag so
// GL legacy (TextRenderer) and RHI paths produce byte-identical pixels.
#ifdef __EMSCRIPTEN__
constexpr const char* kUiVertSrc = R"(#version 300 es
precision highp float;
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
out vec2 vUV;
void main() {
	gl_Position = vec4(aPos, 0.0, 1.0);
	vUV = aUV;
}
)";
constexpr const char* kUiFragSrc = R"(#version 300 es
precision highp float;
in vec2 vUV;
uniform sampler2D uFontTex;
uniform vec3 uColor;
uniform float uAlpha;
uniform int uMode;
out vec4 fragColor;
void main() {
	if (uMode == 1) { fragColor = vec4(uColor, uAlpha); return; }
	float dist = texture(uFontTex, vUV).r;
	float smoothing = fwidth(dist) * 1.2 + 0.02;
	float edge = 0.5;
	if (uMode == 0) {
		float alpha = smoothstep(edge - smoothing, edge + smoothing, dist);
		if (alpha < 0.01) discard;
		fragColor = vec4(uColor, uAlpha * alpha);
	} else {
		float fillAlpha = smoothstep(edge - smoothing, edge + smoothing, dist);
		float outlineEdge = 0.33;
		float outlineAlpha = smoothstep(outlineEdge - smoothing, outlineEdge + smoothing, dist);
		vec3 outlineColor = uColor * 0.15;
		float glowEdge = 0.18;
		float glowAlpha = smoothstep(glowEdge, edge, dist) * 0.25;
		vec3 glowColor = uColor * 0.5;
		vec3 color = glowColor;
		float alpha = glowAlpha;
		color = mix(color, outlineColor, outlineAlpha);
		alpha = max(alpha, outlineAlpha * 0.85);
		color = mix(color, uColor, fillAlpha);
		alpha = max(alpha, fillAlpha);
		if (alpha < 0.01) discard;
		fragColor = vec4(color, uAlpha * alpha);
	}
}
)";
#else
constexpr const char* kUiVertSrc = R"(#version 410 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
out vec2 vUV;
void main() {
	gl_Position = vec4(aPos, 0.0, 1.0);
	vUV = aUV;
}
)";
constexpr const char* kUiFragSrc = R"(#version 410 core
in vec2 vUV;
uniform sampler2D uFontTex;
uniform vec3 uColor;
uniform float uAlpha;
uniform int uMode;
out vec4 fragColor;
void main() {
	if (uMode == 1) { fragColor = vec4(uColor, uAlpha); return; }
	float dist = texture(uFontTex, vUV).r;
	float smoothing = fwidth(dist) * 1.2 + 0.02;
	float edge = 0.5;
	if (uMode == 0) {
		float alpha = smoothstep(edge - smoothing, edge + smoothing, dist);
		if (alpha < 0.01) discard;
		fragColor = vec4(uColor, uAlpha * alpha);
	} else {
		float fillAlpha = smoothstep(edge - smoothing, edge + smoothing, dist);
		float outlineEdge = 0.33;
		float outlineAlpha = smoothstep(outlineEdge - smoothing, outlineEdge + smoothing, dist);
		vec3 outlineColor = uColor * 0.15;
		float glowEdge = 0.18;
		float glowAlpha = smoothstep(glowEdge, edge, dist) * 0.25;
		vec3 glowColor = uColor * 0.5;
		vec3 color = glowColor;
		float alpha = glowAlpha;
		color = mix(color, outlineColor, outlineAlpha);
		alpha = max(alpha, outlineAlpha * 0.85);
		color = mix(color, uColor, fillAlpha);
		alpha = max(alpha, fillAlpha);
		if (alpha < 0.01) discard;
		fragColor = vec4(color, uAlpha * alpha);
	}
}
)";
#endif

} // namespace

class GlRhi : public IRhi {
public:
	bool init(const InitInfo& info) override {
		m_window = info.window;
		m_width = info.width;
		m_height = info.height;

#ifndef __EMSCRIPTEN__
		int version = gladLoadGL(glfwGetProcAddress);
		if (!version) {
			fprintf(stderr, "[gl-rhi] gladLoadGL failed\n");
			return false;
		}
		printf("[gl-rhi] OpenGL %d.%d loaded\n",
			GLAD_VERSION_MAJOR(version), GLAD_VERSION_MINOR(version));
#endif

		glEnable(GL_DEPTH_TEST);
		glEnable(GL_CULL_FACE);
		glCullFace(GL_BACK);

		if (!createUi2DResources()) return false;

		return true;
	}

	void shutdown() override {
		if (m_uiVao) glDeleteVertexArrays(1, &m_uiVao);
		if (m_uiVbo) glDeleteBuffers(1, &m_uiVbo);
		if (m_fontTex) glDeleteTextures(1, &m_fontTex);
		if (m_uiProgram) glDeleteProgram(m_uiProgram);
		m_uiVao = m_uiVbo = m_fontTex = m_uiProgram = 0;
	}

	void onResize(int width, int height) override {
		m_width = width;
		m_height = height;
		glViewport(0, 0, width, height);
	}

	bool beginFrame() override {
		glClearColor(0.12f, 0.12f, 0.15f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		return true;
	}

	void endFrame() override {
		glfwSwapBuffers(m_window);
	}

	bool initImGui() override {
#ifdef __EMSCRIPTEN__
		const char* glslVer = "#version 300 es";
#else
		const char* glslVer = "#version 410";
#endif
		ImGui_ImplGlfw_InitForOpenGL(m_window, true);
		ImGui_ImplOpenGL3_Init(glslVer);
		m_imguiInited = true;
		return true;
	}

	void shutdownImGui() override {
		if (!m_imguiInited) return;
		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		m_imguiInited = false;
	}

	void imguiNewFrame() override {
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
	}

	void imguiRender() override {
		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	}

	// ── Phase 0 stubs for draw methods (GL game renders directly) ──

	void drawCube(const float[16]) override {}
	void drawSky(const float[16], const float[3], float) override {}
	void drawVoxels(const SceneParams&, const float*, uint32_t) override {}
	void drawBoxModel(const SceneParams&, const float*, uint32_t) override {}
	void renderShadows(const float[16], const float*, uint32_t) override {}
	void renderBoxShadows(const float[16], const float*, uint32_t) override {}
	void drawParticles(const SceneParams&, const float*, uint32_t) override {}
	void drawRibbon(const SceneParams&, const float*, uint32_t) override {}

	// Persistent voxel meshes — GL backend stub. The legacy GL game uses its
	// own VBO path (Renderer + ChunkMesher) and bypasses the RHI for terrain;
	// these are here only so the interface stays uniform across backends.
	MeshHandle createVoxelMesh(const float*, uint32_t) override { return kInvalidMesh; }
	void       destroyMesh(MeshHandle) override {}
	void       drawVoxelsMesh(const SceneParams&, MeshHandle) override {}
	void       renderShadowsMesh(const float[16], MeshHandle) override {}

	void drawUi2D(const float* verts, uint32_t vertCount,
	              int mode, const float rgba[4]) override {
		if (!verts || vertCount == 0 || !m_uiProgram || !m_uiVao) return;

		// Save GL state we're going to clobber. The surrounding renderer
		// assumes depth test / cull / blend state doesn't change behind its
		// back between draws.
		GLboolean prevDepthTest = glIsEnabled(GL_DEPTH_TEST);
		GLboolean prevBlend     = glIsEnabled(GL_BLEND);
		GLboolean prevCull      = glIsEnabled(GL_CULL_FACE);
		GLint prevBlendSrc = 0, prevBlendDst = 0;
		glGetIntegerv(GL_BLEND_SRC_ALPHA, &prevBlendSrc);
		glGetIntegerv(GL_BLEND_DST_ALPHA, &prevBlendDst);

		glDisable(GL_DEPTH_TEST);
		glDisable(GL_CULL_FACE);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		glUseProgram(m_uiProgram);
		glUniform3f(m_uLocColor, rgba[0], rgba[1], rgba[2]);
		glUniform1f(m_uLocAlpha, rgba[3]);
		glUniform1i(m_uLocMode,  mode);
		glUniform1i(m_uLocFont,  0);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, m_fontTex);

		glBindVertexArray(m_uiVao);
		glBindBuffer(GL_ARRAY_BUFFER, m_uiVbo);
		const GLsizeiptr bytes = static_cast<GLsizeiptr>(vertCount) * 4 * sizeof(float);
		glBufferData(GL_ARRAY_BUFFER, bytes, verts, GL_DYNAMIC_DRAW);
		glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertCount));

		// Restore state.
		if (!prevBlend)     glDisable(GL_BLEND);
		else                glBlendFunc(prevBlendSrc, prevBlendDst);
		if (prevDepthTest)  glEnable(GL_DEPTH_TEST);
		if (prevCull)       glEnable(GL_CULL_FACE);
	}

	bool screenshot(const char* path) override {
		int w = m_width, h = m_height;
		std::vector<unsigned char> pixels(w * h * 3);
		glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

		// GL reads bottom-up; flip vertically for PPM.
		std::ofstream out(path, std::ios::binary);
		if (!out) return false;
		out << "P6\n" << w << " " << h << "\n255\n";
		for (int y = h - 1; y >= 0; y--)
			out.write((const char*)&pixels[y * w * 3], w * 3);
		printf("[gl-rhi] Screenshot: %s\n", path);
		return true;
	}

private:
	bool createUi2DResources() {
		// Compile + link the UI shader.
		GLuint vs = compileStage(GL_VERTEX_SHADER, kUiVertSrc, "ui2d.vert");
		GLuint fs = compileStage(GL_FRAGMENT_SHADER, kUiFragSrc, "ui2d.frag");
		if (!vs || !fs) return false;
		m_uiProgram = linkProgram(vs, fs, "ui2d");
		if (!m_uiProgram) return false;

		m_uLocColor = glGetUniformLocation(m_uiProgram, "uColor");
		m_uLocAlpha = glGetUniformLocation(m_uiProgram, "uAlpha");
		m_uLocMode  = glGetUniformLocation(m_uiProgram, "uMode");
		m_uLocFont  = glGetUniformLocation(m_uiProgram, "uFontTex");

		// Upload the shared SDF font atlas (byte-identical to what
		// TextRenderer uploads — single source of truth in ui_font_8x8.h).
		std::vector<uint8_t> sdf;
		generateUiFontAtlas(sdf);

		glGenTextures(1, &m_fontTex);
		glBindTexture(GL_TEXTURE_2D, m_fontTex);
#ifdef __EMSCRIPTEN__
		// WebGL 2 requires sized internal format.
		glTexImage2D(GL_TEXTURE_2D, 0, GL_R8,
			kFontAtlasW, kFontAtlasH, 0,
			GL_RED, GL_UNSIGNED_BYTE, sdf.data());
#else
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RED,
			kFontAtlasW, kFontAtlasH, 0,
			GL_RED, GL_UNSIGNED_BYTE, sdf.data());
#endif
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		// Dynamic VBO + VAO for UI quads. Vertex format: {pos.xy, uv.xy}.
		glGenVertexArrays(1, &m_uiVao);
		glGenBuffers(1, &m_uiVbo);
		glBindVertexArray(m_uiVao);
		glBindBuffer(GL_ARRAY_BUFFER, m_uiVbo);
		// Seed with a modest size; drawUi2D reallocates via glBufferData as
		// needed. Typical HUD draws stay well under this.
		glBufferData(GL_ARRAY_BUFFER, 4096 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
			4 * sizeof(float), (void*)0);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
			4 * sizeof(float), (void*)(2 * sizeof(float)));
		glEnableVertexAttribArray(1);
		glBindVertexArray(0);
		return true;
	}

	GLFWwindow* m_window = nullptr;
	int m_width = 0, m_height = 0;
	bool m_imguiInited = false;

	// UI pipeline state.
	GLuint m_uiProgram = 0;
	GLuint m_fontTex   = 0;
	GLuint m_uiVao     = 0;
	GLuint m_uiVbo     = 0;
	GLint  m_uLocColor = -1, m_uLocAlpha = -1, m_uLocMode = -1, m_uLocFont = -1;
};

IRhi* createRhi() { return new GlRhi(); }

} // namespace civcraft::rhi
