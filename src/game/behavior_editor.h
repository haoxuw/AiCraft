#pragma once

/**
 * Visual behavior editor — build behavior logic with expression trees.
 *
 * Architecture (OOP):
 *   BehaviorFunc        — single built-in function definition
 *   BehaviorFuncRegistry — static catalog of all conditions & actions
 *   BehaviorExpr        — recursive expression tree node (pure data)
 *   BehaviorCompiler    — compiles expression tree → Python source
 *   BehaviorExprEditor  — ImGui renderer for editing expression trees
 *   CreatureConfig      — per-creature behavior + item overrides
 *   BehaviorEditorState — UI state for the menu editor panel
 */

#include "shared/constants.h"
#include <imgui.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>

namespace agentworld {

// ================================================================
// BehaviorFunc — definition of one built-in function
// ================================================================

struct BehaviorFunc {
	std::string id;         // internal id (e.g. "is_night", "wander")
	std::string label;      // display name
	bool hasParam;          // takes a string parameter?
	std::string paramHint;  // tooltip for param (e.g. "entity type")
};

// ================================================================
// BehaviorFuncRegistry — catalog of all conditions & actions
// ================================================================

class BehaviorFuncRegistry {
public:
	static const std::vector<BehaviorFunc>& conditions() {
		static const std::vector<BehaviorFunc> c = {
			{"is_day",        "Is Day",          false, ""},
			{"is_night",      "Is Night",        false, ""},
			{"threatened",    "Threatened",      false, ""},
			{"hp_low",        "HP Low (<30%)",   false, ""},
			{"see_entity",    "See Entity",      true,  "entity type (e.g. player)"},
			{"near_block",    "Near Block",      true,  "block type (e.g. wood)"},
			{"random_chance", "Random %",        true,  "percent (0-100)"},
		};
		return c;
	}

	static const std::vector<BehaviorFunc>& actions() {
		static const std::vector<BehaviorFunc> a = {
			{"idle",           "Stand Still",   false, ""},
			{"wander",         "Wander",        false, ""},
			{"follow_nearest", "Follow",        true,  "entity type (e.g. player)"},
			{"flee_nearest",   "Flee From",     true,  "entity type (e.g. player)"},
			{"find_block",     "Go to Block",   true,  "block type (e.g. wood)"},
			{"break_block",    "Break Block",   true,  "block type (e.g. wood)"},
			{"attack_nearest", "Attack",        true,  "entity type (e.g. chicken)"},
		{"drop_item",      "Drop Item",     true,  "item type (e.g. egg)"},
		};
		return a;
	}

	static const BehaviorFunc& condition(int idx) {
		auto& c = conditions();
		return c[std::clamp(idx, 0, (int)c.size() - 1)];
	}

	static const BehaviorFunc& action(int idx) {
		auto& a = actions();
		return a[std::clamp(idx, 0, (int)a.size() - 1)];
	}
};

// ================================================================
// BehaviorExpr — recursive expression tree node (pure data)
// ================================================================

struct BehaviorExpr {
	enum NodeType { Action, Condition, IfThenElse, Sequence };

	NodeType nodeType = Action;
	int funcIndex = 0;       // index into actions() or conditions()
	std::string param;       // parameter for functions that need one
	std::vector<BehaviorExpr> children;

	// Ensure IfThenElse has exactly 3 children: [condition, then, else]
	void ensureIfChildren() {
		while (children.size() < 3) children.push_back({});
	}
};

// ================================================================
// CreatureConfig — per-creature behavior + item overrides
// ================================================================

struct CreatureConfig {
	std::string typeId;
	std::string behaviorId;                      // "" = default, else = custom name
	BehaviorExpr customBehavior;
	std::vector<std::pair<std::string, int>> startItems;
};

// ================================================================
// BehaviorCompiler — compiles expression tree → Python source
// ================================================================

class BehaviorCompiler {
public:
	static std::string compile(const BehaviorExpr& root) {
		std::string code;
		code += "from agentworld_engine import Idle, Wander, MoveTo, Follow, Flee, BreakBlock, DropItem\n";
		code += "import random as _rng\n\n";
		code += "def decide(self, world):\n";
		code += compileNode(root, 1);
		return code;
	}

private:
	static std::string pad(int indent) { return std::string(indent * 4, ' '); }

	static std::string compileNode(const BehaviorExpr& expr, int indent) {
		switch (expr.nodeType) {
		case BehaviorExpr::Action:     return compileAction(expr, indent);
		case BehaviorExpr::Condition:  return pad(indent) + "pass  # bare condition\n";
		case BehaviorExpr::IfThenElse: return compileIf(expr, indent);
		case BehaviorExpr::Sequence:   return compileSequence(expr, indent);
		}
		return pad(indent) + "return Idle()\n";
	}

	static std::string compileAction(const BehaviorExpr& expr, int indent) {
		auto& fn = BehaviorFuncRegistry::action(expr.funcIndex);
		std::string p = pad(indent);
		std::string type = expr.param.empty() ? "player" : expr.param;

		if (fn.id == "idle")    return p + "return Idle()\n";
		if (fn.id == "wander")  return p + "return Wander()\n";
		if (fn.id == "drop_item") {
			std::string item = expr.param.empty() ? "egg" : expr.param;
			return p + "return DropItem(\"base:" + item + "\", 1)\n";
		}

		// Entity-targeting actions: search nearby, act on closest
		if (fn.id == "follow_nearest" || fn.id == "flee_nearest" || fn.id == "attack_nearest") {
			std::string action;
			if (fn.id == "follow_nearest") action = "Follow(e[\"id\"])";
			else if (fn.id == "flee_nearest") action = "Flee(e[\"id\"], speed=5.0)";
			else action = "Follow(e[\"id\"], speed=4.0)";

			return p + "for e in world[\"nearby\"]:\n" +
			       p + "    if e[\"type_id\"] == \"base:" + type + "\":\n" +
			       p + "        return " + action + "\n" +
			       p + "return Wander()\n";
		}

		// Block-targeting actions
		std::string blockType = expr.param.empty() ? "wood" : expr.param;
		if (fn.id == "find_block") {
			return p + "for b in world[\"blocks\"]:\n" +
			       p + "    if b[\"type\"] == \"base:" + blockType + "\":\n" +
			       p + "        return MoveTo(b[\"x\"], b[\"y\"], b[\"z\"])\n" +
			       p + "return Wander()\n";
		}
		if (fn.id == "break_block") {
			return p + "for b in world[\"blocks\"]:\n" +
			       p + "    if b[\"type\"] == \"base:" + blockType + "\":\n" +
			       p + "        return BreakBlock(b[\"x\"], b[\"y\"], b[\"z\"])\n" +
			       p + "return Idle()\n";
		}
		return p + "return Idle()\n";
	}

	static std::string compileCondition(const BehaviorExpr& expr) {
		auto& fn = BehaviorFuncRegistry::condition(expr.funcIndex);

		if (fn.id == "is_day")
			return "0.25 <= world.get(\"time\", 0.5) <= 0.75";
		if (fn.id == "is_night")
			return "(world.get(\"time\", 0.5) > 0.75 or world.get(\"time\", 0.5) < 0.25)";
		if (fn.id == "threatened")
			return "any(e[\"category\"] == \"player\" and e[\"distance\"] < 5.0 for e in world[\"nearby\"])";
		if (fn.id == "hp_low")
			return "self.get(\"hp\", 10) < self.get(\"max_hp\", 10) * 0.3";
		if (fn.id == "see_entity") {
			std::string type = expr.param.empty() ? "player" : expr.param;
			return "any(e[\"type_id\"] == \"base:" + type + "\" for e in world[\"nearby\"])";
		}
		if (fn.id == "near_block") {
			std::string type = expr.param.empty() ? "wood" : expr.param;
			return "any(b[\"type\"] == \"base:" + type + "\" for b in world[\"blocks\"])";
		}
		if (fn.id == "random_chance") {
			std::string pct = expr.param.empty() ? "50" : expr.param;
			return "_rng.random() * 100 < " + pct;
		}
		return "True";
	}

	static std::string compileIf(const BehaviorExpr& expr, int indent) {
		if (expr.children.size() < 2) return pad(indent) + "return Idle()\n";

		auto& condExpr = expr.children[0];
		std::string condCode = (condExpr.nodeType == BehaviorExpr::Condition)
			? compileCondition(condExpr)
			: "True  # complex condition";

		std::string result;
		result += pad(indent) + "if " + condCode + ":\n";
		result += compileNode(expr.children[1], indent + 1);
		if (expr.children.size() >= 3) {
			result += pad(indent) + "else:\n";
			result += compileNode(expr.children[2], indent + 1);
		}
		return result;
	}

	static std::string compileSequence(const BehaviorExpr& expr, int indent) {
		std::string result;
		for (auto& child : expr.children)
			result += compileNode(child, indent);
		if (expr.children.empty())
			result += pad(indent) + "return Idle()\n";
		return result;
	}
};

// ================================================================
// BehaviorExprEditor — ImGui renderer for editing expression trees
// ================================================================

class BehaviorExprEditor {
public:
	// Render the expression tree editor. Returns true if anything changed.
	static bool render(BehaviorExpr& expr, int depth, int& idCounter) {
		if (depth > 8) {
			ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "(max depth)");
			return false;
		}

		bool changed = false;
		int myId = idCounter++;
		ImGui::PushID(myId);

		// Node type selector
		static const char* typeLabels[] = {"Action", "Condition", "IF / THEN / ELSE", "Sequence"};
		int nodeType = (int)expr.nodeType;
		ImGui::SetNextItemWidth(160);
		if (ImGui::Combo("##ntype", &nodeType, typeLabels, 4)) {
			expr.nodeType = (BehaviorExpr::NodeType)nodeType;
			if (expr.nodeType == BehaviorExpr::IfThenElse) expr.ensureIfChildren();
			changed = true;
		}

		switch (expr.nodeType) {
		case BehaviorExpr::Action:    changed |= renderFuncPicker(expr, false); break;
		case BehaviorExpr::Condition: changed |= renderFuncPicker(expr, true);  break;
		case BehaviorExpr::IfThenElse: changed |= renderIfThenElse(expr, depth, idCounter); break;
		case BehaviorExpr::Sequence:   changed |= renderSequence(expr, depth, idCounter); break;
		}

		ImGui::PopID();
		return changed;
	}

private:
	static bool renderFuncPicker(BehaviorExpr& expr, bool isCondition) {
		bool changed = false;
		auto& funcs = isCondition
			? BehaviorFuncRegistry::conditions()
			: BehaviorFuncRegistry::actions();
		int idx = std::clamp(expr.funcIndex, 0, (int)funcs.size() - 1);

		ImGui::SameLine();
		ImGui::SetNextItemWidth(140);
		if (ImGui::BeginCombo("##func", funcs[idx].label.c_str())) {
			for (int i = 0; i < (int)funcs.size(); i++) {
				if (ImGui::Selectable(funcs[i].label.c_str(), i == idx)) {
					expr.funcIndex = i;
					changed = true;
				}
			}
			ImGui::EndCombo();
		}

		if (funcs[idx].hasParam) {
			ImGui::SameLine();
			ImGui::SetNextItemWidth(120);
			char buf[64];
			snprintf(buf, sizeof(buf), "%s", expr.param.c_str());
			if (ImGui::InputText("##p", buf, sizeof(buf))) {
				expr.param = buf;
				changed = true;
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", funcs[idx].paramHint.c_str());
		}
		return changed;
	}

	static bool renderIfThenElse(BehaviorExpr& expr, int depth, int& idCounter) {
		bool changed = false;
		expr.ensureIfChildren();

		ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1), "IF");
		ImGui::Indent(20);
		changed |= render(expr.children[0], depth + 1, idCounter);
		ImGui::Unindent(20);

		ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1), "THEN");
		ImGui::Indent(20);
		changed |= render(expr.children[1], depth + 1, idCounter);
		ImGui::Unindent(20);

		ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.4f, 1), "ELSE");
		ImGui::Indent(20);
		changed |= render(expr.children[2], depth + 1, idCounter);
		ImGui::Unindent(20);

		return changed;
	}

	static bool renderSequence(BehaviorExpr& expr, int depth, int& idCounter) {
		bool changed = false;
		for (int i = 0; i < (int)expr.children.size(); i++) {
			ImGui::PushID(i + 1000);
			char label[16]; snprintf(label, sizeof(label), "Step %d", i + 1);
			ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.9f, 1), "%s", label);
			ImGui::SameLine();
			if (ImGui::SmallButton("x")) {
				expr.children.erase(expr.children.begin() + i);
				changed = true;
				ImGui::PopID();
				break;
			}
			ImGui::Indent(20);
			changed |= render(expr.children[i], depth + 1, idCounter);
			ImGui::Unindent(20);
			ImGui::PopID();
		}
		if (ImGui::SmallButton("+ Add Step")) {
			expr.children.push_back({});
			changed = true;
		}
		return changed;
	}
};

// ================================================================
// BehaviorEditorState — UI state for the menu editor panel
// ================================================================

struct BehaviorEditorState {
	// Multi-select (uint8_t avoids vector<bool> proxy issues with ImGui)
	std::vector<uint8_t> creatureSelected;
	std::vector<uint8_t> characterSelected;

	// Shared behavior expression for selected entities
	BehaviorExpr sharedBehavior;

	// Shared starting items for selected entities
	std::vector<std::pair<std::string, int>> sharedItems;

	// Per-creature overrides (keyed by typeId or "char:N")
	std::unordered_map<std::string, CreatureConfig> configs;

	// Preview state
	bool showPreview = false;
};

// Keep old names as aliases so imgui_menu.h doesn't need changes
inline std::string compileBehavior(const BehaviorExpr& root) {
	return BehaviorCompiler::compile(root);
}
inline bool renderExprEditor(BehaviorExpr& expr, int depth, int& idCounter) {
	return BehaviorExprEditor::render(expr, depth, idCounter);
}

} // namespace agentworld
