#include "mesh_gui.h"
#include "scene_view.h"
#include "gui.h"
#include "results.h"
#include "colormap.h"
#include "mesh.h"

#include "solver_struct.h"
#include "unit_manager.h"
#include "gui_manager.h"
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
	//config.g.dz = gridConfigEdits.L / gridConfigEdits.nz;
	//config.g.dr = gridConfigEdits.R / gridConfigEdits.nr;
}

void MeshGUI::drawPropertiesPanel() {
	ImGui::Begin("Overview");

	if (selectedItem == "Edit") {
		ImGui::SeparatorText("Mesh Settings");
		if (ImGui::BeginTable("Input", 3)) {
			ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 80.0f);
			ImGui::TableSetupColumn("Input", ImGuiTableColumnFlags_WidthFixed, 100.0f);
			ImGui::TableSetupColumn("Units", ImGuiTableColumnFlags_WidthFixed, 100.0f);


			textAtNewRow("Segments", 0, 1);
			ImGui::InputInt("##MeshNseg", &gridConfigEdits.nseg, 0.0, 0.0);

			inputDoubleWithUnits("Length", gridConfigEdits.L, scene.config.varUnits.LUnit, Units::lengthUnits);
			inputDoubleWithUnits("Radius", gridConfigEdits.R, scene.config.varUnits.RUnit, Units::lengthUnits);

			textAtNewRow("nr", 0, 1);
			ImGui::InputInt("##Meshnr", &gridConfigEdits.nr, 0.0, 0.0);

			textAtNewRow("nz", 0, 1);
			ImGui::InputInt("##Meshnz", &gridConfigEdits.nz, 0.0, 0.0);

			textAtNewRow("Radial Bias Factor", 0, 1);
			ImGui::InputDouble("##MeshRadialBias", &config.g.rBias, 0.0, 0.0);

			textAtNewRow("Axial Bias Factor", 0, 1);
			ImGui::InputDouble("##MeshAxialBias", &config.g.zBias, 0.0, 0.0);

			ImGui::EndTable();
		}
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

		drawPropertiesPanel();

		ImGui::EndTabItem();
	}
	changeCursorOnHover();
}