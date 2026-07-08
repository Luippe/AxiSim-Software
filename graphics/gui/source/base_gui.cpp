#include "base_gui.h"
#include "flag_manager.h"
#include "printer.h"

void BaseGUI::changeCursorOnHover() {
	if (ImGui::IsItemHovered()) {
		ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
	}
}

void BaseGUI::tableNextColumn() {
	ImGui::TableNextColumn();
}

void BaseGUI::labelRow(const char* text) {

	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted(text);
	ImGui::TableNextColumn();

}

bool BaseGUI::createSimpleCombo(const char* label, const char* items[], int& currentItem, int itemCount) {
	ImGui::SetNextItemWidth(-FLT_MIN);
	ImGui::AlignTextToFramePadding();
	return ImGui::Combo(label, &currentItem, items, itemCount);
}

bool BaseGUI::createCombo(const char* label, const std::vector<std::string>& vec, int& currentItem) {

	if (vec.empty()) return false;
	
	const char* preview = vec[currentItem].c_str();

	bool changed = false;

	if (ImGui::BeginCombo(label, preview)) {

		for (int i = 0; i < static_cast<int>(vec.size()); i++) {
			bool isSelected = currentItem == i;

			if (ImGui::Selectable(vec[i].c_str(), isSelected)) {
				if (currentItem != i) {
					currentItem = i;
					changed = true;
				}
			}

			if (isSelected) {
				ImGui::SetItemDefaultFocus();
			}
		}

		ImGui::EndCombo();
	}
	return changed;
}

void BaseGUI::checkBox(const char* label, bool* value) {
	ImGui::AlignTextToFramePadding();
	ImGui::Checkbox(label, value);
}

bool BaseGUI::inputDouble(const char* label, double* value, const char* format) {
	ImGui::SetNextItemWidth(-FLT_MIN);
	ImGui::AlignTextToFramePadding();
	return ImGui::InputDouble(label, value, 0.0, 0.0, format);
}

bool BaseGUI::inputInt(const char* label, int* value) {
	ImGui::SetNextItemWidth(-FLT_MIN);
	ImGui::AlignTextToFramePadding();
	return ImGui::InputInt(label, value, 0.0, 0.0);
}

void BaseGUI::sectionHeader(const char* label) {
	ImGui::SeparatorText(label);
}

bool BaseGUI::beginPropertyTable(const char* id, float labelWidth) {
	if (!ImGui::BeginTable(id, 2, UIFlags::TableSimpleFlags)) {
		return false;
	}

	ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, labelWidth);
	ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
	return true;
}

bool BaseGUI::actionButton(const char* label) {
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
	bool clicked = ImGui::Button(label, ImVec2(-FLT_MIN, 0.0f));
	ImGui::PopStyleVar();
	return clicked;
}

void BaseGUI::drawTableProperty(const char* label, const char* value) {
	ImGui::TableNextRow();
	ImGui::TableSetBgColor(
		ImGuiTableBgTarget_RowBg0,
		ImGui::GetColorU32(ImGuiCol_Header)
	);
	ImGui::TableSetColumnIndex(0);
	ImGui::TextUnformatted(label);

	ImGui::TableSetColumnIndex(1);
	ImGui::TextUnformatted(value);
}

bool BaseGUI::drawLeaf(const char* label) {

	bool selected = selectedItem == label;

	ImGui::TreeNodeEx(label, UIFlagsTree::LeafFlags | (selected ? ImGuiTreeNodeFlags_Selected : 0));

	bool clicked = ImGui::IsItemClicked();

	if (clicked) {
		selectedItem = label;
	}

	changeCursorOnHover();

	return clicked;
}

bool BaseGUI::treeHeader(const char* label, ImGuiTreeNodeFlags flags) {
	bool isOpen = ImGui::TreeNodeEx(label, flags);
	changeCursorOnHover();
	return isOpen;
}

bool BaseGUI::drawTree(const char* label, bool& isOpen, ImGuiTreeNodeFlags flags) {

	bool selected = selectedItem == label;

	isOpen = ImGui::TreeNodeEx(label, flags | (selected ? ImGuiTreeNodeFlags_Selected : 0));

	bool clicked = ImGui::IsItemClicked();

	if (clicked) {
		selectedItem = label;
	}

	changeCursorOnHover();

	return clicked;

}
