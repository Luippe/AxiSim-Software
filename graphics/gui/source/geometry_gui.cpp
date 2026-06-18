#include "geometry_gui.h"

#include "project.h"
#include "gui.h"

#include "printer.h"

GeometryGUI::GeometryGUI(Project& project, GUI& gui) :
	project(project),
	gui(gui) {

}
void GeometryGUI::drawPropertiesPanel() {

	if (selectedItem == "Geometry") {

	}

}

void GeometryGUI::draw() {

	if (ImGui::BeginTabItem("Geometry")) {
		project.currentTab = ViewTab::TAB_GEOMETRY;

		ImGui::BeginChild("SetupTree", ImVec2(0.0f, 600.0f), true);
		if (drawLeaf("Geometry")) {
			
		}
		changeCursorOnHover();
		ImGui::EndChild();

		drawPropertiesPanel();

		ImGui::EndTabItem();
	}

}