#include "base_gui.h"
#include "flag_manager.h"

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

void BaseGUI::drawLeaf(const char* label) {

	bool selected = selectedItem == label;

	ImGui::TreeNodeEx(label, UIFlags::LeafFlags | (selected ? ImGuiTreeNodeFlags_Selected : 0));

	if (ImGui::IsItemClicked()) {
		selectedItem = label;
	}

	changeCursorOnHover();
}

