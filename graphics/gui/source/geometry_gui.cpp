#include "geometry_gui.h"

#include "project.h"
#include "gui.h"

#include "printer.h"

#include <algorithm>

namespace {
	const char* sketchEntityTypeName(SketchEntityType type) {
		switch (type) {
		case SketchEntityType::Line:
			return "Line";
		case SketchEntityType::Circle:
			return "Circle";
		case SketchEntityType::Rectangle:
			return "Rectangle";
		case SketchEntityType::Arc:
			return "Arc";
		case SketchEntityType::Point:
			return "Point";
		default:
			return "Unknown";
		}
	}
}

GeometryGUI::GeometryGUI(Project& project, GUI& gui) :
	project(project),
	gui(gui) {

}

void GeometryGUI::drawPropertiesPanel() {
	SketchModel& sketch = project.geometry.sketch;

	ImGui::Begin("Overview");
	if (selectedItem == "Geometry") {
		drawTableHeader("Geometry");

		if (ImGui::BeginTable("GeometryStats", 2, UIFlags::TableSimpleFlags)) {
			setupTableColumns(
				column("Label", 150.0f),
				column("Value", 100.0f, ImGuiTableColumnFlags_WidthStretch)
			);

			std::string lines = std::to_string(sketch.lines.size());
			std::string rectangles = std::to_string(sketch.rectangles.size());
			std::string circles = std::to_string(sketch.circles.size());
			std::string arcs = std::to_string(sketch.arcs.size());
			std::string namedSelections =
				std::to_string(sketch.namedSelections.size());

			drawTableProperty("Lines", lines.c_str());
			drawTableProperty("Rectangles", rectangles.c_str());
			drawTableProperty("Circles", circles.c_str());
			drawTableProperty("Arcs", arcs.c_str());
			drawTableProperty("Named Segments", namedSelections.c_str());

			ImGui::EndTable();
		}
	}
	else if (selectedItem == "Named Segments") {
		drawTableHeader("Named Segments");

		if (sketch.namedSelections.empty()) {
			ImGui::TextDisabled("No named segments");
		}
		else if (ImGui::BeginTable(
			"NamedSegmentSummary",
			2,
			UIFlags::TableSimpleFlags
		)) {
			setupTableColumns(
				column("Name", 150.0f),
				column("Segments", 80.0f, ImGuiTableColumnFlags_WidthStretch)
			);

			for (const SketchNamedSelection& selection :
				sketch.namedSelections) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(selection.name.c_str());
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%d", (int)selection.segments.size());
			}

			ImGui::EndTable();
		}
	}
	else if (selectedItem == "Named Segment") {
		const SketchNamedSelection* selectedSelection = nullptr;

		for (const SketchNamedSelection& selection : sketch.namedSelections) {
			if (selection.id == selectedNamedSelectionID) {
				selectedSelection = &selection;
				break;
			}
		}

		if (!selectedSelection) {
			selectedNamedSelectionID = -1;
			selectedItem = "Named Segments";
		}
		else {
			drawTableHeader(selectedSelection->name.c_str());

			if (ImGui::BeginTable(
				"NamedSegmentDetails",
				2,
				UIFlags::TableSimpleFlags
			)) {
				setupTableColumns(
					column("Label", 150.0f),
					column("Value", 100.0f, ImGuiTableColumnFlags_WidthStretch)
				);

				std::string id = std::to_string(selectedSelection->id);
				std::string count =
					std::to_string(selectedSelection->segments.size());

				drawTableProperty("ID", id.c_str());
				drawTableProperty("Segments", count.c_str());

				ImGui::EndTable();
			}

			if (ImGui::BeginTable(
				"NamedSegmentRows",
				5,
				UIFlags::TableSimpleFlags
			)) {
				setupTableColumns(
					column("Type", 90.0f),
					column("Entity", 60.0f),
					column("Edge", 50.0f),
					column("Start", 75.0f),
					column("End", 75.0f)
				);

				ImGui::TableHeadersRow();

				for (const SketchNamedSegment& segment :
					selectedSelection->segments) {
					ImGui::TableNextRow();

					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(
						sketchEntityTypeName(segment.sourceType)
					);

					ImGui::TableSetColumnIndex(1);
					ImGui::Text("%d", segment.entityID);

					ImGui::TableSetColumnIndex(2);
					if (segment.edgeIndex >= 0) {
						ImGui::Text("%d", segment.edgeIndex);
					}
					else {
						ImGui::TextUnformatted("-");
					}

					ImGui::TableSetColumnIndex(3);
					ImGui::Text("%.3f", segment.startT);

					ImGui::TableSetColumnIndex(4);
					ImGui::Text("%.3f", segment.endT);
				}

				ImGui::EndTable();
			}

			ImGui::Spacing();
			if (ImGui::Button("Delete Named Segment")) {
				int deleteID = selectedNamedSelectionID;
				sketch.namedSelections.erase(
					std::remove_if(
						sketch.namedSelections.begin(),
						sketch.namedSelections.end(),
						[&](const SketchNamedSelection& selection) {
							return selection.id == deleteID;
						}
					),
					sketch.namedSelections.end()
				);

				selectedNamedSelectionID = -1;
				selectedItem = "Named Segments";
			}
		}
	}

	ImGui::End();

}

void GeometryGUI::draw() {

	if (ImGui::BeginTabItem("Geometry")) {
		project.currentTab = ViewTab::TAB_GEOMETRY;

		ImGui::BeginChild("SetupTree", ImVec2(0.0f, 600.0f), true);

		bool geometryOpen =
			ImGui::TreeNodeEx("Geometry", UIFlagsTree::BranchOpenedFlags);

		if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
			selectedNamedSelectionID = -1;
			selectedItem = "Geometry";
		}
		changeCursorOnHover();

		if (geometryOpen) {
			bool namedSegmentsOpen =
				ImGui::TreeNodeEx(
					"Named Segments",
					UIFlagsTree::BranchOpenedFlags |
					(selectedItem == "Named Segments" ?
						ImGuiTreeNodeFlags_Selected :
						0)
				);

			if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
				selectedNamedSelectionID = -1;
				selectedItem = "Named Segments";
			}
			changeCursorOnHover();

			if (namedSegmentsOpen) {
				for (const SketchNamedSelection& selection :
					project.geometry.sketch.namedSelections) {
					ImGui::PushID(selection.id);

					bool selected =
						selectedItem == "Named Segment" &&
						selectedNamedSelectionID == selection.id;

					ImGui::TreeNodeEx(
						selection.name.c_str(),
						UIFlagsTree::LeafFlags |
						(selected ? ImGuiTreeNodeFlags_Selected : 0)
					);

					if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
						selectedNamedSelectionID = selection.id;
						selectedItem = "Named Segment";
					}
					changeCursorOnHover();

					ImGui::PopID();
				}

				ImGui::TreePop();
			}

			ImGui::TreePop();
		}

		ImGui::EndChild();

		drawPropertiesPanel();

		ImGui::EndTabItem();
	}

}
