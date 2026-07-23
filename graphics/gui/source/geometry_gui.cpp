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

std::optional<Vec2> GeometryGUI::entityCenter(
	const SketchModel& sketch,
	SketchEntityType type,
	int entityID
) const {
	switch (type) {
	case SketchEntityType::Line: {
		const SketchLine* line = sketch.findLine(entityID);
		if (!line) {
			return std::nullopt;
		}
		const SketchPoint* p0 = sketch.findPoint(line->p0);
		const SketchPoint* p1 = sketch.findPoint(line->p1);
		if (!p0 || !p1) {
			return std::nullopt;
		}
		return Vec2{ (p0->pos.z + p1->pos.z) * 0.5, (p0->pos.r + p1->pos.r) * 0.5 };
	}
	case SketchEntityType::Rectangle: {
		const SketchRectangle* rect = sketch.findRectangle(entityID);
		if (!rect) {
			return std::nullopt;
		}
		return Vec2{ (rect->min.z + rect->max.z) * 0.5, (rect->min.r + rect->max.r) * 0.5 };
	}
	case SketchEntityType::Circle: {
		const SketchCircle* circle = sketch.findCircle(entityID);
		return circle ? std::optional<Vec2>(circle->center) : std::nullopt;
	}
	case SketchEntityType::Arc: {
		const SketchArc* arc = sketch.findArc(entityID);
		return arc ? std::optional<Vec2>(arc->center) : std::nullopt;
	}
	default:
		return std::nullopt;
	}
}

void GeometryGUI::drawMoveSection(std::optional<std::pair<SketchEntityType, int>> picked) {
	SketchModel& sketch = project.geometry.sketch;
	Geometry& geometry = project.geometry;

	// Figure out what we're moving. The box-select group (published by SketchView)
	// wins when it has anything in it, so a multi-entity selection is movable; a
	// single entity picked from the list is the fallback. Each source gets a
	// distinct sync token so the fields re-read the right center when it changes.
	bool isGroup = geometry.selectionCount > 0;

	Vec2 center{};
	long long token = 0;
	std::string subtitle;

	if (isGroup) {
		center = geometry.selectionCenter;
		// high bit tags "group"; revision changes when the selected set changes
		token = 0x4000000000000000LL ^ (long long)geometry.selectionRevision;
		subtitle = std::to_string(geometry.selectionCount) +
			(geometry.selectionCount == 1 ? " entity selected" : " entities selected");
	}
	else if (picked.has_value()) {
		std::optional<Vec2> c = entityCenter(sketch, picked->first, picked->second);
		if (!c) {
			moveSyncToken = 0;
			return;
		}
		center = *c;
		token = 0x2000000000000000LL |
			((long long)picked->first << 40) |
			(long long)(unsigned)picked->second;
		subtitle = "Moves the selection so its center is here.";
	}
	else {
		// nothing to move; forget the last sync so the next selection re-reads
		moveSyncToken = 0;
		return;
	}

	// Re-read the selection's current center into the fields whenever the
	// selection changes, so the numbers start from where it actually is (and we
	// don't clobber what the user is mid-typing on an unchanged selection).
	if (token != moveSyncToken) {
		moveTargetZ = center.z;
		moveTargetR = center.r;
		moveSyncToken = token;
	}

	ImGui::Spacing();
	sectionHeader(isGroup ? "Move Selection" : "Move");
	ImGui::TextDisabled("%s", subtitle.c_str());

	if (ImGui::BeginTable("MoveToTable", 3, UIFlags::TableSimpleFlags)) {
		ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 40.0f);
		ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Unit", ImGuiTableColumnFlags_WidthFixed, 40.0f);

		labelRow("z");
		inputDouble("##moveZ", moveTargetZ, project.lengthScale.index, Units::lengthUnits, "%.6g");
		unitLabel(Units::lengthUnits, project.lengthScale.index);

		labelRow("r");
		inputDouble("##moveR", moveTargetR, project.lengthScale.index, Units::lengthUnits, "%.6g");
		unitLabel(Units::lengthUnits, project.lengthScale.index);

		ImGui::EndTable();
	}

	if (actionButton("Move To Location")) {
		Vec2 delta{ moveTargetZ - center.z, moveTargetR - center.r };
		if (isGroup) {
			geometry.requestGroupMove(delta);
		}
		else {
			geometry.requestMove(picked->first, picked->second, delta);
		}
	}
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

		// Move-to-typed-location fields. Handles the box-select group (any number
		// of entities, selected on the canvas) or a single entity picked from the
		// list; draws nothing when neither exists.
		drawMoveSection(picked);
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

	ImGuiTabItemFlags tabFlags = ImGuiTabItemFlags_None;
	if (project.tabSwitchRequested && project.requestedTab == ViewTab::TAB_GEOMETRY) {
		tabFlags = ImGuiTabItemFlags_SetSelected;
	}

	if (ImGui::BeginTabItem("Geometry", nullptr, tabFlags)) {
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
