#pragma once
#include <string>
#include <vector>
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

	bool hasChanged = false;

	std::string selectedItem;

	void changeCursorOnHover();
	void tableNextColumn();

	// helper function to move to next row, set text, and move to next column
	void labelRow(const char* text);

	// create a simple combo box with label and items, current item is updated by reference
	// returns true if the combo box value was changed
	bool createSimpleCombo(const char* label, const char* items[], int& currentItem, int itemCount);

	// create a combo using std::vector as input
	bool createCombo(const char* label, const std::vector<std::string>& vec, int& currentItem);

	// create an input double field
	bool inputDouble(const char* label, double* value, const char* format = "%.3g");

	// create an input int field
	bool inputInt(const char* label, int* value);

	// create checkbox in table
	void checkBox(const char* label, bool* value);

	// draw leaf for tree node
	bool drawLeaf(const char* label);

	bool drawTree(const char* label, bool& isOpen, ImGuiTreeNodeFlags flags = UITreeFlags::BranchOpenedFlags);

	// grouping tree node that only holds child leaves: shows the nav hover cursor
	// but does NOT change selectedItem. returns whether the node is expanded.
	bool treeHeader(const char* label, ImGuiTreeNodeFlags flags = UITreeFlags::BranchOpenedFlags);

	// ---- shared panel design helpers (used by every setup tab) ----

	// standard section header for a properties panel (a labelled separator)
	void sectionHeader(const char* label);

	// begin a standard 2-column label/value property table. returns true when the
	// table opened (call ImGui::EndTable() in that case). rows use labelRow(...)
	// then a value/widget, or drawTableProperty(...) for read-only text.
	bool beginPropertyTable(const char* id, float labelWidth = 150.0f);

	// full-width primary action button shown under a setup tree (Generate, Start...)
	bool actionButton(const char* label);

	// draw property values. uses two columns
	void drawTableProperty(const char* label, const char* value);


	// setup table with TableColumn
	template <typename... Columns>
	void setupTableColumns(Columns... columns) {
		(ImGui::TableSetupColumn(columns.label, columns.flags, columns.width), ...);
	}


	// draw combobox units
	template <typename UnitT, size_t N>
	bool comboUnit(
		const char* label,
		std::uint8_t& unitIndex,
		const std::array<UnitT, N>& units
	) {
		if constexpr (N == 0) {
			return false;
		}

		const char* preview = units[unitIndex].name;

		bool changed = false;

		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::AlignTextToFramePadding();

		ImGui::PushID(label);
		if (ImGui::BeginCombo("##unit", preview)) {
			for (int i = 0; i < (int)(N); i++) {
				bool isSelected = unitIndex == i;

				if (ImGui::Selectable(units[i].name, isSelected)) {
					if (unitIndex != i) {
						unitIndex = (uint8_t)(i);
						changed = true;
					}
				}

				if (isSelected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
		ImGui::PopID();

		ImGui::TableNextColumn();

		return changed;
	}

	// draw the unit name as static text (read-only). units are edited in the
	// Units modal, not inline; this occupies the same table cell comboUnit did.
	template <typename UnitT, size_t N>
	void unitLabel(const std::array<UnitT, N>& units, std::uint8_t unitIndex) {
		if constexpr (N > 0) {
			if (unitIndex >= N) {
				unitIndex = 0;
			}
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(units[unitIndex].name);
		}

		ImGui::TableNextColumn();
	}

	// draw input double
	template<typename UnitT, size_t N>
	bool inputDouble(
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

		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::AlignTextToFramePadding();

		bool changed = false;

		ImGui::PushID(label);
		if (ImGui::InputDouble("##value", &displayValue, 0.0, 0.0, format)) {
			value = toBaseValue(displayValue, unit);
			changed = true;
		}
		ImGui::PopID();

		ImGui::TableNextColumn();

		return true;
	}

private:

};
