#include "results_gui.h"

#include "project.h"
#include "gui.h"
#include "scene_view.h"
#include "results.h"

#include "colormap.h"
#include "colorbar.h"

#include "graphics_struct.h"

#include "flag_manager.h"
#include "unit_manager.h"

ResultsGUI::ResultsGUI(Project& project, GUI& gui) :
	project(project),
	gui(gui),
	assets(gui.appConfig.assets),
	scene(gui.scene),
	results(project.results),
	colormap(gui.scene.colormap),
	colorbar(gui.inspector.colorbar){
}

void ResultsGUI::drawPropertiesPanel() {

	ImGui::Begin("Overview");

	if (selectedItem == "General") {
		sectionHeader("Filter");
		if (ImGui::BeginTable("Geometry", 3, UIFlags::TableSimpleFlags)) {
			ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 45.0f);
			ImGui::TableSetupColumn("Slider", ImGuiTableColumnFlags_WidthStretch);

			labelRow("Filter");
			createSimpleCombo("##Filter", results.compareType, (int&)results.currentCompareType, IM_ARRAYSIZE(results.compareType));

			labelRow("Value");
			const bool hasCurrentField = results.currentField != nullptr;
			if (!hasCurrentField) {
				ImGui::BeginDisabled();
			}

			if (results.currentCompareType == CompareType::Between || results.currentCompareType == CompareType::Exclude) {
				ImGui::InputFloat("##LowerBound", &results.filterValues.valueLower, 0.0f, 0.0f);
				ImGui::TableNextColumn();
				ImGui::InputFloat("##UpperBound", &results.filterValues.valueUpper, 0.0f, 0.0f);
			}
			else {
				const float vmin = hasCurrentField ? results.currentField->vmin : 0.0f;
				const float vmax = hasCurrentField ? results.currentField->vmax : 1.0f;
				ImGui::SliderFloat("##Value", &results.filterValues.valueAt, vmin, vmax);
			}

			if (!hasCurrentField) {
				ImGui::EndDisabled();
			}


			ImGui::EndTable();
		}
	}
	else if (selectedItem == "Colormap") {

		sectionHeader("Options");
		if (beginPropertyTable("ColormapOptions", 90.0f)) {
			labelRow("Colormap");
			if (createSimpleCombo("##Colormap", colormap.items, colormap.currentItem, IM_ARRAYSIZE(colormap.items))) {
				colormap.setColormap(colormap.currentItem);
			}

			labelRow("Shading");
			if (createSimpleCombo("##Shading", results.shadingType, (int&)results.currentShadingType, IM_ARRAYSIZE(results.shadingType))) {
				GLint shadingMode = (results.currentShadingType == ShadingType::Interp) ? GL_LINEAR : GL_NEAREST;
				results.setTextureShadingAllField(shadingMode);
			}

			ImGui::EndTable();
		}

		sectionHeader("Display Settings");
		if (beginPropertyTable("DisplaySettings", 110.0f)) {
			labelRow("Precision");
			inputInt("##NumberPrecision", &colorbar.currentPrecision);

			labelRow("Number Format");
			createSimpleCombo("##NumberFormat", colorbar.formatOption, (int&)colorbar.currentNumberFormat, IM_ARRAYSIZE(colorbar.formatOption));

			ImGui::EndTable();
		}
	}
	else if (selectedItem == "Graphics") {
		drawFieldSelector();
	}

	ImGui::End();
}

void ResultsGUI::drawFieldSelector() {

	const std::vector<std::string>& fieldType = results.fieldType;

	sectionHeader("Displayed Fields");
	ImGui::TextWrapped(
		"Drag a field into Shown to add it as a tab in the inspector. "
		"Drag it back (or double-click) to remove it."
	);
	ImGui::Spacing();

	const float spacing = ImGui::GetStyle().ItemSpacing.x;
	const float colWidth = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;
	const float colHeight = 240.0f;

	// payload shared by both regions: the index of the dragged field within
	// fieldType. The drop target decides add vs remove based on which region it is.
	const char* DND_FIELD = "DND_FIELD";

	// ------------------------- Available (all fields) -------------------------
	ImGui::BeginGroup();
	ImGui::TextUnformatted("Available");
	ImGui::BeginChild("##AvailableFields", ImVec2(colWidth, colHeight), true);

	for (int i = 0; i < (int)fieldType.size(); i++) {

		// a field already in Shown does not also appear here - each name lives
		// in exactly one region
		if (results.isShown(fieldType[i])) {
			continue;
		}

		ImGui::Selectable(fieldType[i].c_str());

		// double-click as a shortcut for the drag
		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
			results.addShownField(fieldType[i]);
		}

		if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
			ImGui::SetDragDropPayload(DND_FIELD, &i, sizeof(int));
			ImGui::TextUnformatted(fieldType[i].c_str());	// drag preview
			ImGui::EndDragDropSource();
		}
	}

	ImGui::EndChild();

	// dropping onto Available removes the field from the shown set
	if (ImGui::BeginDragDropTarget()) {
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(DND_FIELD)) {
			int idx = *(const int*)payload->Data;
			if (idx >= 0 && idx < (int)fieldType.size()) {
				results.removeShownField(fieldType[idx]);
			}
		}
		ImGui::EndDragDropTarget();
	}
	ImGui::EndGroup();

	ImGui::SameLine();

	// ------------------------- Shown (inspector tabs) -------------------------
	ImGui::BeginGroup();
	ImGui::TextUnformatted("Shown");
	ImGui::BeginChild("##ShownFields", ImVec2(colWidth, colHeight), true);

	for (int i = 0; i < (int)results.shownFields.size(); i++) {
		const std::string& name = results.shownFields[i];

		ImGui::Selectable(name.c_str());

		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
			results.removeShownField(name);
			break;	// shownFields was mutated; stop iterating this frame
		}

		// dragging a shown field lets the user drop it onto Available to remove it
		if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
			int idx = results.indexOfField(name);
			ImGui::SetDragDropPayload(DND_FIELD, &idx, sizeof(int));
			ImGui::TextUnformatted(name.c_str());
			ImGui::EndDragDropSource();
		}
	}

	ImGui::EndChild();

	// dropping onto Shown adds the field (and activates its new inspector tab)
	if (ImGui::BeginDragDropTarget()) {
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(DND_FIELD)) {
			int idx = *(const int*)payload->Data;
			if (idx >= 0 && idx < (int)fieldType.size()) {
				results.addShownField(fieldType[idx]);
			}
		}
		ImGui::EndDragDropTarget();
	}
	ImGui::EndGroup();
}

void ResultsGUI::draw() {

	ImGuiTabItemFlags tabFlags = ImGuiTabItemFlags_None;
	if (project.tabSwitchRequested && project.requestedTab == ViewTab::TAB_RESULTS) {
		tabFlags = ImGuiTabItemFlags_SetSelected;
	}

	if (ImGui::BeginTabItem("Results", nullptr, tabFlags)) {
		project.currentTab = ViewTab::TAB_RESULTS;

		ImGui::BeginChild("SetupTree", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing()), true);

		drawLeaf("General", &assets.icon("general"));

		drawLeaf("Colormap", &assets.icon("colormap"));

		drawLeaf("Graphics", &assets.icon("graphics"));

		ImGui::EndChild();

		if (actionButton("Generate Results")) {
			if (project.solver.isReady) {
				project.results.generate(project.mesh, project.solver);
				gui.inspector.generate();
				scene.createBuffer();
			}
		}

		drawPropertiesPanel();

		ImGui::EndTabItem();

	}
}
