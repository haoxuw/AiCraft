#pragma once

/**
 * Multi-line text input for the in-game code editor.
 *
 * Handles GLFW character and key callbacks, maintains a buffer of text lines,
 * tracks cursor position, and supports basic editing operations.
 */

#include <string>
#include <vector>
#include <functional>
#include <algorithm>

namespace civcraft {

class TextInput {
public:
	// Set initial content (e.g., load behavior source code)
	void setText(const std::string& text) {
		m_lines.clear();
		m_lines.push_back("");
		for (char c : text) {
			if (c == '\n') m_lines.push_back("");
			else m_lines.back() += c;
		}
		if (m_lines.empty()) m_lines.push_back("");
		m_cursorRow = 0;
		m_cursorCol = 0;
		m_scrollRow = 0;
	}

	// Get all text as a single string
	std::string getText() const {
		std::string result;
		for (size_t i = 0; i < m_lines.size(); i++) {
			if (i > 0) result += '\n';
			result += m_lines[i];
		}
		return result;
	}

	// Handle character input (printable characters)
	void onChar(unsigned int codepoint) {
		if (codepoint < 32 || codepoint > 126) return;
		auto& line = m_lines[m_cursorRow];
		line.insert(line.begin() + m_cursorCol, (char)codepoint);
		m_cursorCol++;
	}

	// Handle key input (special keys)
	void onKey(int key, int action, int mods) {
		if (action != 1 && action != 2) return; // GLFW_PRESS or GLFW_REPEAT

		switch (key) {
		case 259: // GLFW_KEY_BACKSPACE
			if (m_cursorCol > 0) {
				m_lines[m_cursorRow].erase(m_cursorCol - 1, 1);
				m_cursorCol--;
			} else if (m_cursorRow > 0) {
				// Join with previous line
				m_cursorCol = (int)m_lines[m_cursorRow - 1].size();
				m_lines[m_cursorRow - 1] += m_lines[m_cursorRow];
				m_lines.erase(m_lines.begin() + m_cursorRow);
				m_cursorRow--;
			}
			break;

		case 261: // GLFW_KEY_DELETE
			if (m_cursorCol < (int)m_lines[m_cursorRow].size()) {
				m_lines[m_cursorRow].erase(m_cursorCol, 1);
			} else if (m_cursorRow < (int)m_lines.size() - 1) {
				m_lines[m_cursorRow] += m_lines[m_cursorRow + 1];
				m_lines.erase(m_lines.begin() + m_cursorRow + 1);
			}
			break;

		case 257: // GLFW_KEY_ENTER
			{
				std::string rest = m_lines[m_cursorRow].substr(m_cursorCol);
				m_lines[m_cursorRow] = m_lines[m_cursorRow].substr(0, m_cursorCol);

				// Auto-indent: copy leading whitespace from current line
				std::string indent;
				for (char c : m_lines[m_cursorRow])
					if (c == ' ' || c == '\t') indent += c;
					else break;

				m_lines.insert(m_lines.begin() + m_cursorRow + 1, indent + rest);
				m_cursorRow++;
				m_cursorCol = (int)indent.size();
			}
			break;

		case 258: // GLFW_KEY_TAB
			// Insert 4 spaces
			m_lines[m_cursorRow].insert(m_cursorCol, "    ");
			m_cursorCol += 4;
			break;

		case 263: // GLFW_KEY_LEFT
			if (m_cursorCol > 0) m_cursorCol--;
			else if (m_cursorRow > 0) {
				m_cursorRow--;
				m_cursorCol = (int)m_lines[m_cursorRow].size();
			}
			break;

		case 262: // GLFW_KEY_RIGHT
			if (m_cursorCol < (int)m_lines[m_cursorRow].size()) m_cursorCol++;
			else if (m_cursorRow < (int)m_lines.size() - 1) {
				m_cursorRow++;
				m_cursorCol = 0;
			}
			break;

		case 265: // GLFW_KEY_UP
			if (m_cursorRow > 0) {
				m_cursorRow--;
				m_cursorCol = std::min(m_cursorCol, (int)m_lines[m_cursorRow].size());
			}
			break;

		case 264: // GLFW_KEY_DOWN
			if (m_cursorRow < (int)m_lines.size() - 1) {
				m_cursorRow++;
				m_cursorCol = std::min(m_cursorCol, (int)m_lines[m_cursorRow].size());
			}
			break;

		case 268: // GLFW_KEY_HOME
			m_cursorCol = 0;
			break;

		case 269: // GLFW_KEY_END
			m_cursorCol = (int)m_lines[m_cursorRow].size();
			break;

		case 266: // GLFW_KEY_PAGE_UP
			m_cursorRow = std::max(0, m_cursorRow - m_visibleLines);
			m_cursorCol = std::min(m_cursorCol, (int)m_lines[m_cursorRow].size());
			break;

		case 267: // GLFW_KEY_PAGE_DOWN
			m_cursorRow = std::min((int)m_lines.size() - 1, m_cursorRow + m_visibleLines);
			m_cursorCol = std::min(m_cursorCol, (int)m_lines[m_cursorRow].size());
			break;
		}

		// Keep cursor in view
		ensureCursorVisible();
	}

	// Accessors
	const std::vector<std::string>& lines() const { return m_lines; }
	int cursorRow() const { return m_cursorRow; }
	int cursorCol() const { return m_cursorCol; }
	int scrollRow() const { return m_scrollRow; }
	int lineCount() const { return (int)m_lines.size(); }
	void setVisibleLines(int n) { m_visibleLines = n; }

private:
	void ensureCursorVisible() {
		if (m_cursorRow < m_scrollRow) m_scrollRow = m_cursorRow;
		if (m_cursorRow >= m_scrollRow + m_visibleLines)
			m_scrollRow = m_cursorRow - m_visibleLines + 1;
	}

	std::vector<std::string> m_lines = {""};
	int m_cursorRow = 0;
	int m_cursorCol = 0;
	int m_scrollRow = 0;
	int m_visibleLines = 20;
};

} // namespace civcraft
