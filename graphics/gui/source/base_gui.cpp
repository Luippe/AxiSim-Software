#include "base_gui.h"
#include "flag_manager.h"
#include "printer.h"

void BaseGUI::changeCursorOnHover() {
	if (ImGui::IsItemHovered()) {
		ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
	}
}

void BaseGUI::textToLeft(const char* text) {
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted(text);
}

void BaseGUI::setTableColumn(const int column) {
	ImGui::TableSetColumnIndex(column);
}

void BaseGUI::tableNextRow() {
	ImGui::TableNextRow();
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

void BaseGUI::setNextWindowSize(int height, int width) {
	ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Appearing);
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

void BaseGUI::inputDouble(const char* label, double* value, const char* format) {
	ImGui::SetNextItemWidth(-FLT_MIN);
	ImGui::AlignTextToFramePadding();
	ImGui::InputDouble(label, value, 0.0, 0.0, format);
}

void BaseGUI::inputInt(const char* label, int* value) {
	ImGui::SetNextItemWidth(-FLT_MIN);
	ImGui::AlignTextToFramePadding();
	ImGui::InputInt(label, value, 0.0, 0.0);
}

void BaseGUI::drawTableHeader(const char* label) {

	ImGui::PushID(label);

	if (ImGui::BeginTable("Table", 1, UIFlags::TableBoundaryFlags)) {

		setupTableColumns(
			column("Label", 100.0f)
		);
		ImGui::TableNextRow();

		ImGui::TableSetBgColor(
			ImGuiTableBgTarget_RowBg0,
			ImGui::GetColorU32(ImGuiCol_Header)
		);

		ImGui::TableSetColumnIndex(0);
		ImGui::TextUnformatted(label);

		ImGui::EndTable();
	}
	ImGui::PopID();

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