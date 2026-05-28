#include "mesh_gui.h"
#include "scene_view.h"
#include "gui.h"
#include "results.h"
#include "colormap.h"
#include "mesh.h"

#include "solver_struct.h"
#include "unit_manager.h"
#include "flag_manager.h"
#include "printer.h"

MeshGUI::MeshGUI(GUI& gui, SceneView& scene) :
	gui(gui),
	scene(scene),
	mesh(scene.mesh),
	results(scene.results),
	colormap(scene.colormap),
	config(scene.config){

	getGridConfigEdits();
}

void MeshGUI::drawSections() {

	if (ImGui::BeginChild("HELLO", ImVec2(0, 110), true)) {
		ImGui::TextUnformatted("ASDASDa");
		ImGui::Separator();



	}
	ImGui::EndChild();

}

void MeshGUI::getGridConfigEdits() {
	gridConfigEdits.nseg = mesh.nseg;
	gridConfigEdits.L = config.g.L;
	gridConfigEdits.R = config.g.R;
	gridConfigEdits.nr = config.g.nr;
	gridConfigEdits.nz = config.g.nz;
}

void MeshGUI::setGridConfigEdits() {
	mesh.nseg = gridConfigEdits.nseg;
	config.g.L = gridConfigEdits.L;
	config.g.R = gridConfigEdits.R;
	config.g.nr = gridConfigEdits.nr;
	config.g.nz = gridConfigEdits.nz;
	config.g.N = gridConfigEdits.nr * gridConfigEdits.nz;
}

void MeshGUI::drawOverview() {
	ImGui::Begin("Overview");
	if (selectedItem == "Edit") {

		ImGui::TextUnformatted("Geometry");
		ImGui::BeginChild("Geometry", ImVec2(330.0f, 62.0f), true);	// total width = sum of table width + 10 * num of columns to account for padding
		if (ImGui::BeginTable("Geometry", 3)) {

			setupTableColumns(
				column("Label", 150.0f),
				column("Input", 100.0f),
				column("Units", 50.0f)
			);

			labelRow("Length");
			inputDoubleWithUnits(
				"##Length",
				gridConfigEdits.L,
				config.varUnits.LUnit,
				Units::lengthUnits
			);

			labelRow("Radius");
			inputDoubleWithUnits(
				"##Radius",
				gridConfigEdits.R,
				config.varUnits.RUnit,
				Units::lengthUnits
			);

			ImGui::EndTable();
		}
		ImGui::EndChild();

		ImGui::Spacing();
		ImGui::TextUnformatted("Mesh");
		ImGui::BeginChild("Mesh", ImVec2(270, 110), true);
		if (ImGui::BeginTable("Mesh", 2)) {

			setupTableColumns(
				column("Label", 150.0f),
				column("Input", 100.0f)
			);

			labelRow("Axial Segments");
			inputInt("##Meshnz", &gridConfigEdits.nz);

			labelRow("Radial Segments");
			inputInt("##Meshnr", &gridConfigEdits.nr);

			labelRow("Axial Bias Factor");
			inputDouble("##MeshAxialBias", &config.g.zBias);

			labelRow("Radial Bias Factor");
			inputDouble("##MeshRadialBias", &config.g.rBias);

			ImGui::EndTable();
		}
		ImGui::EndChild();
	}
	ImGui::End();
}

void MeshGUI::draw() {
	if (ImGui::BeginTabItem("Mesh")) {
		scene.currentTab = TAB_MESH;

		ImGui::BeginChild("SetupTree", ImVec2(260, 600), true);

		if (ImGui::TreeNodeEx("General", UIFlags::BranchFlags)) {
			drawLeaf("Edit");
			ImGui::TreePop();
		}
		changeCursorOnHover();

		ImGui::EndChild();

		if (ImGui::Button("Generate Mesh")) {
			setGridConfigEdits();
			mesh.generate();
			gui.meshInspector.createGridBuffer();
		}
		changeCursorOnHover();

		drawOverview();

		ImGui::EndTabItem();
	}
	changeCursorOnHover();
}