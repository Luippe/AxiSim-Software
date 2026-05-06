#include "results_gui.h"
#include "gui.h"
#include "scene_view.h"
#include "results.h"
#include "mesh.h"
#include "colormap.h"
#include "graphics_struct.h"

ResultsGUI::ResultsGUI(GUI& gui, SceneView& scene) :
	gui(gui),
	scene(scene),
	mesh(scene.mesh),
	results(scene.results),
	colormap(scene.colormap){
}

void ResultsGUI::draw() {

	if (ImGui::BeginTabItem("Results")) {

		if (scene.currentTab != TAB_RESULTS) {
			scene.currentTab = TAB_RESULTS;
		}

		ImGui::Text("Results Settings");

		if (ImGui::Button("Generate Results")) {

			scene.results.generate();
			gui.inspector.generate();

		}
		changeCursorOnHover();

		if (ImGui::CollapsingHeader("View"), ImGuiTreeNodeFlags_DefaultOpen) {
			if (ImGui::BeginTable("Field", 2)) {
				ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 70.0f);
				ImGui::TableSetupColumn("Combo", ImGuiTableColumnFlags_WidthFixed, 120.0f);

				textAtNewRow("Field", 0, 1);
				if (createSimpleCombo("##SelectField", results.fieldType, results.currentItem, IM_ARRAYSIZE(results.fieldType))) {
					results.updateCurrentField();
					gui.inspector.generate();
				}

				ImGui::EndTable();
			}
		}

		if (ImGui::CollapsingHeader("Edit"), ImGuiTreeNodeFlags_DefaultOpen) {

			ImGui::SeparatorText("Geometry");

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

				ImGui::EndTable();
			}

			ImGui::SeparatorText("Colormap Options");
			if (ImGui::BeginTable("Colormap", 2)) {
				ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 70.0f);
				ImGui::TableSetupColumn("Combo", ImGuiTableColumnFlags_WidthFixed, 120.0f);

				textAtNewRow("Colormap", 0, 1);
				if (createSimpleCombo("##Colormap", colormap.items, colormap.currentItem, IM_ARRAYSIZE(colormap.items))) {
					colormap.setColormap(colormap.currentItem);
					results.uploadColormap();
					gui.inspector.generate();
				}

				ImGui::EndTable();
			}
		}
		ImGui::EndTabItem();
		changeCursorOnHover();

	}
	changeCursorOnHover();
}