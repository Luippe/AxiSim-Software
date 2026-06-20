#include "mesh_gui.h"
#include "project.h"

#include "gui.h"
#include "colormap.h"
#include "mesh.h"

#include "solver_struct.h"
#include "boundary_struct.h"

#include "boundary_func.h"
#include "unit_manager.h"
#include "flag_manager.h"

#include "printer.h"

using namespace BoundaryGet;

MeshGUI::MeshGUI(Project& project, GUI& gui) :
	project(project),
	gui(gui),
	mesh(project.mesh),
	colormap(gui.scene.colormap),
	config(project.config){

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
}

const char* edgeOrientName(EdgeOrient orient) {
	switch (orient) {
	case EdgeOrient::Horizontal:
		return "Horizontal";
	case EdgeOrient::Vertical:
		return "Vertical";
	default:
		return "Unknown";
	}
}

void MeshGUI::drawBoundaryGroupGUI() {
	BoundarySegmentGroup* selectedGroup = getBoundaryGroupByID(mesh.boundaryGroups, selectedBoundaryGroupID);

	if (!selectedGroup) {
		selectedBoundaryGroupID = -1;
		mesh.highlightedBoundarySegmentIDs.clear();
		return;
	}

	ImGui::SeparatorText(selectedGroup->name.c_str());

	ImGui::Text("Group ID: %d", selectedGroup->id);
	ImGui::Text("Total Length: %g", selectedGroup->totalLength);

	ImGui::SeparatorText("Sizing");

	BoundarySizing& sizing = selectedGroup->sizing;

	if (createSimpleCombo("##SizingType", mesh.sizingType, (int&)sizing.mode, IM_ARRAYSIZE(mesh.sizingType))) {
		if (!(sizing.mode == BoundarySizingMode::None)) {
			sizing.enabled = true;
		}
		else {
			sizing.enabled = false;
		}
	}

	if (sizing.mode == BoundarySizingMode::EdgeCount) {
		ImGui::InputInt("Edge Count", &sizing.edgeCount);
		ImGui::InputDouble("Bias", &sizing.bias, 0.1, 0.5, "%.3f");
	}
	else if (sizing.mode == BoundarySizingMode::TargetSpacing) {
		ImGui::InputDouble("Target spacing", &sizing.targetSpacing, 0.0001, 0.001, "%.6f");
		ImGui::InputDouble("Bias", &sizing.bias, 0.1, 0.5, "%.3f");
	}


	if (sizing.targetSpacing < 1e-12) {
		sizing.targetSpacing = 1e-12;
	}

	if (sizing.bias < 0.05) {
		sizing.bias = 0.05;
	}

	if (sizing.edgeCount < 1) {
		sizing.edgeCount = 1;
	}


	ImGui::Spacing();

	drawTableHeader("Edges");

	ImGuiTableFlags flags =
		ImGuiTableFlags_Borders |
		ImGuiTableFlags_RowBg |
		ImGuiTableFlags_Resizable |
		ImGuiTableFlags_ScrollY;

	if (ImGui::BeginTable("BoundaryGroupEdges", 3, flags, ImVec2(0.0f, 220.0f))) {
		ImGui::TableSetupColumn("Type");
		ImGui::TableSetupColumn("i");
		ImGui::TableSetupColumn("j");
		ImGui::TableHeadersRow();

		ImGuiListClipper clipper;
		clipper.Begin((int)selectedGroup->edges.size());

		while (clipper.Step()) {
			for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
				const MeshEdge& edge = selectedGroup->edges[row];

				ImGui::TableNextRow();

				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(edgeOrientName(edge.orient));

				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%d", edge.i);

				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%d", edge.j);
			}
		}

		ImGui::EndTable();
	}

	if (ImGui::Button("Delete Boundary Group")) {

		hasChanged = true;

		if (gui.meshInspector.deleteBoundaryGroupByID(selectedGroup->id)) {
			selectedBoundaryGroupID = -1;
		}

		return;
	}
}

void MeshGUI::drawOverview() {
	ImGui::Begin("Overview");
	if (selectedItem == "General") {

		drawTableHeader("Statistics");

		if (ImGui::BeginTable("StatisticsTable", 2, UIFlags::TableSimpleFlags, ImVec2(0.0f, 0.0f))) {
			setupTableColumns(
				column("Label", 150.0f),
				column("Value", 100.0f, ImGuiTableColumnFlags_WidthStretch)
			);

			std::string numCells = std::to_string(mesh.g.nr * mesh.g.nz);
			std::string numNodes = std::to_string((mesh.g.nr + 1) * (mesh.g.nz + 1));

			labelRow("Mesh Type");
			createSimpleCombo("##MeshType", mesh.meshType, (int&)mesh.currentMeshType, IM_ARRAYSIZE(mesh.meshType));
			drawTableProperty("Number of Cells", numCells.c_str());
			drawTableProperty("Number of Nodes", numNodes.c_str());

			ImGui::EndTable();
		}
	}
	else if (selectedItem == "Edit") {

		ImGui::TextUnformatted("Geometry");
		ImGui::BeginChild("Geometry", ImVec2(0.0f, 80.0f), true);	// total width = sum of table width + 10 * num of columns to account for padding
		if (ImGui::BeginTable("Geometry", 3)) {

			setupTableColumns(
				column("Label", 150.0f),
				column("Input", 100.0f),
				column("Units", 50.0f)
			);

			labelRow("Length");
			if (inputDouble("##Length", gridConfigEdits.L, config.varUnits.LUnit, Units::lengthUnits)) {
				hasChanged = true;
			}
			comboUnit("##Length", config.varUnits.LUnit, Units::lengthUnits);

			labelRow("Radius");
			if (inputDouble("##Radius", gridConfigEdits.R, config.varUnits.RUnit, Units::lengthUnits)) {
				hasChanged = true;
			}
			comboUnit("##Radius", config.varUnits.RUnit, Units::lengthUnits);

			ImGui::EndTable();
		}
		ImGui::EndChild();

		ImGui::Spacing();
		ImGui::TextUnformatted("Mesh");
		ImGui::BeginChild("Mesh", ImVec2(0.0f, 140.0f), true);
		if (ImGui::BeginTable("Mesh", 2)) {

			setupTableColumns(
				column("Label", 150.0f),
				column("Input", 100.0f)
			);

			labelRow("Axial Segments");
			if (inputInt("##Meshnz", &gridConfigEdits.nz)) {
				hasChanged = true;
			}

			labelRow("Radial Segments");
			if (inputInt("##Meshnr", &gridConfigEdits.nr)) {
				hasChanged = true;
			}

			labelRow("Axial Bias Factor");
			if (inputDouble("##MeshAxialBias", &config.g.zBias)) {
				hasChanged = true;
			}

			labelRow("Radial Bias Factor");
			if (inputDouble("##MeshRadialBias", &config.g.rBias)) {
				hasChanged = true;
			}

			ImGui::EndTable();
		}
		ImGui::EndChild();
	}
	else if (selectedItem == "Boundary") {

		drawTableHeader("Statistics");

		if (ImGui::BeginTable("StatsticsTable", 2, UIFlags::TableSimpleFlags, ImVec2(0.0f, 220.0f))) {
			setupTableColumns(
				column("Label", 150.0f),
				column("Value", 100.0f, ImGuiTableColumnFlags_WidthStretch)
			);

			std::string numSegs = std::to_string(mesh.boundarySegments.size());
			std::string numGroups = std::to_string(mesh.boundaryGroups.size());

			drawTableProperty("Segments", numSegs.c_str());
			drawTableProperty("Groups", numGroups.c_str());
			ImGui::EndTable();
		}
	}

	drawBoundaryGroupGUI();

	ImGui::End();
}

void MeshGUI::draw() {
	if (ImGui::BeginTabItem("Mesh")) {
		project.currentTab = ViewTab::TAB_MESH;

		ImGui::BeginChild("SetupTree", ImVec2(0.0f, 600), true);

		// draw general tree node
		bool generalOpen = ImGui::TreeNodeEx("General", UIFlagsTree::BranchOpenedFlags);

		if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {

			selectedBoundaryGroupID = -1;
			mesh.highlightedBoundarySegmentIDs.clear();
			selectedItem = "General";

		}

		if (generalOpen) {
			if (drawLeaf("Edit")) {
				selectedBoundaryGroupID = -1;
				mesh.highlightedBoundarySegmentIDs.clear();
			}

			ImGui::TreePop();
		}
		changeCursorOnHover();

		// draw boundary tree node
		bool boundariesOpen = ImGui::TreeNodeEx("Boundary", UIFlagsTree::BranchOpenedFlags);

		if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {

			selectedBoundaryGroupID = -1;
			mesh.highlightedBoundarySegmentIDs.clear();
			selectedItem = "Boundary";

		}

		if (boundariesOpen) {
			for (BoundarySegmentGroup& group : mesh.boundaryGroups) {
				ImGui::PushID(group.id);

				if (drawLeaf(group.name.c_str())) {
					selectedBoundaryGroupID = group.id;
					mesh.highlightSegmentsInGroup(group);
				}

				ImGui::PopID();
			}

			ImGui::TreePop();
		}
		changeCursorOnHover();

		ImGui::EndChild();

		// draw generate button
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
		if (ImGui::Button("Generate Mesh")) {
			const SketchModel& sketch = project.geometry.sketch;
			bool hasSketchGeometry =
				!sketch.lines.empty() ||
				!sketch.rectangles.empty() ||
				!sketch.circles.empty() ||
				!sketch.arcs.empty();

			bool topologyChanged =
				gridConfigEdits.nr != config.g.nr ||
				gridConfigEdits.nz != config.g.nz ||
				gridConfigEdits.L != config.g.L ||
				gridConfigEdits.R != config.g.R;
			bool shouldGenerate = true;

			setGridConfigEdits();

			if (mesh.currentMeshType == MeshType::Unstructured &&
				hasSketchGeometry) {
				if (!mesh.convertSketchToUnstructuredMesh(sketch)) {
					shouldGenerate = false;
				}
				else {
					getGridConfigEdits();
				}
			}
			else {
				if (topologyChanged) {

					mesh.clearUnstructuredGeometry();
					mesh.boundaryGroups.clear();
					mesh.boundarySegments.clear();
					mesh.boundaryEdges.clear();
					mesh.boundaryVertices.clear();
					mesh.gridLineVertices.clear();
					mesh.selectedBoundaryIDs.clear();
					mesh.highlightedBoundarySegmentIDs.clear();
					mesh.selectableOuterEdges.clear();

					selectedBoundaryGroupID = -1;
					mesh.initializeUnstructuredDomain(5, 5);
				}
			}

			if (shouldGenerate) {
				mesh.generate();
				gui.meshInspector.createGridBuffer();
			}
		}
		changeCursorOnHover();
		ImGui::PopStyleVar();

		// draw overview window
		drawOverview();

		ImGui::EndTabItem();
	}
	changeCursorOnHover();
}
