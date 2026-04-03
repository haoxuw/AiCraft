#pragma once

/**
 * Visual behavior editor — build behavior logic with expression trees.
 *
 * Expression trees use discrete math: if/then/else with recursive
 * sub-expressions. Conditions and actions are built-in functions
 * that will eventually map to Python behavior code.
 *
 * Example:
 *   IF is_night THEN find(bed) ELSE IF threatened THEN flee(player) ELSE wander
 *
 * The tree compiles to Python code compatible with the behavior system.
 */

#include "shared/constants.h"
#include <imgui.h>
#include <string>
#include <vector>
#include <sstream>

namespace agentworld {

// ================================================================
// Built-in functions — conditions and actions
// ================================================================

struct BuiltinFunc {
	std::string id;         // internal id
	std::string label;      // display name
	bool isCondition;       // true = condition (bool), false = action
	bool hasParam;          // takes a string parameter?
	std::string paramHint;  // e.g. "entity type", "block type"
};

inline const std::vector<BuiltinFunc>& builtinConditions() {
	static std::vector<BuiltinFunc> conds = {
		{"is_day",        "Is Day",          true, false, ""},
		{"is_night",      "Is Night",        true, false, ""},
		{"threatened",    "Threatened",      true, false, ""},
		{"hp_low",        "HP Low (<30%)",   true, false, ""},
		{"see_entity",    "See Entity",      true, true,  "entity type"},
		{"near_block",    "Near Block",      true, true,  "block type"},
		{"random_chance", "Random %",        true, true,  "percent"},
	};
	return conds;
}

inline const std::vector<BuiltinFunc>& builtinActions() {
	static std::vector<BuiltinFunc> acts = {
		{"idle",           "Stand Still",   false, false, ""},
		{"wander",         "Wander",        false, false, ""},
		{"follow_nearest", "Follow",        false, true,  "entity type"},
		{"flee_nearest",   "Flee From",     false, true,  "entity type"},
		{"find_block",     "Go to Block",   false, true,  "block type"},
		{"break_block",    "Break Block",   false, true,  "block type"},
		{"attack_nearest", "Attack",        false, true,  "entity type"},
	};
	return acts;
}

// ================================================================
// Expression tree node
// ================================================================

struct BehaviorExpr {
	enum NodeType { Action, Condition, IfThenElse, Sequence };

	NodeType nodeType = Action;
	int funcIndex = 0;       // index into builtinActions() or builtinConditions()
	std::string param;       // parameter for functions that need one

	// Children:
	//   IfThenElse: [0]=condition, [1]=then, [2]=else (optional)
	//   Sequence:   [0..n]=steps (first matching condition wins)
	std::vector<BehaviorExpr> children;

	// Ensure IfThenElse has 3 children
	void ensureIfChildren() {
		while (children.size() < 3)
			children.push_back({});
	}
};

// ================================================================
// Creature/character config for world creation
// ================================================================

struct CreatureConfig {
	std::string typeId;                          // "base:pig"
	std::string behaviorId;                      // "" = use default, "custom" = use expr
	BehaviorExpr customBehavior;                 // expression tree
	std::vector<std::pair<std::string, int>> startItems; // starting items
};

// ================================================================
// Compile expression tree → Python behavior code
// ================================================================

inline std::string exprToPython(const BehaviorExpr& expr, int indent = 1) {
	std::string pad(indent * 4, ' ');

	switch (expr.nodeType) {
	case BehaviorExpr::Action: {
		auto& acts = builtinActions();
		int idx = std::clamp(expr.funcIndex, 0, (int)acts.size() - 1);
		auto& fn = acts[idx];

		if (fn.id == "idle")    return pad + "return Idle()\n";
		if (fn.id == "wander")  return pad + "return Wander()\n";

		if (fn.id == "follow_nearest") {
			std::string type = expr.param.empty() ? "player" : expr.param;
			return pad + "for e in world[\"nearby\"]:\n" +
			       pad + "    if e[\"type_id\"] == \"base:" + type + "\":\n" +
			       pad + "        return Follow(e[\"id\"])\n" +
			       pad + "return Wander()\n";
		}
		if (fn.id == "flee_nearest") {
			std::string type = expr.param.empty() ? "player" : expr.param;
			return pad + "for e in world[\"nearby\"]:\n" +
			       pad + "    if e[\"type_id\"] == \"base:" + type + "\":\n" +
			       pad + "        return Flee(e[\"id\"])\n" +
			       pad + "return Wander()\n";
		}
		if (fn.id == "find_block") {
			std::string type = expr.param.empty() ? "wood" : expr.param;
			return pad + "for b in world[\"blocks\"]:\n" +
			       pad + "    if b[\"type\"] == \"base:" + type + "\":\n" +
			       pad + "        return MoveTo(b[\"x\"], b[\"y\"], b[\"z\"])\n" +
			       pad + "return Wander()\n";
		}
		if (fn.id == "break_block") {
			std::string type = expr.param.empty() ? "wood" : expr.param;
			return pad + "for b in world[\"blocks\"]:\n" +
			       pad + "    if b[\"type\"] == \"base:" + type + "\":\n" +
			       pad + "        return BreakBlock(b[\"x\"], b[\"y\"], b[\"z\"])\n" +
			       pad + "return Idle()\n";
		}
		if (fn.id == "attack_nearest") {
			std::string type = expr.param.empty() ? "player" : expr.param;
			return pad + "for e in world[\"nearby\"]:\n" +
			       pad + "    if e[\"type_id\"] == \"base:" + type + "\":\n" +
			       pad + "        return Follow(e[\"id\"], speed=4.0)\n" +
			       pad + "return Wander()\n";
		}
		return pad + "return Idle()\n";
	}

	case BehaviorExpr::Condition: {
		// Conditions are only used inside IfThenElse, not standalone
		return pad + "return Idle()  # bare condition\n";
	}

	case BehaviorExpr::IfThenElse: {
		if (expr.children.size() < 2) return pad + "return Idle()\n";

		// Generate condition
		auto& conds = builtinConditions();
		auto& condExpr = expr.children[0];
		int cidx = std::clamp(condExpr.funcIndex, 0, (int)conds.size() - 1);
		auto& cond = conds[cidx];
		std::string condCode;

		if (cond.id == "is_day")
			condCode = "0.25 <= world.get(\"time\", 0.5) <= 0.75";
		else if (cond.id == "is_night")
			condCode = "world.get(\"time\", 0.5) > 0.75 or world.get(\"time\", 0.5) < 0.25";
		else if (cond.id == "threatened")
			condCode = "any(e[\"category\"] == \"player\" and e[\"distance\"] < 5.0 for e in world[\"nearby\"])";
		else if (cond.id == "hp_low")
			condCode = "self.get(\"hp\", 10) < self.get(\"max_hp\", 10) * 0.3";
		else if (cond.id == "see_entity") {
			std::string type = condExpr.param.empty() ? "player" : condExpr.param;
			condCode = "any(e[\"type_id\"] == \"base:" + type + "\" for e in world[\"nearby\"])";
		}
		else if (cond.id == "near_block") {
			std::string type = condExpr.param.empty() ? "wood" : condExpr.param;
			condCode = "any(b[\"type\"] == \"base:" + type + "\" for b in world[\"blocks\"])";
		}
		else if (cond.id == "random_chance") {
			std::string pct = condExpr.param.empty() ? "50" : condExpr.param;
			condCode = "__import__('random').random() * 100 < " + pct;
		}
		else
			condCode = "True";

		// Handle nested IfThenElse in condition (recursive expressions)
		if (condExpr.nodeType == BehaviorExpr::IfThenElse) {
			// Nested condition: evaluate as inline function
			condCode = "True  # nested condition";
		}

		std::string result;
		result += pad + "if " + condCode + ":\n";
		result += exprToPython(expr.children[1], indent + 1);

		if (expr.children.size() >= 3 &&
		    expr.children[2].nodeType != BehaviorExpr::Action &&
		    expr.children[2].funcIndex != 0) {
			// Has a non-trivial else branch
			result += pad + "else:\n";
			result += exprToPython(expr.children[2], indent + 1);
		} else if (expr.children.size() >= 3) {
			result += pad + "else:\n";
			result += exprToPython(expr.children[2], indent + 1);
		}

		return result;
	}

	case BehaviorExpr::Sequence: {
		std::string result;
		for (auto& child : expr.children)
			result += exprToPython(child, indent);
		if (expr.children.empty())
			result += pad + "return Idle()\n";
		return result;
	}
	}

	return pad + "return Idle()\n";
}

inline std::string compileBehavior(const BehaviorExpr& root) {
	std::string code;
	code += "from agentworld_engine import Idle, Wander, MoveTo, Follow, Flee, BreakBlock\n\n";
	code += "def decide(self, world):\n";
	code += exprToPython(root, 1);
	return code;
}

// ================================================================
// ImGui behavior expression editor — recursive rendering
// ================================================================

// Forward declare for recursion
inline bool renderExprEditor(BehaviorExpr& expr, int depth, int& idCounter);

inline bool renderExprEditor(BehaviorExpr& expr, int depth, int& idCounter) {
	bool changed = false;
	int myId = idCounter++;

	// Limit depth to prevent infinite recursion in UI
	if (depth > 8) {
		ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "(max depth)");
		return false;
	}

	ImGui::PushID(myId);

	// Node type selector
	const char* typeLabels[] = {"Action", "Condition", "IF / THEN / ELSE", "Sequence"};
	int nodeType = (int)expr.nodeType;
	ImGui::SetNextItemWidth(160);
	if (ImGui::Combo("##nodetype", &nodeType, typeLabels, 4)) {
		expr.nodeType = (BehaviorExpr::NodeType)nodeType;
		if (expr.nodeType == BehaviorExpr::IfThenElse)
			expr.ensureIfChildren();
		changed = true;
	}

	switch (expr.nodeType) {
	case BehaviorExpr::Action: {
		auto& acts = builtinActions();
		ImGui::SameLine();
		ImGui::SetNextItemWidth(140);
		if (ImGui::BeginCombo("##action", acts[expr.funcIndex].label.c_str())) {
			for (int i = 0; i < (int)acts.size(); i++) {
				if (ImGui::Selectable(acts[i].label.c_str(), i == expr.funcIndex)) {
					expr.funcIndex = i;
					changed = true;
				}
			}
			ImGui::EndCombo();
		}
		if (acts[expr.funcIndex].hasParam) {
			ImGui::SameLine();
			ImGui::SetNextItemWidth(120);
			char buf[64];
			snprintf(buf, sizeof(buf), "%s", expr.param.c_str());
			if (ImGui::InputText("##param", buf, sizeof(buf))) {
				expr.param = buf;
				changed = true;
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", acts[expr.funcIndex].paramHint.c_str());
		}
		break;
	}

	case BehaviorExpr::Condition: {
		auto& conds = builtinConditions();
		ImGui::SameLine();
		ImGui::SetNextItemWidth(140);
		if (ImGui::BeginCombo("##cond", conds[expr.funcIndex].label.c_str())) {
			for (int i = 0; i < (int)conds.size(); i++) {
				if (ImGui::Selectable(conds[i].label.c_str(), i == expr.funcIndex)) {
					expr.funcIndex = i;
					changed = true;
				}
			}
			ImGui::EndCombo();
		}
		if (conds[expr.funcIndex].hasParam) {
			ImGui::SameLine();
			ImGui::SetNextItemWidth(120);
			char buf[64];
			snprintf(buf, sizeof(buf), "%s", expr.param.c_str());
			if (ImGui::InputText("##cparam", buf, sizeof(buf))) {
				expr.param = buf;
				changed = true;
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", conds[expr.funcIndex].paramHint.c_str());
		}
		break;
	}

	case BehaviorExpr::IfThenElse: {
		expr.ensureIfChildren();

		// Condition
		ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1), "IF");
		ImGui::Indent(20);
		changed |= renderExprEditor(expr.children[0], depth + 1, idCounter);
		ImGui::Unindent(20);

		// Then branch
		ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1), "THEN");
		ImGui::Indent(20);
		changed |= renderExprEditor(expr.children[1], depth + 1, idCounter);
		ImGui::Unindent(20);

		// Else branch
		ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.4f, 1), "ELSE");
		ImGui::Indent(20);
		changed |= renderExprEditor(expr.children[2], depth + 1, idCounter);
		ImGui::Unindent(20);
		break;
	}

	case BehaviorExpr::Sequence: {
		for (int i = 0; i < (int)expr.children.size(); i++) {
			ImGui::PushID(i + 1000);
			char stepLabel[16];
			snprintf(stepLabel, sizeof(stepLabel), "Step %d", i + 1);
			ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.9f, 1), "%s", stepLabel);

			ImGui::SameLine();
			if (ImGui::SmallButton("x")) {
				expr.children.erase(expr.children.begin() + i);
				changed = true;
				ImGui::PopID();
				break;
			}

			ImGui::Indent(20);
			changed |= renderExprEditor(expr.children[i], depth + 1, idCounter);
			ImGui::Unindent(20);
			ImGui::PopID();
		}

		if (ImGui::SmallButton("+ Add Step")) {
			expr.children.push_back({});
			changed = true;
		}
		break;
	}
	}

	ImGui::PopID();
	return changed;
}

// ================================================================
// Creature/character config editor
// ================================================================

struct BehaviorEditorState {
	// Multi-select creature types (uint8_t avoids vector<bool> proxy issues)
	std::vector<uint8_t> creatureSelected;     // parallel to WorldGenConfig::mobs
	std::vector<uint8_t> characterSelected;    // parallel to CharacterManager list

	// Shared behavior expression for selected entities
	BehaviorExpr sharedBehavior;

	// Shared starting items for selected entities
	std::vector<std::pair<std::string, int>> sharedItems;

	// Per-creature overrides (keyed by typeId or character index)
	std::unordered_map<std::string, CreatureConfig> configs;

	// Preview of generated Python code
	std::string previewCode;
	bool showPreview = false;

	// New item being added
	char newItemType[64] = "";
	int newItemCount = 1;
};

} // namespace agentworld
