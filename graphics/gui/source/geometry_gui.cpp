#include "geometry_gui.h"

#include <cfloat>
#include <cstdio>
#include <string>

#include "project.h"
#include "app_struct.h"

GeometryGUI::GeometryGUI(Project& project, AppAssets& assets) :
	project(project),
	assets(assets) {

}

std::optional<std::pair<SketchEntityType, int>> GeometryGUI::selectedEntity(
	const SketchModel& sketch
) const {
	for (const SketchLine& line : sketch.lines) {
		if (line.selected) {
			return std::pair{ SketchEntityType::Line, line.id };
		}
	}
	for (const SketchRectangle& rect : sketch.rectangles) {
		if (rect.selected) {
			return std::pair{ SketchEntityType::Rectangle, rect.id };
		}
	}
	for (const SketchCircle& circle : sketch.circles) {
		if (circle.selected) {
			return std::pair{ SketchEntityType::Circle, circle.id };
		}
	}
	for (const SketchArc& arc : sketch.arcs) {
		if (arc.selected) {
			return std::pair{ SketchEntityType::Arc, arc.id };
		}
	}

	return std::nullopt;
}

void GeometryGUI::drawEntityGroup(
	SketchModel& sketch,
	const char* label,
	SketchEntityType type,
	int count
) {
	if (count <= 0) {
		return;
	}

	// "Lines (3)" — the count stays visible when the group is collapsed
	std::string header = std::string(label) + " (" + std::to_string(count) + ")";
	if (!ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}

	auto row = [&](int id, bool selected, double a, double b, const char* fmt) {
		// ids restart per type, so scope the ImGui id by type to keep rows unique
		ImGui::PushID(static_cast<int>(type));
		ImGui::PushID(id);

		char text[96];
		snprintf(text, sizeof(text), fmt, id, a, b);

		if (ImGui::Selectable(text, selected)) {
			// selection lives on the model, so the sketch view highlights it
			sketch.selectOnly(type, id);
		}

		ImGui::PopID();
		ImGui::PopID();
	};

	switch (type) {
	case SketchEntityType::Line:
		for (const SketchLine& line : sketch.lines) {
			row(line.id, line.selected, sketch.getLineLength(line.id), 0.0,
				"Line %d  —  length %.4g");
		}
		break;
	case SketchEntityType::Rectangle:
		for (const SketchRectangle& rect : sketch.rectangles) {
			row(rect.id, rect.selected,
				sketch.getRectangleWidth(rect.id),
				sketch.getRectangleHeight(rect.id),
				"Rectangle %d  —  %.4g x %.4g");
		}
		break;
	case SketchEntityType::Circle:
		for (const SketchCircle& circle : sketch.circles) {
			row(circle.id, circle.selected, circle.radius, 0.0,
				"Circle %d  —  r %.4g");
		}
		break;
	case SketchEntityType::Arc:
		for (const SketchArc& arc : sketch.arcs) {
			row(arc.id, arc.selected, arc.radius, 0.0,
				"Arc %d  —  r %.4g");
		}
		break;
	default:
		break;
	}
}

void GeometryGUI::drawPropertiesPanel() {
	SketchModel& sketch = project.geometry.sketch;

	ImGui::Begin("Overview");
	if (selectedItem == "Geometry") {
		sectionHeader("Geometry");

		const int lineCount = static_cast<int>(sketch.lines.size());
		const int rectCount = static_cast<int>(sketch.rectangles.size());
		const int circleCount = static_cast<int>(sketch.circles.size());
		const int arcCount = static_cast<int>(sketch.arcs.size());

		if (lineCount + rectCount + circleCount + arcCount == 0) {
			ImGui::TextDisabled("Nothing sketched yet.");
			ImGui::End();
			return;
		}

		drawEntityGroup(sketch, "Lines", SketchEntityType::Line, lineCount);
		drawEntityGroup(sketch, "Rectangles", SketchEntityType::Rectangle, rectCount);
		drawEntityGroup(sketch, "Circles", SketchEntityType::Circle, circleCount);
		drawEntityGroup(sketch, "Arcs", SketchEntityType::Arc, arcCount);

		ImGui::Spacing();

		// SketchView performs the delete so it can record undo; see
		// Geometry::requestDelete. Disabled until something is picked.
		std::optional<std::pair<SketchEntityType, int>> picked = selectedEntity(sketch);

		ImGui::BeginDisabled(!picked.has_value());
		if (ImGui::Button("Delete Selected", ImVec2(-FLT_MIN, 0.0f))) {
			project.geometry.requestDelete(picked->first, picked->second);
		}
		ImGui::EndDisabled();
	}
	else if (selectedItem == "General") {
		sectionHeader("Statistics");

		if (beginPropertyTable("GeometryStatisticsTable")) {
			drawTableProperty("Lines", std::to_string(sketch.lines.size()).c_str());
			drawTableProperty("Rectangles", std::to_string(sketch.rectangles.size()).c_str());
			drawTableProperty("Circles", std::to_string(sketch.circles.size()).c_str());
			drawTableProperty("Arcs", std::to_string(sketch.arcs.size()).c_str());

			ImGui::EndTable();
		}
	}

	ImGui::End();

}

void GeometryGUI::draw() {

	if (ImGui::BeginTabItem("Geometry")) {
		project.currentTab = ViewTab::TAB_GEOMETRY;

		ImGui::BeginChild("SetupTree", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing()), true);

		// childless leaves; clicking one sets selectedItem and the overview
		// switches panel (drawLeaf handles selection + the hover cursor)
		drawLeaf("General", &assets.icon("general"));
		drawLeaf("Geometry", &assets.icon("geometry"));

		ImGui::EndChild();

		drawPropertiesPanel();

		ImGui::EndTabItem();
	}
}
