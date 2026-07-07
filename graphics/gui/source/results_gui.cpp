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
	scene(gui.scene),
	results(project.results),
	colormap(gui.scene.colormap),
	colorbar(gui.inspector.colorbar){
}

void ResultsGUI::drawPropertiesPanel() {

	ImGui::Begin("Overview");

	if (selectedItem == "View") {
		if (ImGui::BeginTable("Geometry", 3)) {
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
	else if (selectedItem == "Change Colormap") {

		ImGui::SeparatorText("Colormap Settings");

		if (ImGui::BeginTable("Colormap", 2)) {
			ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 70.0f);
			ImGui::TableSetupColumn("Combo", ImGuiTableColumnFlags_WidthFixed, 120.0f);

			labelRow("Colormap");
			if (createSimpleCombo("##Colormap", colormap.items, colormap.currentItem, IM_ARRAYSIZE(colormap.items))) {
				colormap.setColormap(colormap.currentItem);
			}

			ImGui::EndTable();
		}

		ImGui::SeparatorText("Colormap Settings");
		if (ImGui::BeginTable("Shading", 2)) {
			ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 70.0f);
			ImGui::TableSetupColumn("Combo", ImGuiTableColumnFlags_WidthFixed, 120.0f);

			labelRow("Shading");
			if (createSimpleCombo("##Shading", results.shadingType, (int&)results.currentShadingType, IM_ARRAYSIZE(results.shadingType))) {
				
				GLint shadingMode = (results.currentShadingType == ShadingType::Interp) ? GL_LINEAR : GL_NEAREST;	// choose lienar and flat shading
				results.setTextureShadingAllField(shadingMode);

			}
			ImGui::EndTable();
		}
	}
	else if (selectedItem == "Display") {

		ImGui::SeparatorText("Display Settings");

		if (ImGui::BeginTable("Display Settings", 2)) {
			ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 80.0f);
			ImGui::TableSetupColumn("Input", ImGuiTableColumnFlags_WidthFixed, 120.0f);

			labelRow("Precision");
			ImGui::InputInt("##NumberPrecision", &colorbar.currentPrecision, 0, 0);

			labelRow("Number Format");
			createSimpleCombo("##NumberFormat", colorbar.formatOption, (int&)colorbar.currentNumberFormat, IM_ARRAYSIZE(colorbar.formatOption));

			ImGui::EndTable();
		}
	}
	ImGui::End();
}

void ResultsGUI::draw() {

	if (ImGui::BeginTabItem("Results")) {
		project.currentTab = ViewTab::TAB_RESULTS;

		ImGui::BeginChild("SetupTree", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing()), true);

		if (ImGui::BeginTable("Field", 2)) {
			ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 70.0f);
			ImGui::TableSetupColumn("Combo", ImGuiTableColumnFlags_WidthFixed, 120.0f);

			labelRow("Field");
			if (createCombo(
				"##SelectField",
				project.results.fieldType,
				results.currentItem
			)) {
				results.updateCurrentField();
			}

			ImGui::EndTable();
		}

		if (ImGui::TreeNodeEx("General", UIFlagsTree::BranchOpenedFlags)) {
			drawLeaf("View");
			ImGui::TreePop();
		}
		changeCursorOnHover();

		if (ImGui::TreeNodeEx("Colormap", UIFlagsTree::BranchOpenedFlags)) {
			drawLeaf("Change Colormap");
			drawLeaf("Display");
			ImGui::TreePop();
		}
		changeCursorOnHover();

		ImGui::EndChild();

		if (ImGui::Button("Generate Results")) {
			if (project.solver.isReady) {
				project.results.generate(project.mesh, project.solver);
				gui.inspector.generate();
				scene.createBuffer();
			}
		}
		changeCursorOnHover();

		drawPropertiesPanel();

		ImGui::EndTabItem();

	}
}
