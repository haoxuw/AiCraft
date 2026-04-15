#pragma once

/**
 * ModelEditorUI — in-game model editor panel.
 *
 * Hosted inside the Handbook detail view: when the user clicks Edit on a
 * model entry, this panel takes over and exposes per-part numeric
 * controls + a live ModelPreview. Saving regenerates the .py via
 * model_writer::emitModelPy.
 *
 * Native: writes to artifacts/models/{base|player}/<name>.py directly.
 * WASM:   triggers a browser download via Emscripten — the user drops
 *         the file back into their fork.
 *
 * Architectural notes:
 *   - Edits mutate a working copy (BoxModel) — the live game's m_models
 *     are NOT touched, so changes only land after restart (or future
 *     hot-reload). This keeps the editor crash-safe and avoids fighting
 *     the simulation.
 *   - Preview uses the same ModelPreview FBO the Handbook already owns,
 *     so we get free animation playback and clip selection.
 */

#include "client/box_model.h"
#include "client/model.h"
#include "client/model_preview.h"
#include "client/model_writer.h"
#include <imgui.h>
#include <cstdio>
#include <fstream>
#include <string>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace civcraft {

class ModelEditorUI {
public:
	void setPreview(ModelPreview* p, ModelRenderer* r) {
		m_preview = p;
		m_renderer = r;
	}

	// Load a model into the editor. `path` is the .py path on disk
	// (used both for save and for "where it came from" display).
	void open(const std::string& id, const BoxModel& src, const std::string& path) {
		m_id = id;
		m_working = src;
		m_path = path;
		m_selectedPart = m_working.parts.empty() ? -1 : 0;
		m_status.clear();
		m_statusTimer = 0;
	}

	bool isOpen() const { return !m_id.empty(); }
	void close() { m_id.clear(); }

	void render() {
		if (!isOpen() || !m_preview || !m_renderer) return;

		float dt = ImGui::GetIO().DeltaTime;

		// Header strip with id, path, close.
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.20f, 0.22f, 0.26f, 1));
		ImGui::SetWindowFontScale(1.15f);
		ImGui::Text("Editing: %s", m_id.c_str());
		ImGui::SetWindowFontScale(1.0f);
		ImGui::PopStyleColor();
		ImGui::TextColored(ImVec4(0.55f, 0.57f, 0.60f, 1), "%s", m_path.c_str());
		ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60);
		if (ImGui::SmallButton("Close")) { close(); return; }
		ImGui::Separator();

		// Two-pane layout: preview + part list (left), part editor (right).
		float paneW = ImGui::GetContentRegionAvail().x;
		float leftW = paneW * 0.42f;

		ImGui::BeginChild("EditorLeft", ImVec2(leftW, 0), false);
		m_preview->render(*m_renderer, m_working, dt, leftW - 20);
		ImGui::Separator();
		ImGui::TextColored(ImVec4(0.50f, 0.52f, 0.56f, 1), "Parts (%zu)", m_working.parts.size());
		ImGui::BeginChild("PartList", ImVec2(0, 200), true);
		for (int i = 0; i < (int)m_working.parts.size(); i++) {
			auto& p = m_working.parts[i];
			char label[64];
			snprintf(label, sizeof(label), "%d %s", i,
				p.name.empty() ? "(unnamed)" : p.name.c_str());
			if (ImGui::Selectable(label, m_selectedPart == i))
				m_selectedPart = i;
		}
		ImGui::EndChild();

		// Add / remove buttons.
		if (ImGui::SmallButton("+ Cube")) {
			BodyPart np;
			np.offset = glm::vec3(0, 1, 0);
			np.halfSize = glm::vec3(0.1f, 0.1f, 0.1f);
			np.color = glm::vec4(0.7f, 0.7f, 0.7f, 1);
			m_working.parts.push_back(np);
			m_selectedPart = (int)m_working.parts.size() - 1;
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Delete") && m_selectedPart >= 0 &&
		    m_selectedPart < (int)m_working.parts.size()) {
			m_working.parts.erase(m_working.parts.begin() + m_selectedPart);
			if (m_selectedPart >= (int)m_working.parts.size())
				m_selectedPart = (int)m_working.parts.size() - 1;
		}
		ImGui::EndChild();

		ImGui::SameLine();

		// Right pane: editor for the selected part.
		ImGui::BeginChild("EditorRight", ImVec2(0, 0), true);
		if (m_selectedPart < 0 || m_selectedPart >= (int)m_working.parts.size()) {
			ImGui::TextColored(ImVec4(0.50f, 0.52f, 0.56f, 1), "Select a part to edit.");
		} else {
			renderPartEditor(m_working.parts[m_selectedPart]);
		}
		ImGui::Separator();
		ImGui::Spacing();
		renderModelLevelEditor();
		ImGui::Separator();
		ImGui::Spacing();

		// Save button.
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.65f, 0.35f, 1));
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
		if (ImGui::Button("Save", ImVec2(120, 32))) save();
		ImGui::PopStyleColor(2);
		if (m_statusTimer > 0) {
			ImGui::SameLine();
			ImVec4 col = m_saveOk ? ImVec4(0.20f, 0.70f, 0.30f, 1)
			                      : ImVec4(0.85f, 0.30f, 0.30f, 1);
			ImGui::TextColored(col, "%s", m_status.c_str());
			m_statusTimer -= dt;
		}
		ImGui::EndChild();
	}

private:
	void renderPartEditor(BodyPart& p) {
		char nameBuf[64];
		snprintf(nameBuf, sizeof(nameBuf), "%s", p.name.c_str());
		if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
			p.name = nameBuf;
		char roleBuf[64];
		snprintf(roleBuf, sizeof(roleBuf), "%s", p.role.c_str());
		if (ImGui::InputText("Role", roleBuf, sizeof(roleBuf)))
			p.role = roleBuf;
		ImGui::Checkbox("Head (tracks lookYaw/Pitch)", &p.isHead);

		ImGui::Spacing();
		ImGui::TextColored(ImVec4(0.50f, 0.52f, 0.56f, 1), "Geometry");
		ImGui::DragFloat3("Offset (center)", &p.offset.x, 0.01f, -10.0f, 10.0f, "%.3f");
		// Edit full size; halfSize is the loader-side rep.
		glm::vec3 fullSize = p.halfSize * 2.0f;
		if (ImGui::DragFloat3("Size (full)", &fullSize.x, 0.01f, 0.001f, 10.0f, "%.3f"))
			p.halfSize = fullSize * 0.5f;
		ImGui::DragFloat3("Pivot", &p.pivot.x, 0.01f, -10.0f, 10.0f, "%.3f");
		ImGui::ColorEdit4("Color", &p.color.x);

		ImGui::Spacing();
		ImGui::TextColored(ImVec4(0.50f, 0.52f, 0.56f, 1), "Walk swing");
		ImGui::DragFloat3("Axis", &p.swingAxis.x, 0.05f, -1.0f, 1.0f, "%.2f");
		ImGui::DragFloat("Amplitude (deg)", &p.swingAmplitude, 0.5f, 0.0f, 180.0f, "%.1f");
		ImGui::DragFloat("Phase (rad)", &p.swingPhase, 0.05f, -6.3f, 6.3f, "%.2f");
		ImGui::DragFloat("Speed", &p.swingSpeed, 0.05f, 0.0f, 10.0f, "%.2f");
	}

	void renderModelLevelEditor() {
		ImGui::TextColored(ImVec4(0.50f, 0.52f, 0.56f, 1), "Model");
		ImGui::DragFloat("Total height", &m_working.totalHeight, 0.01f, 0.1f, 10.0f, "%.2f");
		ImGui::DragFloat("Scale", &m_working.modelScale, 0.01f, 0.1f, 10.0f, "%.2f");
		ImGui::DragFloat3("Head pivot", &m_working.headPivot.x, 0.01f, -5.0f, 5.0f, "%.2f");
		ImGui::DragFloat3("Right hand", &m_working.handR.x,  0.01f, -5.0f, 5.0f, "%.3f");
		ImGui::DragFloat3("Left hand",  &m_working.handL.x,  0.01f, -5.0f, 5.0f, "%.3f");
		ImGui::DragFloat3("Right pivot", &m_working.pivotR.x, 0.01f, -5.0f, 5.0f, "%.3f");
		ImGui::DragFloat3("Left pivot",  &m_working.pivotL.x, 0.01f, -5.0f, 5.0f, "%.3f");
	}

	void save() {
		std::string py = model_writer::emitModelPy(m_working, m_id);
#ifdef __EMSCRIPTEN__
		// Browser: trigger download via JS shim.
		std::string fname = m_id + ".py";
		EM_ASM({
			const text = UTF8ToString($0);
			const name = UTF8ToString($1);
			const blob = new Blob([text], {type: "text/plain"});
			const url = URL.createObjectURL(blob);
			const a = document.createElement("a");
			a.href = url; a.download = name;
			document.body.appendChild(a); a.click();
			document.body.removeChild(a);
			URL.revokeObjectURL(url);
		}, py.c_str(), fname.c_str());
		m_status = "Downloaded " + fname;
		m_saveOk = true;
		m_statusTimer = 4.0f;
#else
		std::ofstream f(m_path);
		if (!f.is_open()) {
			m_status = "Failed to open " + m_path;
			m_saveOk = false;
		} else {
			f << py;
			m_status = "Saved " + m_path + " — rebuild to see in-world.";
			m_saveOk = true;
		}
		m_statusTimer = 5.0f;
#endif
	}

	std::string m_id;
	std::string m_path;
	BoxModel m_working;
	int m_selectedPart = -1;
	ModelPreview* m_preview = nullptr;
	ModelRenderer* m_renderer = nullptr;
	std::string m_status;
	float m_statusTimer = 0;
	bool m_saveOk = true;
};

} // namespace civcraft
