#include "mesh_gui.h"
#include "project.h"

#include "gui.h"
#include "mesh.h"

#include "solver_struct.h"
#include "boundary_struct.h"

#include "boundary_func.h"
#include "unit_manager.h"
#include "flag_manager.h"

#include <algorithm>

using namespace BoundaryGet;

MeshGUI::MeshGUI(Project& project, GUI& gui) :
	project(project),
	gui(gui),
	mesh(project.mesh),
	assets(gui.appConfig.assets),
	config(project.config){
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


	if (ImGui::Button("Delete Boundary Group")) {

		hasChanged = true;

		if (gui.meshInspector.deleteBoundaryGroupByID(selectedGroup->id)) {
			selectedBoundaryGroupID = -1;
		}

		return;
	}
}

void MeshGUI::drawRegionOfInfluenceGUI() {
	ImGui::SeparatorText("Region of Influence");

	if (ImGui::Button("Add Region")) {
		MeshRegionOfInfluence region{};
		region.id = mesh.nextRegionOfInfluenceID++;
		region.shape = MeshRegionShape::Circle;
		region.center = Vec2{ config.g.L * 0.5, config.g.R * 0.5 };
		region.radius = std::max(std::min(config.g.L, config.g.R) * 0.1, 1e-6);
		region.min = Vec2{
			region.center.z - region.radius,
			region.center.r - region.radius
		};
		region.max = Vec2{
			region.center.z + region.radius,
			region.center.r + region.radius
		};
		region.targetSpacing = std::max(std::min(config.g.L, config.g.R) / 80.0, 1e-6);
		region.outsideSpacing = std::max(std::min(config.g.L, config.g.R) / 10.0, 1e-6);
		region.transitionThickness = region.radius * 0.5;
		mesh.regionsOfInfluence.push_back(region);
		hasChanged = true;
	}

	ImGui::Spacing();

	int deleteID = -1;

	for (MeshRegionOfInfluence& region : mesh.regionsOfInfluence) {
		ImGui::PushID(region.id);

		std::string label = "Region " + std::to_string(region.id);
		if (ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
			if (ImGui::Checkbox("Enabled", &region.enabled)) {
				hasChanged = true;
			}

			if (ImGui::BeginTable("RegionFields", 2)) {
				setupTableColumns(
					column("Label", 150.0f),
					column("Input", 120.0f, ImGuiTableColumnFlags_WidthStretch)
				);

				const char* shapeItems[] = { "Circle", "Rectangle" };
				int shapeIndex = static_cast<int>(region.shape);

				labelRow("Shape");
				if (createSimpleCombo("##Shape", shapeItems, shapeIndex, IM_ARRAYSIZE(shapeItems))) {
					region.shape = static_cast<MeshRegionShape>(shapeIndex);
					hasChanged = true;
				}

				if (region.shape == MeshRegionShape::Circle) {
					labelRow("Center Z");
					if (inputDouble("##CenterZ", &region.center.z, "%.6g")) {
						hasChanged = true;
					}

					labelRow("Center R");
					if (inputDouble("##CenterR", &region.center.r, "%.6g")) {
						hasChanged = true;
					}

					labelRow("Radius");
					if (inputDouble("##Radius", &region.radius, "%.6g")) {
						hasChanged = true;
					}
				}
				else {
					labelRow("Z Min");
					if (inputDouble("##ZMin", &region.min.z, "%.6g")) {
						hasChanged = true;
					}

					labelRow("Z Max");
					if (inputDouble("##ZMax", &region.max.z, "%.6g")) {
						hasChanged = true;
					}

					labelRow("R Min");
					if (inputDouble("##RMin", &region.min.r, "%.6g")) {
						hasChanged = true;
					}

					labelRow("R Max");
					if (inputDouble("##RMax", &region.max.r, "%.6g")) {
						hasChanged = true;
					}
				}

				labelRow("Target Spacing");
				if (inputDouble("##TargetSpacing", &region.targetSpacing, "%.6g")) {
					hasChanged = true;
				}

				labelRow("Outside Spacing");
				if (inputDouble("##OutsideSpacing", &region.outsideSpacing, "%.6g")) {
					hasChanged = true;
				}

				labelRow("Transition");
				if (inputDouble("##Transition", &region.transitionThickness, "%.6g")) {
					hasChanged = true;
				}

				ImGui::EndTable();
			}

			region.center.z = std::max(0.0, std::min(region.center.z, config.g.L));
			region.center.r = std::max(0.0, std::min(region.center.r, config.g.R));
			region.radius = std::max(region.radius, 1e-12);
			region.min.z = std::max(0.0, std::min(region.min.z, config.g.L));
			region.max.z = std::max(0.0, std::min(region.max.z, config.g.L));
			region.min.r = std::max(0.0, std::min(region.min.r, config.g.R));
			region.max.r = std::max(0.0, std::min(region.max.r, config.g.R));
			region.targetSpacing = std::max(region.targetSpacing, 1e-12);
			region.outsideSpacing = std::max(region.outsideSpacing, 0.0);
			region.transitionThickness = std::max(region.transitionThickness, 0.0);

			if (region.shape == MeshRegionShape::Circle) {
				region.min = Vec2{
					region.center.z - region.radius,
					region.center.r - region.radius
				};
				region.max = Vec2{
					region.center.z + region.radius,
					region.center.r + region.radius
				};
			}
			else {
				double zMin = std::min(region.min.z, region.max.z);
				double zMax = std::max(region.min.z, region.max.z);
				double rMin = std::min(region.min.r, region.max.r);
				double rMax = std::max(region.min.r, region.max.r);

				region.min = Vec2{ zMin, rMin };
				region.max = Vec2{ zMax, rMax };
				region.center = Vec2{
					0.5 * (zMin + zMax),
					0.5 * (rMin + rMax)
				};
				region.radius = 0.5 * std::min(zMax - zMin, rMax - rMin);
			}

			if (ImGui::Button("Delete Region")) {
				deleteID = region.id;
				hasChanged = true;
			}

			ImGui::TreePop();
		}

		ImGui::PopID();
	}

	if (deleteID >= 0) {
		mesh.regionsOfInfluence.erase(
			std::remove_if(
				mesh.regionsOfInfluence.begin(),
				mesh.regionsOfInfluence.end(),
				[deleteID](const MeshRegionOfInfluence& region) {
					return region.id == deleteID;
				}
			),
			mesh.regionsOfInfluence.end()
		);
	}
}

void MeshGUI::drawPropertiesPanel() {
	ImGui::Begin("Overview");
	if (selectedItem == "General") {

		sectionHeader("Statistics");

		if (ImGui::BeginTable("StatisticsTable", 2, UIFlags::TableSimpleFlags, ImVec2(0.0f, 0.0f))) {
			setupTableColumns(
				column("Label", 150.0f),
				column("Value", 100.0f, ImGuiTableColumnFlags_WidthStretch)
			);

			// cells/nodes come from different data depending on the mesh type, and are
			// read live so they refresh after each Generate Mesh:
			//  - multiblock: the real totals live on the blocks, NOT the raster g.nr x
			//    g.nz (which is only the resample grid the results view samples). Cells
			//    sum each block's cell count; nodes sum each block's grid points (blocks
			//    own their nodes, so shared seam nodes are counted once per block).
			//  - single-block structured: nr x nz cells, (nr+1)(nz+1) nodes.
			//  - unstructured: counted from its triangles and points.
			std::string numCells;
			std::string numNodes;

			if (mesh.isMultiBlock) {
				size_t nodes = 0;
				for (const Block& b : mesh.multiBlock.blocks) {
					nodes += b.nodes.size();
				}
				numCells = std::to_string(mesh.multiBlock.totalCells);
				numNodes = std::to_string(nodes);
			}
			else if (mesh.currentMeshType == MeshType::Structured) {
				numCells = std::to_string(mesh.g.nr * mesh.g.nz);
				numNodes = std::to_string((mesh.g.nr + 1) * (mesh.g.nz + 1));
			}
			else {
				numCells = std::to_string(mesh.unstructuredTriangles.size());
				numNodes = std::to_string(mesh.unstructuredPoints.size());
			}

			labelRow("Mesh Type");
			createSimpleCombo("##MeshType", mesh.meshType, (int&)mesh.currentMeshType, IM_ARRAYSIZE(mesh.meshType));
			drawTableProperty("Number of Cells", numCells.c_str());
			drawTableProperty("Number of Nodes", numNodes.c_str());

			ImGui::EndTable();
		}
	}
	else if (selectedItem == "Boundary") {

		sectionHeader("Statistics");

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
	else if (selectedItem == "Edit") {
		sectionHeader("Grid Bands");

		const SketchModel& sketch = project.geometry.sketch;

		std::vector<double> zL, rL;
		mesh.computeTrellisLines(sketch, zL, rL);

		if (zL.size() >= 2 && rL.size() >= 2) {
			mesh.ensureBandSizes(zL.size() - 1, rL.size() - 1);

			if (ImGui::BeginTable("BandTable", 2, UIFlags::TableSimpleFlags)) {
				setupTableColumns(
					column("Band", 180.0f),
					column("Cells", 100.0f, ImGuiTableColumnFlags_WidthStretch)
				);

				for (size_t i = 0; i + 1 < zL.size(); i++) {
					ImGui::PushID(static_cast<int>(i));
					char label[64];
					snprintf(label, sizeof(label), "z: %.4g - %.4g", zL[i], zL[i + 1]);
					labelRow(label);
					if (inputInt("##zband", &mesh.zBandCells[i]) && mesh.zBandCells[i] < 1) {
						mesh.zBandCells[i] = 1;
					}
					ImGui::PopID();
				}

				for (size_t j = 0; j + 1 < rL.size(); j++) {
					ImGui::PushID(1000000 + static_cast<int>(j));
					char label[64];
					snprintf(label, sizeof(label), "r: %.4g - %.4g", rL[j], rL[j + 1]);
					labelRow(label);
					if (inputInt("##rband", &mesh.rBandCells[j]) && mesh.rBandCells[j] < 1) {
						mesh.rBandCells[j] = 1;
					}
					ImGui::PopID();
				}

				ImGui::EndTable();
			}

			ImGui::TextWrapped(
				"Axial cells per z-band and radial cells per r-band. "
				"Applies on the next Generate Mesh.");
		}
		else {
			ImGui::TextWrapped(
				"Draw a rectangle or a closed rectilinear outline in the sketch "
				"to define grid bands.");
		}
	}
	else if (selectedItem == "Region of Influence") {
		drawRegionOfInfluenceGUI();
	}

	drawBoundaryGroupGUI();

	ImGui::End();
}

void MeshGUI::draw() {
	if (ImGui::BeginTabItem("Mesh")) {
		project.currentTab = ViewTab::TAB_MESH;

		// fill the available height but leave room for the Generate button below,
		// so the tree scales with the panel and no scrollbar appears
		ImGui::BeginChild("SetupTree", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing()), true);

		// draw general as a childless leaf; clicking it shows mesh statistics in
		// the overview (drawLeaf sets selectedItem and handles the hover cursor)
		if (drawLeaf("General", &assets.icon("general"))) {
			selectedBoundaryGroupID = -1;
			mesh.highlightedBoundarySegmentIDs.clear();
		}

		// draw edit tree node
		bool editOpen = false;
		if (drawTree("Edit", editOpen)) {
			selectedBoundaryGroupID = -1;
			mesh.highlightedBoundarySegmentIDs.clear();
			selectedItem = "Edit";
		}

		if (editOpen) {
			if (drawLeaf("Region of Influence")) {
				selectedBoundaryGroupID = -1;
				mesh.highlightedBoundarySegmentIDs.clear();
				selectedItem = "Region of Influence";
			}
			ImGui::TreePop();
		}


		// draw boundary tree node
		bool boundariesOpen = false;
		if (drawTree("Boundary", boundariesOpen)) {
			selectedBoundaryGroupID = -1;
			mesh.highlightedBoundarySegmentIDs.clear();
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

		ImGui::EndChild();

		// draw generate button
		if (actionButton("Generate Mesh")) {
			const SketchModel& sketch = project.geometry.sketch;
			bool hasSketchGeometry =
				!sketch.lines.empty() ||
				!sketch.rectangles.empty() ||
				!sketch.circles.empty() ||
				!sketch.arcs.empty();

			bool shouldGenerate = true;


			if (mesh.currentMeshType == MeshType::Unstructured &&
				hasSketchGeometry) {
				if (!mesh.convertSketchToUnstructuredMesh(sketch)) {
					shouldGenerate = false;
				}
			}
			else if (mesh.currentMeshType == MeshType::Structured &&
				hasSketchGeometry) {
				std::string reason;
				if (!mesh.sketchSupportsStructured(sketch, reason)) {
					if (mesh.console) {
						mesh.console->addLine(
							("Cannot generate structured grid: " + reason + "\n").c_str());
					}
					shouldGenerate = false;
				}
				else if (!mesh.convertSketchToStructuredMesh(sketch)) {
					shouldGenerate = false;
				}
				else {
					mesh.buildStructuredMultiBlock(sketch);
				}
			}
			else if (mesh.currentMeshType == MeshType::Structured) {
				mesh.selectedBoundaryIDs.clear();
				mesh.highlightedBoundarySegmentIDs.clear();
				mesh.selectableOuterEdges.clear();
				mesh.g.obstacleIndices.clear();
				mesh.g.activeCell.assign(
					static_cast<size_t>(mesh.g.nr) * mesh.g.nz,
					1
				);
				mesh.buildStructuredMultiBlock(sketch);   // single domain-spanning block
			}
			else {

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

			if (shouldGenerate) {
				mesh.generate();
				gui.meshInspector.createGridBuffer();
				gui.meshInspector.markMeshChanged();
			}
		}

		// draw overview window
		drawPropertiesPanel();

		ImGui::EndTabItem();
	}
}
