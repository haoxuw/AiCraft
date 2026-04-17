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
#include <string>
#include <unordered_map>
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

		if (m_skyVao) glDeleteVertexArrays(1, &m_skyVao);
		if (m_skyVbo) glDeleteBuffers(1, &m_skyVbo);
		if (m_skyProgram) glDeleteProgram(m_skyProgram);
		m_skyVao = m_skyVbo = m_skyProgram = 0;

		for (auto& [_, m] : m_chunkMeshes) {
			if (m.vbo) glDeleteBuffers(1, &m.vbo);
			if (m.vao) glDeleteVertexArrays(1, &m.vao);
		}
		m_chunkMeshes.clear();
		if (m_chunkProgram) glDeleteProgram(m_chunkProgram);
		m_chunkProgram = 0;
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

	void drawSky(const float invVP[16],
	             const float skyColor[3],
	             const float horizonColor[3],
	             const float sunDir[3],
	             float sunStrength,
	             float time) override {
		if (!m_skyProgram && !createSkyResources()) return;

		// Sky pass disables depth-test (full-screen quad at z=0.999) and
		// re-enables on exit so subsequent draws aren't broken.
		GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);
		GLboolean prevCull  = glIsEnabled(GL_CULL_FACE);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_CULL_FACE);

		glUseProgram(m_skyProgram);
		glUniformMatrix4fv(m_skyUInvVP, 1, GL_FALSE, invVP);
		glUniform3fv(m_skyUSky,     1, skyColor);
		glUniform3fv(m_skyUHorizon, 1, horizonColor);
		glUniform3fv(m_skyUSunDir,  1, sunDir);
		glUniform1f(m_skyUSunStr,   sunStrength);
		glUniform1f(m_skyUTime,     time);

		glBindVertexArray(m_skyVao);
		glDrawArrays(GL_TRIANGLES, 0, 6);
		glBindVertexArray(0);

		if (prevDepth) glEnable(GL_DEPTH_TEST);
		if (prevCull)  glEnable(GL_CULL_FACE);
	}

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
	void       updateVoxelMesh(MeshHandle, const float*, uint32_t) override {}
	void       destroyMesh(MeshHandle mesh) override {
		// Unified destroy — covers chunk meshes (the only persistent GL meshes
		// today). Voxel meshes aren't created on this backend, so nothing else
		// needs handling.
		auto it = m_chunkMeshes.find(mesh);
		if (it == m_chunkMeshes.end()) return;
		if (it->second.vbo) glDeleteBuffers(1, &it->second.vbo);
		if (it->second.vao) glDeleteVertexArrays(1, &it->second.vao);
		m_chunkMeshes.erase(it);
	}
	void       drawVoxelsMesh(const SceneParams&, MeshHandle) override {}
	void       renderShadowsMesh(const float[16], MeshHandle) override {}

	// ── Chunk meshes ────────────────────────────────────────────────────
	// 13-float vertex layout matches IRhi's spec and the chunk_mesher output;
	// terrain.vert/frag (moved out of CivCraft into platform/shaders/) carry
	// the per-vertex AO, fog, glow effects.
	MeshHandle createChunkMesh(const float* verts, uint32_t vertexCount) override {
		if (!m_chunkProgram && !createChunkResources()) return kInvalidMesh;
		ChunkMeshGL m{};
		m.vertexCount = vertexCount;
		glGenVertexArrays(1, &m.vao);
		glGenBuffers(1, &m.vbo);
		glBindVertexArray(m.vao);
		glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
		const GLsizeiptr bytes = (GLsizeiptr)vertexCount * 13 * sizeof(float);
		glBufferData(GL_ARRAY_BUFFER, bytes, verts, GL_DYNAMIC_DRAW);
		setupChunkVertexAttribs();
		glBindVertexArray(0);
		const MeshHandle h = ++m_nextMesh;
		m_chunkMeshes.emplace(h, m);
		return h;
	}
	void updateChunkMesh(MeshHandle mesh, const float* verts,
	                     uint32_t vertexCount) override {
		auto it = m_chunkMeshes.find(mesh);
		if (it == m_chunkMeshes.end()) return;
		it->second.vertexCount = vertexCount;
		glBindBuffer(GL_ARRAY_BUFFER, it->second.vbo);
		const GLsizeiptr bytes = (GLsizeiptr)vertexCount * 13 * sizeof(float);
		// glBufferData reallocates — fine on GL since there's no in-flight
		// frame to worry about; the next draw sees the new data immediately.
		glBufferData(GL_ARRAY_BUFFER, bytes, verts, GL_DYNAMIC_DRAW);
	}
	void drawChunkMeshOpaque(const SceneParams& scene,
	                         const float fogColor[3],
	                         float fogStart, float fogEnd,
	                         MeshHandle mesh) override {
		drawChunkMeshInternal(scene, fogColor, fogStart, fogEnd, mesh,
		                      /*transparent=*/false);
	}
	void drawChunkMeshTransparent(const SceneParams& scene,
	                              const float fogColor[3],
	                              float fogStart, float fogEnd,
	                              MeshHandle mesh) override {
		drawChunkMeshInternal(scene, fogColor, fogStart, fogEnd, mesh,
		                      /*transparent=*/true);
	}
	// GL Renderer doesn't drive a shadow map — civcraft-ui's main client uses
	// per-entity oval discs instead of cascaded shadows. No-op here keeps the
	// interface uniform with VK.
	void renderShadowsChunkMesh(const float[16], MeshHandle) override {}

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

	void setGrading(const GradingParams&) override {
		// GL backend has no composite pass yet — grading tuning is VK-only.
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
	// Read a text file fully into a std::string. Returns empty string on
	// failure; caller treats that as "shader source not found, sky pass
	// becomes a no-op." Loaded lazily so an init-time failure doesn't crash
	// callers that don't draw a sky (model_editor etc).
	static std::string slurp(const char* path) {
		std::ifstream f(path, std::ios::binary);
		if (!f) return {};
		std::string out((std::istreambuf_iterator<char>(f)),
		                 std::istreambuf_iterator<char>());
		return out;
	}

	bool createSkyResources() {
		// Load CivCraft's sky shader from the platform shader dir (moved out
		// of src/CivCraft/shaders/ as part of Phase 3 — the platform RHI now
		// owns the sky pass for all GL clients).
		std::string vsSrc = slurp("shaders/sky.vert");
		std::string fsSrc = slurp("shaders/sky.frag");
		if (vsSrc.empty() || fsSrc.empty()) {
			fprintf(stderr, "[gl-rhi] sky shader source not found "
				"(shaders/sky.{vert,frag}); drawSky disabled\n");
			return false;
		}

		GLuint vs = compileStage(GL_VERTEX_SHADER, vsSrc.c_str(), "sky.vert");
		GLuint fs = compileStage(GL_FRAGMENT_SHADER, fsSrc.c_str(), "sky.frag");
		if (!vs || !fs) return false;
		m_skyProgram = linkProgram(vs, fs, "sky");
		if (!m_skyProgram) return false;

		m_skyUInvVP   = glGetUniformLocation(m_skyProgram, "uInvVP");
		m_skyUSky     = glGetUniformLocation(m_skyProgram, "uSkyColor");
		m_skyUHorizon = glGetUniformLocation(m_skyProgram, "uHorizonColor");
		m_skyUSunDir  = glGetUniformLocation(m_skyProgram, "uSunDir");
		m_skyUSunStr  = glGetUniformLocation(m_skyProgram, "uSunStrength");
		m_skyUTime    = glGetUniformLocation(m_skyProgram, "uTime");

		// Fullscreen quad — two triangles in NDC. Sky vertex shader pushes
		// z to 0.999 so the quad sits behind everything.
		const float quad[] = { -1,-1, 1,-1, 1,1, -1,-1, 1,1, -1,1 };
		glGenVertexArrays(1, &m_skyVao);
		glGenBuffers(1, &m_skyVbo);
		glBindVertexArray(m_skyVao);
		glBindBuffer(GL_ARRAY_BUFFER, m_skyVbo);
		glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
		glEnableVertexAttribArray(0);
		glBindVertexArray(0);
		return true;
	}

	// Bind the 13-float chunk-vertex layout on the currently-bound VAO/VBO.
	// Matches IRhi's documented format: pos / color / normal / ao / shade /
	// alpha / glow at locations 0..6.
	static void setupChunkVertexAttribs() {
		const GLsizei stride = 13 * sizeof(float);
		auto setF = [&](GLuint loc, GLint n, size_t off) {
			glVertexAttribPointer(loc, n, GL_FLOAT, GL_FALSE, stride,
				(void*)(off * sizeof(float)));
			glEnableVertexAttribArray(loc);
		};
		setF(0, 3, 0);   // position
		setF(1, 3, 3);   // color
		setF(2, 3, 6);   // normal
		setF(3, 1, 9);   // ao
		setF(4, 1, 10);  // shade
		setF(5, 1, 11);  // alpha
		setF(6, 1, 12);  // glow
	}

	bool createChunkResources() {
		std::string vsSrc = slurp("shaders/terrain.vert");
		std::string fsSrc = slurp("shaders/terrain.frag");
		if (vsSrc.empty() || fsSrc.empty()) {
			fprintf(stderr, "[gl-rhi] terrain shader source not found "
				"(shaders/terrain.{vert,frag}); chunk meshes disabled\n");
			return false;
		}
		GLuint vs = compileStage(GL_VERTEX_SHADER,   vsSrc.c_str(), "terrain.vert");
		GLuint fs = compileStage(GL_FRAGMENT_SHADER, fsSrc.c_str(), "terrain.frag");
		if (!vs || !fs) return false;
		m_chunkProgram = linkProgram(vs, fs, "terrain");
		if (!m_chunkProgram) return false;

		m_chunkUViewProj = glGetUniformLocation(m_chunkProgram, "uViewProj");
		m_chunkUSunDir   = glGetUniformLocation(m_chunkProgram, "uSunDir");
		m_chunkUCamPos   = glGetUniformLocation(m_chunkProgram, "uCamPos");
		m_chunkUFogColor = glGetUniformLocation(m_chunkProgram, "uFogColor");
		m_chunkUFogStart = glGetUniformLocation(m_chunkProgram, "uFogStart");
		m_chunkUFogEnd   = glGetUniformLocation(m_chunkProgram, "uFogEnd");
		m_chunkUSunStr   = glGetUniformLocation(m_chunkProgram, "uSunStrength");
		m_chunkUTime     = glGetUniformLocation(m_chunkProgram, "uTime");
		return true;
	}

	void drawChunkMeshInternal(const SceneParams& scene,
	                           const float fogColor[3],
	                           float fogStart, float fogEnd,
	                           MeshHandle mesh, bool transparent) {
		if (!m_chunkProgram) return;
		auto it = m_chunkMeshes.find(mesh);
		if (it == m_chunkMeshes.end() || it->second.vertexCount == 0) return;

		// Pass-specific GL state — opaque keeps full depth + back-face cull;
		// transparent disables depth writes, enables alpha blend, and shows
		// both faces (thin glass / portals).
		glEnable(GL_DEPTH_TEST);
		if (transparent) {
			glDepthMask(GL_FALSE);
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glDisable(GL_CULL_FACE);
		} else {
			glDepthMask(GL_TRUE);
			glDisable(GL_BLEND);
			glEnable(GL_CULL_FACE);
			glCullFace(GL_BACK);
			glFrontFace(GL_CCW);
		}

		glUseProgram(m_chunkProgram);
		glUniformMatrix4fv(m_chunkUViewProj, 1, GL_FALSE, scene.viewProj);
		glUniform3fv(m_chunkUSunDir,   1, scene.sunDir);
		glUniform3fv(m_chunkUCamPos,   1, scene.camPos);
		glUniform3fv(m_chunkUFogColor, 1, fogColor);
		glUniform1f(m_chunkUFogStart,  fogStart);
		glUniform1f(m_chunkUFogEnd,    fogEnd);
		glUniform1f(m_chunkUSunStr,    scene.sunStr);
		glUniform1f(m_chunkUTime,      scene.time);

		glBindVertexArray(it->second.vao);
		glDrawArrays(GL_TRIANGLES, 0, (GLsizei)it->second.vertexCount);
		glBindVertexArray(0);

		// Restore the assumed default state so passes downstream don't see
		// transparent's depth-mask-off / blend-on bleed-through.
		if (transparent) {
			glDepthMask(GL_TRUE);
			glDisable(GL_BLEND);
			glEnable(GL_CULL_FACE);
		}
	}

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

	// Sky pipeline (lazily initialized on first drawSky call).
	GLuint m_skyProgram = 0;
	GLuint m_skyVao     = 0;
	GLuint m_skyVbo     = 0;
	GLint  m_skyUInvVP   = -1;
	GLint  m_skyUSky     = -1;
	GLint  m_skyUHorizon = -1;
	GLint  m_skyUSunDir  = -1;
	GLint  m_skyUSunStr  = -1;
	GLint  m_skyUTime    = -1;

	// Chunk-mesh pipeline (lazy on first createChunkMesh).
	GLuint m_chunkProgram = 0;
	GLint  m_chunkUViewProj = -1;
	GLint  m_chunkUSunDir   = -1;
	GLint  m_chunkUCamPos   = -1;
	GLint  m_chunkUFogColor = -1;
	GLint  m_chunkUFogStart = -1;
	GLint  m_chunkUFogEnd   = -1;
	GLint  m_chunkUSunStr   = -1;
	GLint  m_chunkUTime     = -1;
	struct ChunkMeshGL { GLuint vao = 0, vbo = 0; uint32_t vertexCount = 0; };
	std::unordered_map<MeshHandle, ChunkMeshGL> m_chunkMeshes;
	MeshHandle m_nextMesh = 0;
};

IRhi* createRhi() { return new GlRhi(); }

} // namespace civcraft::rhi
