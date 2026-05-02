#include "base_gui.h"
#include "imgui.h"

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

void BaseGUI::textAtNewRow(const char* text, const int column, const int nextColumn) {
	tableNextRow();
	setTableColumn(column);
	ImGui::TextUnformatted(text);
	setTableColumn(nextColumn);
}

void BaseGUI::setNextWindowSize(int height, int width) {
	ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Appearing);
}

void BaseGUI::createSimpleCombo(const char* label, const char* items[], int& currentItem, int itemCount) {
	ImGui::Combo(label, &currentItem, items, itemCount);
}