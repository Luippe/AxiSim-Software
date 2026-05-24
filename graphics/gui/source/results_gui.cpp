#include "results_gui.h"
#include "gui.h"
#include "scene_view.h"
#include "results.h"
#include "mesh.h"
#include "colormap.h"
#include "graphics_struct.h"
#include "gui_manager.h"
#include "colorbar.h"

ResultsGUI::ResultsGUI(GUI& gui, SceneView& scene) :
	gui(gui),
	scene(scene),
	mesh(scene.mesh),
	results(scene.results),
	colormap(scene.colormap),
	colorbar(gui.inspector.colorbar){
}

void ResultsGUI::drawPropertiesPanel() {

	ImGui::Begin("Overview");

	if (selectedItem == "Settings") {

	}
	else if (selectedItem == "View") {
		if (ImGui::BeginTable("Geometry", 3)) {
			ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 45.0f);
			ImGui::TableSetupColumn("Slider", ImGuiTableColumnFlags_WidthStretch);

			textAtNewRow("Front", 0, 1);
			if (ImGui::SliderInt("##Front", &results.colFront, 0, mesh.g.nz)) {
				results.currentFront = (float)results.colFront * (float)mesh.g.dz;
				results.updateModel();
			}

			textAtNewRow("Back", 0, 1);
			if (ImGui::SliderInt("##Back", &results.colBack, 0, mesh.g.nz)) {
				results.currentBack = (float)results.colBack * (float)mesh.g.dz;
				results.updateModel();
			}

			textAtNewRow("Outer", 0, 1);
			if (ImGui::SliderInt("##Outer", &results.rowTop, 0, mesh.g.nr)) {
				results.currentOuter = (float)results.rowTop * (float)mesh.g.dr;
				results.updateModel();
			}

			textAtNewRow("Inner", 0, 1);
			if (ImGui::SliderInt("##Inner", &results.rowBot, 0, mesh.g.nr)) {
				results.currentInner = (float)results.rowBot * (float)mesh.g.dr;
				results.updateModel();
			}

			textAtNewRow("Filter", 0, 1);
			if (createSimpleCombo("##Filter", results.compareType, (int&)results.currentCompareType, IM_ARRAYSIZE(results.compareType))) {
				results.updateSelectedInstances();
			}

			textAtNewRow("Value", 0, 1);
			if (results.currentCompareType == CompareType::Between || results.currentCompareType == CompareType::Exclude) {
				ImGui::InputFloat("##LowerBound", &results.filterValues.valueLower, 0.0f, 0.0f);
				ImGui::TableNextColumn();
				ImGui::InputFloat("##UpperBound", &results.filterValues.valueUpper, 0.0f, 0.0f);
			}
			else {
				ImGui::SliderFloat("##Value", &results.filterValues.valueAt, results.currentField->vmin, results.currentField->vmax);
			}



			ImGui::EndTable();
		}
	}
	else if (selectedItem == "Change Colormap") {

		ImGui::SeparatorText("Colormap Settings");

		if (ImGui::BeginTable("Colormap", 2)) {
			ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 70.0f);
			ImGui::TableSetupColumn("Combo", ImGuiTableColumnFlags_WidthFixed, 120.0f);

			textAtNewRow("Colormap", 0, 1);
			if (createSimpleCombo("##Colormap", colormap.items, colormap.currentItem, IM_ARRAYSIZE(colormap.items))) {
				colormap.setColormap(colormap.currentItem);
			}

			ImGui::EndTable();
		}

		ImGui::SeparatorText("Colormap Settings");
		if (ImGui::BeginTable("Shading", 2)) {
			ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 70.0f);
			ImGui::TableSetupColumn("Combo", ImGuiTableColumnFlags_WidthFixed, 120.0f);

			textAtNewRow("Shading", 0, 1);
			if (createSimpleCombo("##Shading", results.shadingType, (int&)results.currentShadingType, IM_ARRAYSIZE(results.shadingType))) {
				
				GLint shadingMode = (results.currentShadingType == ShadingType::Interp) ? GL_LINEAR : GL_NEAREST;	// choose lienar and flat shading
				results.currentField->textureBuffer.setTextureShading(shadingMode);
			}
			ImGui::EndTable();
		}
	}
	else if (selectedItem == "Display") {

		ImGui::SeparatorText("Display Settings");

		if (ImGui::BeginTable("Display Settings", 2)) {
			ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 80.0f);
			ImGui::TableSetupColumn("Input", ImGuiTableColumnFlags_WidthFixed, 120.0f);

			textAtNewRow("Precision", 0, 1);
			ImGui::InputInt("##NumberPrecision", &colorbar.currentPrecision, 0, 0);

			textAtNewRow("Number Format", 0, 1);
			createSimpleCombo("##NumberFormat", colorbar.formatOption, (int&)colorbar.currentNumberFormat, IM_ARRAYSIZE(colorbar.formatOption));

			ImGui::EndTable();
		}
	}
	ImGui::End();
}

void ResultsGUI::draw() {

	if (ImGui::BeginTabItem("Results")) {
		scene.currentTab = TAB_RESULTS;

		ImGui::BeginChild("SetupTree", ImVec2(260, 600), true);

		if (ImGui::BeginTable("Field", 2)) {
			ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 70.0f);
			ImGui::TableSetupColumn("Combo", ImGuiTableColumnFlags_WidthFixed, 120.0f);

			textAtNewRow("Field", 0, 1);
			if (createSimpleCombo("##SelectField", results.fieldType, results.currentItem, IM_ARRAYSIZE(results.fieldType))) {
				results.updateCurrentField();
			}

			ImGui::EndTable();
		}

		if (ImGui::TreeNodeEx("General", UIFlags::BranchFlags)) {
			drawLeaf("Settings");
			drawLeaf("View");
			ImGui::TreePop();
		}
		changeCursorOnHover();

		if (ImGui::TreeNodeEx("Colormap", UIFlags::BranchFlags)) {
			drawLeaf("Change Colormap");
			drawLeaf("Display");
			ImGui::TreePop();
		}
		changeCursorOnHover();

		ImGui::EndChild();

		if (ImGui::Button("Generate Results")) {

			scene.results.generate();
			gui.inspector.generate();

		}
		changeCursorOnHover();

		drawPropertiesPanel();

		ImGui::EndTabItem();

	}
}