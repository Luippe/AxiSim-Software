#include "base_gui.h"
#include "buffer_manager.h"
#include "flag_manager.h"
#include "printer.h"

void BaseGUI::drawIconTreeLabel(const char* label, TextureBuffer* icon, ImGuiTreeNodeFlags flags) {
	if (!ImGui::IsItemVisible()) {
		return;
	}

	const ImGuiStyle& style = ImGui::GetStyle();
	const ImVec2 itemMin = ImGui::GetItemRectMin();
	const ImVec2 itemMax = ImGui::GetItemRectMax();
	const float fontSize = ImGui::GetFontSize();
	const bool displayFrame = (flags & ImGuiTreeNodeFlags_Framed) != 0;
	const bool isLeaf = (flags & ImGuiTreeNodeFlags_Leaf) != 0;
	const bool hasBullet = (flags & ImGuiTreeNodeFlags_Bullet) != 0;

	float labelX = itemMin.x;
	if (displayFrame || !isLeaf || hasBullet) {
		labelX += fontSize + (displayFrame ? style.FramePadding.x * 3.0f : style.FramePadding.x * 2.0f);
	}

	float textX = labelX;
	const float rowHeight = itemMax.y - itemMin.y;

	if (icon) {
		const unsigned int textureID = icon->getTextureID();
		if (textureID != 0) {
			const float iconSize = fontSize * treeIconScale;
			const float iconY = itemMin.y + (rowHeight - iconSize) * 0.5f;
			const ImVec2 iconMin(labelX, iconY);
			const ImVec2 iconMax(labelX + iconSize, iconY + iconSize);

			ImGui::GetWindowDrawList()->AddImage(
				(ImTextureID)(intptr_t)textureID,
				iconMin,
				iconMax
			);

			textX = iconMax.x + style.ItemInnerSpacing.x;
		}
	}

	const char* labelEnd = ImGui::FindRenderedTextEnd(label);
	if (label != labelEnd) {
		const float textY = itemMin.y + (rowHeight - fontSize) * 0.5f;
		ImGui::GetWindowDrawList()->AddText(
			ImVec2(textX, textY),
			ImGui::GetColorU32(ImGuiCol_Text),
			label,
			labelEnd
		);
	}
}

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

void BaseGUI::disabledHint(bool disabled, const char* reason) {
	if (!disabled || !reason) return;

	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
		ImGui::SetTooltip("%s", reason);
	}
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

bool BaseGUI::beginPropertyTable(const char* id) {
	if (!ImGui::BeginTable(id, 2, UIFlags::TableSimpleFlags)) {
		return false;
	}

	// Width 0 with WidthFixed means "auto-fit to the widest cell actually
	// submitted in this column", so a label can never be clipped no matter the
	// font size or DPI. This replaced hand-tuned pixel widths, which were what
	// truncated labels like "Number Format" and "Color Range" in the first place.
	ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 0.0f);
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

bool BaseGUI::drawLeaf(const char* label, TextureBuffer* icon) {

	bool selected = selectedItem == label;
	ImGuiTreeNodeFlags flags = UITreeFlags::LeafFlags | (selected ? ImGuiTreeNodeFlags_Selected : 0);

	if (icon) {
		ImGui::TreeNodeEx(label, flags, " ");
		drawIconTreeLabel(label, icon, flags);
	}
	else {
		ImGui::TreeNodeEx(label, flags);
	}

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

bool BaseGUI::treeHeader(const char* label, TextureBuffer* icon, ImGuiTreeNodeFlags flags) {
	if (icon) {
		bool isOpen = ImGui::TreeNodeEx(label, flags, " ");
		drawIconTreeLabel(label, icon, flags);
		changeCursorOnHover();
		return isOpen;
	}

	return treeHeader(label, flags);
}

bool BaseGUI::drawTree(const char* label, bool& isOpen, ImGuiTreeNodeFlags flags) {
	return drawTree(label, isOpen, nullptr, flags);
}

bool BaseGUI::drawTree(const char* label, bool& isOpen, TextureBuffer* icon, ImGuiTreeNodeFlags flags) {

	bool selected = selectedItem == label;
	ImGuiTreeNodeFlags nodeFlags = flags | (selected ? ImGuiTreeNodeFlags_Selected : 0);

	if (icon) {
		isOpen = ImGui::TreeNodeEx(label, nodeFlags, " ");
		drawIconTreeLabel(label, icon, nodeFlags);
	}
	else {
		isOpen = ImGui::TreeNodeEx(label, nodeFlags);
	}

	bool clicked = ImGui::IsItemClicked();

	if (clicked) {
		selectedItem = label;
	}

	changeCursorOnHover();

	return clicked;
}
