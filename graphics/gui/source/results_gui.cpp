#include "results_gui.h"
#include "gui.h"
#include "scene_view.h"
#include "results.h"
#include "mesh.h"
#include "colormap.h"
#include "graphics_struct.h"
#include "gui_manager.h"

ResultsGUI::ResultsGUI(GUI& gui, SceneView& scene) :
	gui(gui),
	scene(scene),
	mesh(scene.mesh),
	results(scene.results),
	colormap(scene.colormap){
}

void ResultsGUI::drawPropertiesPanel() {

	ImGui::Begin("Overview");

	if (selectedItem == "Settings") {

	}
	else if (selectedItem == "View") {
		if (ImGui::BeginTable("Geometry", 2)) {
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
			ImGui::InputFloat("##Value", &results.selectedValue);
			ImGui::EndTable();
		}
	}
	else if (selectedItem == "Change Colormap") {

		if (ImGui::BeginTable("Colormap", 2)) {
			ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 70.0f);
			ImGui::TableSetupColumn("Combo", ImGuiTableColumnFlags_WidthFixed, 120.0f);

			textAtNewRow("Colormap", 0, 1);
			if (createSimpleCombo("##Colormap", colormap.items, colormap.currentItem, IM_ARRAYSIZE(colormap.items))) {
				colormap.setColormap(colormap.currentItem);
				results.uploadUniforms();
			}

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
			ImGui::TreePop();
		}
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