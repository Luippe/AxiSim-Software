#include "geometry_gui.h"

#include "project.h"

GeometryGUI::GeometryGUI(Project& project) :
	project(project) {

}

void GeometryGUI::drawPropertiesPanel() {
	SketchModel& sketch = project.geometry.sketch;

	ImGui::Begin("Overview");
	if (selectedItem == "Geometry") {
		sectionHeader("Geometry");

		if (beginPropertyTable("GeometryStats")) {
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

		ImGui::BeginChild("SetupTree", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing()), true);

		bool geometryOpen = false;
		drawLeaf("Geometry");

		if (geometryOpen) {
			ImGui::TreePop();
		}

		ImGui::EndChild();

		drawPropertiesPanel();

		ImGui::EndTabItem();
	}
}
