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
	textToLeft(text);
	setTableColumn(nextColumn);
}