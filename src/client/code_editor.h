#pragma once

/**
 * In-game Python code editor for behavior editing.
 *
 * Players can view and modify entity behavior code. The editor shows:
 *   - Line numbers in a gutter
 *   - Syntax-highlighted Python code
 *   - Cursor with blinking
 *   - Error messages (if behavior code crashes)
 *   - [Apply] [Cancel] [Reset] buttons
 *
 * This is the core "learn to code" interface of ModCraft.
 */

#include "client/text_input.h"
#include "client/text.h"
#include "shared/entity.h"
#include <string>
#include <GLFW/glfw3.h>

namespace modcraft {

class CodeEditor {
public:
	// Open editor for an entity's behavior
	void open(EntityId entityId, const std::string& sourceCode, const std::string& goalText);

	// Close editor without applying
	void close();

	bool isOpen() const { return m_open; }
	EntityId editingEntity() const { return m_entityId; }

	// Get edited source code (for Apply)
	std::string getCode() const { return m_input.getText(); }

	// Set error message (shown in red at bottom)
	void setError(const std::string& error) { m_error = error; }
	void clearError() { m_error.clear(); }

	// GLFW callbacks — forward from window
	void onChar(unsigned int codepoint) { if (m_open) m_input.onChar(codepoint); }
	void onKey(int key, int action, int mods);

	// Render the editor panel
	void render(TextRenderer& text, float aspect, float time);

	// Button results (checked by game loop)
	bool wantsApply() const { return m_apply; }
	bool wantsCancel() const { return m_cancel; }
	bool wantsReset() const { return m_reset; }
	void clearFlags() { m_apply = m_cancel = m_reset = false; }

private:
	// Syntax highlighting: classify a token
	enum TokenType { Normal, Keyword, String, Comment, Number, Builtin };
	TokenType classifyToken(const std::string& token) const;

	// Render a single line with syntax coloring
	void renderLine(TextRenderer& text, const std::string& line, float x, float y,
	                float scale, float aspect);

	bool m_open = false;
	EntityId m_entityId = 0;
	TextInput m_input;
	std::string m_error;
	std::string m_goalText;

	bool m_apply = false;
	bool m_cancel = false;
	bool m_reset = false;
};

} // namespace modcraft
