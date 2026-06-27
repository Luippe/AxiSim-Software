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

			drawTableProperty("Lines", lines.c_str());
			drawTableProperty("Rectangles", rectangles.c_str());
			drawTableProperty("Circles", circles.c_str());
			drawTableProperty("Arcs", arcs.c_str());

			ImGui::EndTable();
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
			selectedItem = "Geometry";
		}
		changeCursorOnHover();

		if (geometryOpen) {
			ImGui::TreePop();
		}

		ImGui::EndChild();

		drawPropertiesPanel();

		ImGui::EndTabItem();
	}
}
