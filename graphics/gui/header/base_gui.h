#pragma once
#include <string>
#include <vector>
#include "imgui.h"
#include "unit_manager.h"
#include "flag_manager.h"

class TextureBuffer;

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

	// Column sized to the widest cell actually submitted in it (width 0 +
	// WidthFixed is ImGui's auto-fit). Use this for any column holding TEXT --
	// labels, headers, read-only values -- so nothing is ever clipped.
	//
	// Do NOT use it for a column whose widget stretches to the cell (inputDouble /
	// createSimpleCombo call SetNextItemWidth(-FLT_MIN)): the width would then be
	// defined in terms of itself. Those columns want an explicit or stretch width.
	inline TableColumn autoColumn(const char* label) {
		return { label, 0.0f, ImGuiTableColumnFlags_WidthFixed };
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

	// tooltip explaining why the widget just submitted is greyed out. call
	// immediately after that widget; does nothing when `disabled` is false.
	// hovering a disabled widget needs AllowWhenDisabled, which is why this is
	// not just ImGui::SetItemTooltip.
	void disabledHint(bool disabled, const char* reason);

	// draw leaf for tree node.
	bool drawLeaf(const char* label, TextureBuffer* icon = nullptr);

	bool drawTree(const char* label, bool& isOpen, ImGuiTreeNodeFlags flags = UITreeFlags::BranchOpenedFlags);
	bool drawTree(const char* label, bool& isOpen, TextureBuffer* icon, ImGuiTreeNodeFlags flags = UITreeFlags::BranchOpenedFlags);

	// grouping tree node that only holds child leaves: shows the nav hover cursor
	// but does NOT change selectedItem. returns whether the node is expanded.
	bool treeHeader(const char* label, ImGuiTreeNodeFlags flags = UITreeFlags::BranchOpenedFlags);
	bool treeHeader(const char* label, TextureBuffer* icon, ImGuiTreeNodeFlags flags = UITreeFlags::BranchOpenedFlags);

	// ---- shared panel design helpers (used by every setup tab) ----

	// standard section header for a properties panel (a labelled separator)
	void sectionHeader(const char* label);

	// begin a standard 2-column label/value property table. returns true when the
	// table opened (call ImGui::EndTable() in that case). rows use labelRow(...)
	// then a value/widget, or drawTableProperty(...) for read-only text.
	// The label column auto-fits its widest label; the value column stretches.
	bool beginPropertyTable(const char* id);

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

		return changed;
	}

private:
	void drawIconTreeLabel(const char* label, TextureBuffer* icon, ImGuiTreeNodeFlags flags);

	// icon size relative to the text line height in the setup trees. >1 makes the
	// icon larger than the label so the (busy, multi-color) icons stay legible.
	static constexpr float treeIconScale = 1.25f;

};
