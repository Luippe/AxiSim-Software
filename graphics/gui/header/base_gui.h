#pragma once
#include <string>
#include "imgui.h"
#include "unit_manager.h"

class BaseGUI {
public:

	void changeCursorOnHover();
	void textToLeft(const char* text);
	void setTableColumn(const int column);
	void tableNextRow();
	void tableNextColumn();

	void setNextWindowSize(int height, int width);

	// helper function to move to next row, set text, and move to next column
	void textAtNewRow(const char* text, const int column, const int nextColumn);

	// create a simple combo box with label and items, current item is updated by reference. returns true if the combo box value was changed
	bool createSimpleCombo(const char* label, const char* items[], int& currentItem, int itemCount);

	// create an input double field
	void createInputDouble(const char* label, double* value);

	// draw leaf for tree node
	void drawLeaf(const char* label);

	// draw label, input, units in a 3 column table
	template <typename T, size_t N>
	void inputDoubleWithUnits(const char* label, T& value, std::uint8_t& unitIndex, const std::array<UnitOption, N>& units) {
		
		const UnitOption& unit = units[unitIndex];

		double displayValue = (double)value / unit.toBase;

		ImGui::PushID(label);	// dont have to build unique settings with this
		ImGui::TableNextRow();

		ImGui::TableSetColumnIndex(0);
		ImGui::TextUnformatted(label);
		ImGui::TableNextColumn();


		// convert display value back to solver/base value
		if (ImGui::InputDouble("##value", &displayValue)) {
			value = (T)(displayValue * unit.toBase);
		}

		ImGui::TableNextColumn();
		if (ImGui::BeginCombo("##unit", unit.name)) {
			for (int i = 0; i < (int)N; i++) {
				bool selected = (unitIndex == i);

				if (ImGui::Selectable(units[i].name, selected)) {
					unitIndex = i;
				}

				if (selected) {
					ImGui::SetItemDefaultFocus();
				}
			}

			ImGui::EndCombo();
		}
		ImGui::PopID();
	}

	std::string selectedItem;

private:

	ImGuiTreeNodeFlags flags =
		ImGuiTreeNodeFlags_Leaf |
		ImGuiTreeNodeFlags_NoTreePushOnOpen |
		ImGuiTreeNodeFlags_SpanAvailWidth;


};