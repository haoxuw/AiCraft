#include "client/code_editor.h"
#include <cstdio>
#include <cmath>
#include <set>

namespace agentica {

// Python keywords for syntax highlighting
static const std::set<std::string> KEYWORDS = {
	"def", "class", "if", "elif", "else", "for", "while", "return",
	"import", "from", "as", "in", "not", "and", "or", "is",
	"True", "False", "None", "self", "pass", "break", "continue",
	"try", "except", "finally", "raise", "with", "lambda", "yield",
};

static const std::set<std::string> BUILTINS = {
	"print", "len", "range", "int", "float", "str", "list", "dict",
	"min", "max", "abs", "sum", "sorted", "enumerate", "zip",
	"Idle", "Wander", "MoveTo", "Follow", "Flee", "Attack",
};

void CodeEditor::open(EntityId entityId, const std::string& sourceCode,
                       const std::string& goalText) {
	m_entityId = entityId;
	m_input.setText(sourceCode);
	m_goalText = goalText;
	m_error.clear();
	m_open = true;
	m_apply = m_cancel = m_reset = false;
	m_input.setVisibleLines(18);
}

void CodeEditor::close() {
	m_open = false;
	m_entityId = 0;
}

void CodeEditor::onKey(int key, int action, int mods) {
	if (!m_open) return;

	// Ctrl+Enter = Apply
	if (key == 257 && action == 1 && (mods & 2)) { // GLFW_MOD_CONTROL
		m_apply = true;
		return;
	}

	// Escape = Cancel
	if (key == 256 && action == 1) { // GLFW_KEY_ESCAPE
		m_cancel = true;
		return;
	}

	// Forward to text input
	m_input.onKey(key, action, mods);
}

CodeEditor::TokenType CodeEditor::classifyToken(const std::string& token) const {
	if (KEYWORDS.count(token)) return Keyword;
	if (BUILTINS.count(token)) return Builtin;

	// Numbers
	if (!token.empty() && (token[0] >= '0' && token[0] <= '9'))
		return Number;

	return Normal;
}

void CodeEditor::renderLine(TextRenderer& text, const std::string& line,
                             float x, float y, float scale, float aspect) {
	// Simple token-based syntax highlighting
	size_t i = 0;
	float charW = scale * 0.012f / aspect; // approximate character width

	while (i < line.size()) {
		// Comment: # to end of line
		if (line[i] == '#') {
			std::string rest = line.substr(i);
			text.drawText(rest.c_str(), x + i * charW, y, scale, {0.5f, 0.5f, 0.5f, 0.9f}, aspect);
			return;
		}

		// String: ' or "
		if (line[i] == '\'' || line[i] == '"') {
			char quote = line[i];
			size_t end = line.find(quote, i + 1);
			if (end == std::string::npos) end = line.size() - 1;
			std::string str = line.substr(i, end - i + 1);
			text.drawText(str.c_str(), x + i * charW, y, scale, {0.4f, 0.9f, 0.4f, 1.0f}, aspect);
			i = end + 1;
			continue;
		}

		// Identifier/keyword
		if ((line[i] >= 'a' && line[i] <= 'z') || (line[i] >= 'A' && line[i] <= 'Z') || line[i] == '_') {
			size_t start = i;
			while (i < line.size() && ((line[i] >= 'a' && line[i] <= 'z') ||
			       (line[i] >= 'A' && line[i] <= 'Z') || (line[i] >= '0' && line[i] <= '9') ||
			       line[i] == '_'))
				i++;
			std::string token = line.substr(start, i - start);
			TokenType tt = classifyToken(token);

			glm::vec4 color;
			switch (tt) {
			case Keyword:  color = {0.5f, 0.7f, 1.0f, 1.0f}; break; // blue
			case Builtin:  color = {1.0f, 0.8f, 0.4f, 1.0f}; break; // orange
			case Number:   color = {0.9f, 0.6f, 0.3f, 1.0f}; break; // orange
			default:       color = {0.9f, 0.9f, 0.9f, 1.0f}; break; // white
			}

			text.drawText(token.c_str(), x + start * charW, y, scale, color, aspect);
			continue;
		}

		// Number
		if (line[i] >= '0' && line[i] <= '9') {
			size_t start = i;
			while (i < line.size() && ((line[i] >= '0' && line[i] <= '9') || line[i] == '.'))
				i++;
			std::string num = line.substr(start, i - start);
			text.drawText(num.c_str(), x + start * charW, y, scale, {0.9f, 0.6f, 0.3f, 1.0f}, aspect);
			continue;
		}

		// Single character (operator, punctuation, space)
		char ch[2] = {line[i], 0};
		glm::vec4 color = {0.8f, 0.8f, 0.8f, 1.0f};
		if (line[i] == '(' || line[i] == ')' || line[i] == '[' || line[i] == ']' ||
		    line[i] == '{' || line[i] == '}')
			color = {0.7f, 0.7f, 1.0f, 1.0f};
		text.drawText(ch, x + i * charW, y, scale, color, aspect);
		i++;
	}
}

void CodeEditor::render(TextRenderer& text, float aspect, float time) {
	if (!m_open) return;

	float scale = 0.55f;
	float charW = scale * 0.012f / aspect;
	float lineH = 0.035f;

	// Panel dimensions
	float panelW = 0.85f;
	float panelH = 0.88f;
	float px = -panelW / 2;
	float py = -panelH / 2;

	// Background
	text.drawRect(px, py, panelW, panelH, {0.08f, 0.08f, 0.12f, 0.95f});

	// Title bar
	text.drawRect(px, py + panelH - 0.045f, panelW, 0.045f, {0.15f, 0.15f, 0.25f, 1.0f});
	char title[128];
	snprintf(title, sizeof(title), "Behavior Editor — Entity #%u", m_entityId);
	text.drawText(title, px + 0.02f, py + panelH - 0.035f, 0.65f, {1, 0.9f, 0.5f, 1}, aspect);

	// Goal display
	float goalY = py + panelH - 0.075f;
	char goalBuf[128];
	snprintf(goalBuf, sizeof(goalBuf), "Goal: %s", m_goalText.c_str());
	text.drawText(goalBuf, px + 0.02f, goalY, 0.55f, {0.5f, 1.0f, 0.8f, 0.9f}, aspect);

	// Code area
	float codeTop = goalY - 0.02f;
	float codeBottom = py + 0.10f; // leave room for error + buttons
	float gutterW = 0.04f;
	float codeX = px + gutterW + 0.01f;

	// Gutter background
	text.drawRect(px, codeBottom, gutterW, codeTop - codeBottom, {0.12f, 0.12f, 0.18f, 1.0f});

	// Render visible lines
	int visibleLines = (int)((codeTop - codeBottom) / lineH);
	m_input.setVisibleLines(visibleLines);
	int scrollRow = m_input.scrollRow();

	for (int i = 0; i < visibleLines && (scrollRow + i) < m_input.lineCount(); i++) {
		int lineIdx = scrollRow + i;
		float y = codeTop - (i + 1) * lineH;

		// Line number
		char lineNum[8];
		snprintf(lineNum, sizeof(lineNum), "%3d", lineIdx + 1);
		text.drawText(lineNum, px + 0.005f, y, 0.5f, {0.4f, 0.4f, 0.5f, 0.8f}, aspect);

		// Highlight current line
		if (lineIdx == m_input.cursorRow()) {
			text.drawRect(px + gutterW, y - 0.003f, panelW - gutterW, lineH,
			              {0.18f, 0.18f, 0.28f, 0.5f});
		}

		// Render line with syntax highlighting
		const std::string& line = m_input.lines()[lineIdx];
		renderLine(text, line, codeX, y, scale, aspect);

		// Cursor (blinking)
		if (lineIdx == m_input.cursorRow()) {
			bool blink = std::fmod(time * 2.0f, 2.0f) < 1.2f;
			if (blink) {
				float cursorX = codeX + m_input.cursorCol() * charW;
				text.drawRect(cursorX, y - 0.002f, 0.002f, lineH * 0.9f,
				              {1.0f, 1.0f, 1.0f, 0.9f});
			}
		}
	}

	// Error display (red bar at bottom of code area)
	if (!m_error.empty()) {
		float errY = codeBottom - 0.005f;
		text.drawRect(px, errY - lineH, panelW, lineH + 0.005f, {0.3f, 0.05f, 0.05f, 0.9f});
		// Truncate error to fit
		std::string errText = m_error;
		if (errText.size() > 80) errText = errText.substr(0, 80) + "...";
		text.drawText(errText.c_str(), px + 0.02f, errY - lineH + 0.005f, 0.5f,
		              {1.0f, 0.4f, 0.4f, 1.0f}, aspect);
	}

	// Button bar
	float btnY = py + 0.02f;
	float btnH = 0.04f;
	float btnW = 0.12f;
	float gap = 0.02f;

	// [Apply] button (green)
	float applyX = px + 0.02f;
	text.drawRect(applyX, btnY, btnW, btnH, {0.1f, 0.4f, 0.1f, 1.0f});
	text.drawText("Ctrl+Enter", applyX + 0.005f, btnY + 0.01f, 0.5f, {0.8f, 1.0f, 0.8f, 1.0f}, aspect);

	// [Cancel] button (gray)
	float cancelX = applyX + btnW + gap;
	text.drawRect(cancelX, btnY, btnW, btnH, {0.3f, 0.3f, 0.3f, 1.0f});
	text.drawText("ESC Cancel", cancelX + 0.005f, btnY + 0.01f, 0.5f, {0.9f, 0.9f, 0.9f, 1.0f}, aspect);

	// [Reset] button (orange)
	float resetX = cancelX + btnW + gap;
	text.drawRect(resetX, btnY, btnW + 0.02f, btnH, {0.4f, 0.25f, 0.05f, 1.0f});
	text.drawText("Reset Default", resetX + 0.005f, btnY + 0.01f, 0.5f, {1.0f, 0.8f, 0.5f, 1.0f}, aspect);

	// Line/column indicator
	char posInfo[32];
	snprintf(posInfo, sizeof(posInfo), "Ln %d, Col %d",
	         m_input.cursorRow() + 1, m_input.cursorCol() + 1);
	text.drawText(posInfo, px + panelW - 0.15f, btnY + 0.01f, 0.5f,
	              {0.5f, 0.5f, 0.5f, 0.8f}, aspect);
}

} // namespace agentica
