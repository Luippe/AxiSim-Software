#include "mesh_gui.h"
#include "scene_view.h"
#include "gui.h"
#include "results.h"
#include "colormap.h"
#include "mesh.h"
#include "solver_struct.h"

MeshGUI::MeshGUI(GUI& gui, SceneView& scene) :
	gui(gui),
	scene(scene),
	mesh(scene.mesh),
	results(scene.results),
	colormap(scene.colormap),
	config(scene.config){
	getGridConfigEdits();
}

void MeshGUI::getGridConfigEdits() {
	GridConfigEdits.nseg = mesh.nseg;
	GridConfigEdits.L = config.g.L;
	GridConfigEdits.R = config.g.R;
	GridConfigEdits.nr = config.g.nr;
	GridConfigEdits.nz = config.g.nz;
}

void MeshGUI::setGridConfigEdits() {
	mesh.nseg = GridConfigEdits.nseg;
	config.g.L = GridConfigEdits.L;
	config.g.R = GridConfigEdits.R;
	config.g.nr = GridConfigEdits.nr;
	config.g.nz = GridConfigEdits.nz;
	config.g.N = GridConfigEdits.nr * GridConfigEdits.nz;
	config.g.dz = GridConfigEdits.L / GridConfigEdits.nz;
	config.g.dr = GridConfigEdits.R / GridConfigEdits.nr;
}

void MeshGUI::draw() {
	if (ImGui::BeginTabItem("Mesh")) {

		if (scene.currentTab != TAB_MESH) {
			scene.currentTab = TAB_MESH;
		}

		if (ImGui::CollapsingHeader("Edit"), ImGuiTreeNodeFlags_DefaultOpen) {

			ImGui::SeparatorText("Mesh Settings");
			if (ImGui::BeginTable("Input", 2)) {
				ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("Input", ImGuiTableColumnFlags_WidthFixed, 100.0f);

				textAtNewRow("Segments", 0, 1);
				ImGui::InputInt("##MeshNseg", &GridConfigEdits.nseg, 0.0, 0.0);

				textAtNewRow("Length", 0, 1);
				ImGui::InputDouble("##MeshLength", &GridConfigEdits.L, 0.0, 0.0, "%.3f");

				textAtNewRow("Radius", 0, 1);
				ImGui::InputDouble("##MeshRadius",&GridConfigEdits.R, 0.0, 0.0, "%.3f");

				textAtNewRow("nr", 0, 1);
				ImGui::InputInt("##Meshnr", &GridConfigEdits.nr, 0.0, 0.0);

				textAtNewRow("nz", 0, 1);
				ImGui::InputInt("##Meshnz", &GridConfigEdits.nz, 0.0, 0.0);


				ImGui::EndTable();
			}
			ImGui::SeparatorText("Slice Geometry");

			if (ImGui::BeginTable("Geometry", 2)) {
				ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 45.0f);
				ImGui::TableSetupColumn("Slider", ImGuiTableColumnFlags_WidthStretch);
				
				textAtNewRow("Front", 0, 1);
				if (ImGui::SliderInt("##Front", &mesh.colFront, 0, GridConfigEdits.nz)) {
					mesh.currentFront = (float)mesh.colFront * (float)mesh.g.dz;
				}

				textAtNewRow("Back", 0, 1);
				if (ImGui::SliderInt("##Back", &mesh.colBack, 0, GridConfigEdits.nz)) {
					mesh.currentBack = (float)mesh.colBack * (float)mesh.g.dz;
				}

				textAtNewRow("Outer", 0, 1);
				if (ImGui::SliderInt("##Outer", &mesh.rowTop, 0, GridConfigEdits.nr)) {
					mesh.currentOuter = (float)mesh.rowTop * (float)mesh.g.dr;
				}

				textAtNewRow("Inner", 0, 1);
				if (ImGui::SliderInt("##Inner", &mesh.rowBot, 0, GridConfigEdits.nr)) {
					mesh.currentInner = (float)mesh.rowBot * (float)mesh.g.dr;
				}

				ImGui::EndTable();
			}
		}
		changeCursorOnHover();

		if (ImGui::Button("Generate Mesh")) {
			setGridConfigEdits();
			mesh.generate();
		}
		changeCursorOnHover();

		ImGui::EndTabItem();
	}
	changeCursorOnHover();
}