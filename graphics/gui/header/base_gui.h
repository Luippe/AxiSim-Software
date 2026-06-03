#pragma once
#include <string>
#include "imgui.h"
#include "unit_manager.h"
#include "flag_manager.h"

class BaseGUI {
public:

	struct TableColumn {
		const char* label;
		float width;
		ImGuiTableColumnFlags flags;
	};

	inline TableColumn column(
		const char* label,
		float width,
		ImGuiTableColumnFlags flags = ImGuiTableColumnFlags_WidthFixed
	) {
		return { label, width, flags };
	}

	std::string selectedItem;

	void changeCursorOnHover();
	void textToLeft(const char* text);
	void setTableColumn(const int column);
	void tableNextRow();
	void tableNextColumn();

	void setNextWindowSize(int height, int width);

	// helper function to move to next row, set text, and move to next column
	void labelRow(const char* text);

	// create a simple combo box with label and items, current item is updated by reference. returns true if the combo box value was changed
	bool createSimpleCombo(const char* label, const char* items[], int& currentItem, int itemCount);

	// create an input double field
	void inputDouble(const char* label, double* value, const char* format = "%.3g");

	// create an input int field
	void inputInt(const char* label, int* value);

	// create checkbox in table
	void checkBox(const char* label, bool* value);

	// draw leaf for tree node
	bool drawLeaf(const char* label);

	bool drawTree(const char* label, bool& isOpen, ImGuiTreeNodeFlags flags = UIFlagsTree::BranchOpenedFlags);


	// table functions
	
	// draw table header. uses single column
	void drawTableHeader(const char* label);

	// draw property values. uses two columns
	void drawTableProperty(const char* label, const char* value);


	// setup table with TableColumn
	template <typename... Columns>
	void setupTableColumns(Columns... columns) {
		(ImGui::TableSetupColumn(columns.label, columns.flags, columns.width), ...);
	}


	// draw input and unit using two columns
	template <typename UnitT, size_t N>
	void inputDoubleWithUnits(
		const char* label,
		double& value,
		std::uint8_t& unitIndex,
		const std::array<UnitT, N>& units,
		const char* format = "%.3g"
	) {
		if (unitIndex >= N) {
			unitIndex = 0;
		}

		const UnitT& unit = units[unitIndex];

		double baseValue = (double)(value);
		double displayValue = fromBaseValue(baseValue, unit);

		ImGui::PushID(label);

		ImGui::SetNextItemWidth(-FLT_MIN);

		if (ImGui::InputDouble("##value", &displayValue, 0.0, 0.0, format)) {
			value = toBaseValue(displayValue, unit);
		}

		ImGui::TableNextColumn();

		ImGui::SetNextItemWidth(-FLT_MIN);

		if (ImGui::BeginCombo("##unit", unit.name)) {
			for (int i = 0; i < (int)(N); i++) {
				bool selected = unitIndex == i;

				if (ImGui::Selectable(units[i].name, selected)) {
					unitIndex = (std::uint8_t)(i);
				}

				if (selected) {
					ImGui::SetItemDefaultFocus();
				}
			}

			ImGui::EndCombo();
		}

		ImGui::PopID();
	}



private:

};