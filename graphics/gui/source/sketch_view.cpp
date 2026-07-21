#include "sketch_view.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "project.h"
#include "gui.h"

#include "geometry.h"
#include "math_func.h"
#include "keyboard_manager.h"
#include "flag_manager.h"
#include "unit_manager.h"

using namespace sketchmath;
using namespace Shortcuts;
using namespace Snapping;
using namespace UIDockFlags;

namespace {
	Vec2 translatePoint(Vec2 point, Vec2 delta) {
		return Vec2{ point.z + delta.z, point.r + delta.r };
	}

	TrimPreviewResult translatedPreview(
		TrimPreviewResult preview,
		Vec2 delta
	) {
		preview.a = translatePoint(preview.a, delta);
		preview.b = translatePoint(preview.b, delta);
		preview.center = translatePoint(preview.center, delta);
		return preview;
	}

	std::string shortcutText(const char* label, ImGuiKeyChord shortcut) {
		return std::string(label) + " (" + ImGui::GetKeyChordName(shortcut) + ")";
	}
}

SketchView::SketchView(Project& project, GUI& gui) :
	geometry(project.geometry),
	gui(gui),
	assets(gui.appConfig.assets),
	BaseSurfaceViewer("graphics/shaders/sketch.vert", "graphics/shaders/sketch.frag") {
	frameBuffer.create2DBuffer(500, 500, GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE);
}

void SketchView::recordSketchUndoState(const SketchModel& beforeChange) {
	constexpr int maxUndoStates = 100;

	undoSketchStates.push_back(beforeChange);
	if ((int)undoSketchStates.size() > maxUndoStates) {
		undoSketchStates.erase(undoSketchStates.begin());
	}

	redoSketchStates.clear();
}

void SketchView::consumePendingDelete() {
	if (geometry.pendingDeleteID < 0) {
		return;
	}

	SketchEntityType type = geometry.pendingDeleteType;
	int entityID = geometry.pendingDeleteID;
	geometry.pendingDeleteID = -1;

	SketchModel beforeDelete = geometry.sketch;
	if (geometry.sketch.removeEntity(type, entityID)) {
		recordSketchUndoState(beforeDelete);
	}
}

void SketchView::consumePendingMove() {
	if (geometry.pendingMoveID < 0) {
		return;
	}

	SketchEntityType type = geometry.pendingMoveType;
	int entityID = geometry.pendingMoveID;
	Vec2 delta = geometry.pendingMoveDelta;
	geometry.pendingMoveID = -1;

	// a zero move is a no-op; don't push an undo state for it
	if (std::abs(delta.z) <= 1e-12 && std::abs(delta.r) <= 1e-12) {
		return;
	}

	SketchModel beforeMove = geometry.sketch;
	if (geometry.sketch.moveEntity(type, entityID, delta)) {
		recordSketchUndoState(beforeMove);
	}
}

void SketchView::consumePendingGroupMove() {
	if (!geometry.pendingGroupMove) {
		return;
	}

	Vec2 delta = geometry.pendingGroupMoveDelta;
	geometry.pendingGroupMove = false;

	// a zero move is a no-op; don't push an undo state for it
	if (std::abs(delta.z) <= 1e-12 && std::abs(delta.r) <= 1e-12) {
		return;
	}

	// moveSelectedTrimSegments translates the whole box-select group (deleting and
	// re-adding each entity), so it handles lines/rects/circles/arcs uniformly.
	SketchModel beforeMove = geometry.sketch;
	if (moveSelectedTrimSegments(delta)) {
		recordSketchUndoState(beforeMove);
	}
}

void SketchView::restoreSketchState(const SketchModel& state) {
	SketchTool activeTool = geometry.sketch.activeTool;
	geometry.sketch = state;
	geometry.sketch.activeTool = activeTool;

	for (SketchPoint& point : geometry.sketch.points) {
		point.selected = false;
	}
	for (SketchLine& line : geometry.sketch.lines) {
		line.selected = false;
	}
	for (SketchCircle& circle : geometry.sketch.circles) {
		circle.selected = false;
	}
	for (SketchArc& arc : geometry.sketch.arcs) {
		arc.selected = false;
	}
	for (SketchRectangle& rect : geometry.sketch.rectangles) {
		rect.selected = false;
	}
	for (SketchDimension& dimension : geometry.sketch.dimensions) {
		dimension.selected = false;
	}

	selectedTrimSegments.clear();
	movingTrimSegments.clear();
	isSelecting = false;
	isMovingSelection = false;
	isDrawingEntity = false;
	editingDimensionID = -1;
}

bool SketchView::undoSketchEdit() {
	if (undoSketchStates.empty()) {
		return false;
	}

	redoSketchStates.push_back(geometry.sketch);
	SketchModel previous = undoSketchStates.back();
	undoSketchStates.pop_back();
	restoreSketchState(previous);
	return true;
}

bool SketchView::redoSketchEdit() {
	if (redoSketchStates.empty()) {
		return false;
	}

	undoSketchStates.push_back(geometry.sketch);
	SketchModel next = redoSketchStates.back();
	redoSketchStates.pop_back();
	restoreSketchState(next);
	return true;
}

void SketchView::clearToolToggles() {
	toggleEraser = false;
	toggleRuler = false;
	toggleTrim = false;
	toggleMoveTo = false;
	toggleDrawLine = false;
	toggleDrawRect = false;
	toggleDrawCircle = false;
	isDrawingEntity = false;
	isSelecting = false;
	isMovingSelection = false;
	hasMoveToBase = false;
	movingTrimSegments.clear();
}

void SketchView::setActiveSketchTool(SketchTool tool) {
	clearToolToggles();
	geometry.sketch.activeTool = tool;

	switch (tool) {
	case SketchTool::MoveTo:
		toggleMoveTo = true;
		break;
	case SketchTool::Erase:
		toggleEraser = true;
		break;
	case SketchTool::Dimension:
		toggleRuler = true;
		break;
	case SketchTool::Line:
		toggleDrawLine = true;
		break;
	case SketchTool::Rectangle:
		toggleDrawRect = true;
		break;
	case SketchTool::Circle:
		toggleDrawCircle = true;
		break;
	case SketchTool::Trim:
		toggleTrim = true;
		break;
	default:
		break;
	}
}

std::optional<SnapResult> SketchView::findSnap(ImVec2 mouse) {
	constexpr float snapRadiusPx = 10.0f;

	Vec2 mouseWorld = camera.screenToWorld(mouse);

	std::optional<SnapResult> best;

	auto tryCandidate = [&](SnapType type, Vec2 world, int entityID) {
		ImVec2 screen = camera.worldToScreen(world);

		float dx = screen.x - mouse.x;
		float dy = screen.y - mouse.y;
		float distPx = std::sqrt(dx * dx + dy * dy);

		if (distPx > snapRadiusPx) return;

		if (!best || distPx < best->distancePx) {
			best = SnapResult{
				type,
				world,
				screen,
				distPx,
				entityID
			};
		}
	};

	// origin (0,0) as a snappable vertex -- routed through tryCandidate so a
	// strictly closer sketch vertex or point still wins, instead of the origin
	// always taking priority via an early return. Grouped with the points rather
	// than the axes so it survives the default settings, where axis snapping is
	// off: it is the one reference an axisymmetric sketch is always drawn against.
	if (snapToPoints) {
		tryCandidate(SnapType::Vertex, Vec2{ 0.0, 0.0 }, -102);
	}

	// The axis lines (r = 0 and z = 0) are off by default: they span the whole
	// canvas, so with them on anything drawn near an axis gets pulled onto it.
	// Only the nearest point on each is offered, so they behave like any other
	// edge candidate rather than as a band across the view.
	if (snapToAxis) {
		tryCandidate(SnapType::Line, Vec2{ mouseWorld.z, 0.0 }, -104);	// r = 0
		tryCandidate(SnapType::Line, Vec2{ 0.0, mouseWorld.r }, -105);	// z = 0
	}

	// snap to vertices
	if (snapToPoints) {
		for (const SketchPoint& point : geometry.sketch.points) {
			tryCandidate(SnapType::Vertex, point.pos, point.id);
		}
	}

	// Everything below is drawn sketch geometry -- the edges and the vertices they
	// imply. Line endpoints are not among them: those live in sketch.points above,
	// so turning this off keeps endpoint snapping and drops mid-edge snapping,
	// which is the distinction the two checkboxes are meant to draw.
	if (!snapToSketch) {
		return best;
	}

	// snap to lines / edges
	for (const SketchLine& line : geometry.sketch.lines) {
		const SketchPoint* p0 = geometry.sketch.findPoint(line.p0);
		const SketchPoint* p1 = geometry.sketch.findPoint(line.p1);
		if (!p0 || !p1) {
			continue;
		}

		Vec2 closest = closestPointOnSegment(mouseWorld, p0->pos, p1->pos);
		tryCandidate(SnapType::Line, closest, line.id);
	}

	for (const SketchRectangle& rect : geometry.sketch.rectangles) {
		Vec2 corners[4] = {
			Vec2{ rect.min.z, rect.min.r },
			Vec2{ rect.max.z, rect.min.r },
			Vec2{ rect.max.z, rect.max.r },
			Vec2{ rect.min.z, rect.max.r }
		};

		for (int edge = 0; edge < 4; edge++) {
			Vec2 a = corners[edge];
			Vec2 b = corners[(edge + 1) % 4];

			tryCandidate(SnapType::Vertex, a, rect.id);
			tryCandidate(SnapType::Line, closestPointOnSegment(mouseWorld, a, b), rect.id);
		}

		// center
		tryCandidate(
			SnapType::Vertex,
			Vec2{
				0.5 * (rect.min.z + rect.max.z),
				0.5 * (rect.min.r + rect.max.r)
			},
			rect.id
		);
	}

	// snap to circle edge
	for (const SketchCircle& circle : geometry.sketch.circles) {
		double dz = mouseWorld.z - circle.center.z;
		double dr = mouseWorld.r - circle.center.r;
		double len = std::sqrt(dz * dz + dr * dr);

		if (len > 1e-30) {
			Vec2 closest{
				circle.center.z + circle.radius * dz / len,
				circle.center.r + circle.radius * dr / len
			};

			tryCandidate(SnapType::Circle, closest, circle.id);
		}

		// optional: snap to circle center too
		tryCandidate(SnapType::Vertex, circle.center, circle.id);
	}

	for (const SketchArc& arc : geometry.sketch.arcs) {
		double angle = angleOfPoint(arc.center, mouseWorld);
		if (angleOnArc(angle, arc)) {
			tryCandidate(
				SnapType::Circle,
				pointOnCircle(arc.center, arc.radius, angle),
				arc.id
			);
		}

		tryCandidate(SnapType::Vertex, pointOnCircle(arc.center, arc.radius, arc.startAngle), arc.id);
		tryCandidate(SnapType::Vertex, pointOnCircle(arc.center, arc.radius, arc.endAngle), arc.id);
		tryCandidate(SnapType::Vertex, arc.center, arc.id);
	}

	return best;
}

std::optional<DimensionPickResult> SketchView::findDimensionTarget(ImVec2 mouse) {
	constexpr float pickRadiusPx = 12.0f;

	Vec2 mouseWorld = camera.screenToWorld(mouse);
	std::optional<DimensionPickResult> best;

	auto tryCandidate = [&](
		SketchDimensionType type,
		int entityID,
		Vec2 closestWorld
	) {
		float distPx = pixelDistance(camera.worldToScreen(closestWorld), mouse);

		if (distPx > pickRadiusPx) {
			return;
		}

		if (!best || distPx < best->distancePx) {
			best = DimensionPickResult{
				type,
				entityID,
				mouseWorld,
				distPx
			};
		}
	};

	for (const SketchLine& line : geometry.sketch.lines) {
		const SketchPoint* p0 = geometry.sketch.findPoint(line.p0);
		const SketchPoint* p1 = geometry.sketch.findPoint(line.p1);

		if (!p0 || !p1) {
			continue;
		}

		Vec2 closest = closestPointOnSegment(mouseWorld, p0->pos, p1->pos);
		tryCandidate(SketchDimensionType::LineLength, line.id, closest);
	}

	for (const SketchCircle& circle : geometry.sketch.circles) {
		double dz = mouseWorld.z - circle.center.z;
		double dr = mouseWorld.r - circle.center.r;
		double len = std::sqrt(dz * dz + dr * dr);

		if (len <= 1e-30) {
			continue;
		}

		Vec2 closest{
			circle.center.z + circle.radius * dz / len,
			circle.center.r + circle.radius * dr / len
		};

		tryCandidate(SketchDimensionType::CircleRadius, circle.id, closest);
	}

	for (const SketchRectangle& rect : geometry.sketch.rectangles) {
		Vec2 a{ rect.min.z, rect.min.r };
		Vec2 b{ rect.max.z, rect.min.r };
		Vec2 c{ rect.max.z, rect.max.r };
		Vec2 d{ rect.min.z, rect.max.r };

		tryCandidate(
			SketchDimensionType::RectangleWidth,
			rect.id,
			closestPointOnSegment(mouseWorld, a, b)
		);

		tryCandidate(
			SketchDimensionType::RectangleWidth,
			rect.id,
			closestPointOnSegment(mouseWorld, d, c)
		);

		tryCandidate(
			SketchDimensionType::RectangleHeight,
			rect.id,
			closestPointOnSegment(mouseWorld, a, d)
		);

		tryCandidate(
			SketchDimensionType::RectangleHeight,
			rect.id,
			closestPointOnSegment(mouseWorld, b, c)
		);
	}

	return best;
}

std::string SketchView::getDimensionLabel(
	const SketchDimension& dimension
) const {
	double value = geometry.sketch.getDimensionValue(dimension);

	const UnitOption& unit = Units::lengthUnits[gui.project.lengthScale.index];
	double displayValue = fromBaseValue(value, unit);

	char buffer[64] = {};

	switch (dimension.type) {
	case SketchDimensionType::LineLength:
		std::snprintf(buffer, sizeof(buffer), "%.6g %s", displayValue, unit.name);
		break;
	case SketchDimensionType::CircleRadius:
		std::snprintf(buffer, sizeof(buffer), "R %.6g %s", displayValue, unit.name);
		break;
	case SketchDimensionType::RectangleWidth:
		std::snprintf(buffer, sizeof(buffer), "W %.6g %s", displayValue, unit.name);
		break;
	case SketchDimensionType::RectangleHeight:
		std::snprintf(buffer, sizeof(buffer), "H %.6g %s", displayValue, unit.name);
		break;
	}

	return buffer;
}

std::optional<int> SketchView::findDimensionLabel(ImVec2 mouse) {
	for (const SketchDimension& dimension : geometry.sketch.dimensions) {
		std::string label = getDimensionLabel(dimension);
		ImVec2 labelPos = camera.worldToScreen(dimension.labelPos);
		ImVec2 textSize = ImGui::CalcTextSize(label.c_str());

		ImVec2 rectMin(labelPos.x - 5.0f, labelPos.y - 3.0f);
		ImVec2 rectMax(
			labelPos.x + textSize.x + 5.0f,
			labelPos.y + textSize.y + 3.0f
		);

		bool inside =
			mouse.x >= rectMin.x && mouse.x <= rectMax.x &&
			mouse.y >= rectMin.y && mouse.y <= rectMax.y;

		if (inside) {
			return dimension.id;
		}
	}

	return std::nullopt;
}

void SketchView::openDimensionEditor(int dimensionID) {
	editingDimensionID = dimensionID;
	dimensionEditValue = geometry.sketch.getDimensionValue(dimensionID);
	ImGui::OpenPopup("Edit Dimension");
}

void SketchView::copyActiveSurfaceToClipboard() {
	GLint oldFBO, oldViewport[4];
	ImVec2 oldDisplaySize, oldFramebufferSize;
	offScreenFBO.create2DBuffer(pendingCopyWidth, pendingCopyHeight, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
	offScreenFBO.beginOffScreenImGuiRender(oldFBO, oldViewport, oldDisplaySize, oldFramebufferSize);

	// build imgui draw commands
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::Begin("##ExportWindow", nullptr, UIFlags::TemporaryWindowFlags);

	ImVec2 exportSize((float)pendingCopyWidth, (float)pendingCopyHeight);
	ImGui::Image((ImTextureID)(intptr_t)frameBuffer.getTextureID(), exportSize, ImVec2(0.0, 1.0f), ImVec2(1.0f, 0.0f));

	canvasRect = makePaddedRect(ImGui::GetItemRectMin(), exportSize);

	camera.setDimensions(
		canvasRect.size.x,
		canvasRect.size.y,
		canvasRect.min
	);

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawSurface(canvasRect);
	drawCanvas(drawList, canvasRect, 0.0f);

	drawList->PushClipRect(canvasRect.min, canvasRect.max, true);
	drawGrid(drawList);
	drawAxes(drawList);
	drawSketchEntities(drawList);
	drawTrimPreview(drawList);
	drawDimensions(drawList);
	drawPendingSketchEntity(drawList);
	drawTemporarySketch(drawList);
	drawSnapping(drawList);
	drawDimensionEditor();
	drawList->PopClipRect();

	ImGui::End();
	ImGui::PopStyleVar();

	offScreenFBO.endOffScreenImGuiRender(oldFBO, oldViewport, oldDisplaySize, oldFramebufferSize);
}

void SketchView::drawToolBar() {

	// CFD-style ribbon: tools grouped by workflow into named sections
	// (clipboard | home | create | edit | view). The drawing and editing tools trade
	// their captions for two compact rows — their section name says what they are,
	// and the tooltip holds the fuller description and the key chord.
	beginToolbar();

	// --- clipboard ---
	beginSection();

	// Paste is the tall captioned button and the other two stack beside it: it is
	// the group's primary action, and the shape of the group says so.
	std::string pasteText = shortcutText("Paste Geometry", pasteShortcut);
	ImGui::BeginDisabled(clipboardSegments.empty());
	if (addImageButton("Paste", "Paste", pasteText.c_str(), assets.icon("paste"))) {
		// the cursor is up here on the button, so there is no canvas point to land
		// on — unlike the chord, this always pastes off the original
		pasteClipboardAt(offsetPasteTarget());
	}
	ImGui::EndDisabled();
	ImGui::SameLine();

	// a group so the second row lands under the first instead of back at the
	// band's left edge
	ImGui::BeginGroup();

	// gated exactly as the copy chord is, so the button and Ctrl+C never disagree
	// about whether there is anything to copy
	std::string copyText = shortcutText("Copy Selected Geometry", copyShortcut);
	ImGui::BeginDisabled(
		geometry.sketch.activeTool != SketchTool::Select ||
		selectedTrimSegments.empty()
	);
	if (addImageButtonRow("CopyGeometry", "Copy", copyText.c_str(), assets.icon("copy"))) {
		copySelectedTrimSegments();
	}
	ImGui::EndDisabled();

	if (addImageButtonRow(
		"Screenshot",
		"Screenshot",
		"Copy the whole sketch view to the clipboard as an image",
		assets.icon("screenshot")
	) || consoleCopy) {
		pendingCopyWidth = frameBuffer.width;
		pendingCopyHeight = frameBuffer.height;
		pendingCopy = true;
		consoleCopy = false;
	}
	ImGui::EndGroup();

	endSection("Clipboard");

	// --- home ---
	beginSection();
	std::string resetViewText = shortcutText("Reset View", resetViewShortcut);
	if (addImageButton("Reset", "Home", resetViewText.c_str(), assets.icon("house"))) {
		resetView();
	}
	endSection("Home");

	// --- create ---
	beginSection();
	std::string lineText = shortcutText("Draw Straight Line", lineToolShortcut);
	if (addImageButtonToggle("DrawLine", nullptr, lineText.c_str(), assets.icon("draw-line"), toggleDrawLine, smallIconSize())) {
		setActiveSketchTool(toggleDrawLine ? SketchTool::Line : SketchTool::Select);
	}
	ImGui::SameLine();
	std::string rectangleText = shortcutText("Draw Rectangle", rectangleToolShortcut);
	if (addImageButtonToggle("DrawRect", nullptr, rectangleText.c_str(), assets.icon("draw-rectangle"), toggleDrawRect, smallIconSize())) {
		setActiveSketchTool(toggleDrawRect ? SketchTool::Rectangle : SketchTool::Select);
	}
	std::string circleText = shortcutText("Draw Circle", circleToolShortcut);
	if (addImageButtonToggle("DrawCircle", nullptr, circleText.c_str(), assets.icon("draw-circle"), toggleDrawCircle, smallIconSize())) {
		setActiveSketchTool(toggleDrawCircle ? SketchTool::Circle : SketchTool::Select);
	}
	endSection("Create");

	// --- edit ---
	beginSection();
	std::string trimText = shortcutText("Trim", trimToolShortcut);
	if (addImageButtonToggle("Trim", nullptr, trimText.c_str(), assets.icon("trim"), toggleTrim, smallIconSize())) {
		setActiveSketchTool(toggleTrim ? SketchTool::Trim : SketchTool::Select);
	}
	ImGui::SameLine();
	std::string eraseText = shortcutText("Erase", eraseToolShortcut);
	if (addImageButtonToggle("Erase", nullptr, eraseText.c_str(), assets.icon("eraser"), toggleEraser, smallIconSize())) {
		setActiveSketchTool(toggleEraser ? SketchTool::Erase : SketchTool::Select);
	}
	std::string rulerText = shortcutText("Ruler", rulerToolShortcut);
	if (addImageButtonToggle("Ruler", nullptr, rulerText.c_str(), assets.icon("ruler"), toggleRuler, smallIconSize())) {
		setActiveSketchTool(toggleRuler ? SketchTool::Dimension : SketchTool::Select);
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(selectedTrimSegments.empty());
	if (addImageButtonToggle(
		"MoveTo",
		nullptr,
		"Move selected geometry: click a base point on the selection, then click its destination",
		assets.icon("move_to"),
		toggleMoveTo,
		smallIconSize()
	)) {
		setActiveSketchTool(toggleMoveTo ? SketchTool::MoveTo : SketchTool::Select);
	}
	ImGui::EndDisabled();
	endSection("Edit");

	// --- view ---
	beginSection();
	// no keyboard shortcut is wired for the grid toggle, so the tooltip carries
	// no key chord (it previously showed the Circle tool's shortcut by mistake)
	addImageButtonToggle("DisplayGrid", "Grid", "Display Grid", assets.icon("grid"), toggleGrid);
	endSection("View");

	endToolbar();
}

std::optional<SnapResult> SketchView::resolveSnap(ImVec2 mouse) {
	if (!toggleSnapping) {
		return std::nullopt;
	}

	std::optional<SnapResult> snap = findSnap(mouse);

	// when the grid is shown, prioritize grid vertices over sketch edges: a grid
	// vertex beats any edge (Line/Circle) snap, and also beats a sketch vertex
	// only when it is at least as close. Sketch vertices/points otherwise win.
	// Still gated on toggleGrid as well as the setting: the snap uses the spacing
	// that is being drawn, so snapping to a grid nobody can see reads as a bug.
	if (snapToGrid && toggleGrid && gridWorldStep() > 0.0) {
		Vec2 gridWorld = snapToGridVertex(camera.screenToWorld(mouse));
		ImVec2 gridScreen = camera.worldToScreen(gridWorld);
		float gridDistPx = pixelDistance(gridScreen, mouse);

		bool snapIsVertex = snap && snap->type == SnapType::Vertex;
		bool gridWins = !snapIsVertex || gridDistPx <= snap->distancePx;

		if (gridWins) {
			snap = SnapResult{
				SnapType::Vertex,
				gridWorld,
				gridScreen,
				gridDistPx,
				-103 // sentinel id for a grid vertex
			};
		}
	}

	return snap;
}

Vec2 SketchView::getSnappedWorld(ImVec2 mouse) {
	if (auto snap = resolveSnap(mouse)) {
		return snap->world;
	}

	return camera.screenToWorld(mouse);
}

void SketchView::setInitLeftMouse() {
	pendingStartWorld = getSnappedWorld(currentMousePos);
	initLeftMouse = camera.worldToScreen(pendingStartWorld);
}

bool SketchView::hoveredSelectedTrimSegment(ImVec2 mouse) {
	constexpr float movePickRadiusPx = 8.0f;
	Vec2 mouseWorld = camera.screenToWorld(mouse);

	for (const TrimPreviewResult& segment : selectedTrimSegments) {
		switch (segment.geometry) {
		case TrimPreviewGeometry::Line: {
			Vec2 closest = closestPointOnSegment(mouseWorld, segment.a, segment.b);
			if (pixelDistance(camera.worldToScreen(closest), mouse) <=
				movePickRadiusPx) {
				return true;
			}
			break;
		}
		case TrimPreviewGeometry::Circle: {
			double radialDistance =
				std::abs(distance(mouseWorld, segment.center) - segment.radius);
			if (camera.worldLengthToScreen(radialDistance) <= movePickRadiusPx) {
				return true;
			}
			break;
		}
		case TrimPreviewGeometry::Arc: {
			SketchArc arc{
				-1,
				segment.center,
				segment.radius,
				segment.startAngle,
				segment.endAngle
			};
			double angle = angleOfPoint(segment.center, mouseWorld);
			if (!angleOnArc(angle, arc)) {
				break;
			}

			double radialDistance =
				std::abs(distance(mouseWorld, segment.center) - segment.radius);
			if (camera.worldLengthToScreen(radialDistance) <= movePickRadiusPx) {
				return true;
			}
			break;
		}
		default:
			break;
		}
	}

	return false;
}

std::optional<Vec2> SketchView::findSelectedMovePoint(ImVec2 mouse) {
	constexpr float pickRadiusPx = 10.0f;
	Vec2 mouseWorld = camera.screenToWorld(mouse);
	std::optional<Vec2> best;
	float bestDistancePx = pickRadiusPx;

	auto tryCandidate = [&](Vec2 candidate) {
		float distancePx = pixelDistance(camera.worldToScreen(candidate), mouse);
		if (distancePx <= bestDistancePx) {
			best = candidate;
			bestDistancePx = distancePx;
		}
	};

	for (const TrimPreviewResult& segment : selectedTrimSegments) {
		switch (segment.geometry) {
		case TrimPreviewGeometry::Line:
			tryCandidate(closestPointOnSegment(mouseWorld, segment.a, segment.b));
			break;
		case TrimPreviewGeometry::Circle: {
			double radialDistance = distance(segment.center, mouseWorld);
			if (radialDistance <= sketchEpsilon) {
				tryCandidate(pointOnCircle(segment.center, segment.radius, 0.0));
			}
			else {
				double scale = segment.radius / radialDistance;
				tryCandidate(Vec2{
					segment.center.z + (mouseWorld.z - segment.center.z) * scale,
					segment.center.r + (mouseWorld.r - segment.center.r) * scale
				});
			}
			break;
		}
		case TrimPreviewGeometry::Arc: {
			SketchArc arc{
				-1,
				segment.center,
				segment.radius,
				segment.startAngle,
				segment.endAngle
			};
			double angle = angleOfPoint(segment.center, mouseWorld);
			if (angleOnArc(angle, arc)) {
				tryCandidate(pointOnCircle(segment.center, segment.radius, angle));
			}
			else {
				tryCandidate(segment.a);
				tryCandidate(segment.b);
			}
			break;
		}
		default:
			break;
		}
	}

	return best;
}

void SketchView::handleSelect() {

	if (!(geometry.sketch.activeTool == SketchTool::Select)) return;

	// a dimension-label drag owns the left button; don't start a selection
	if (draggingDimensionID >= 0) return;

	ImGuiIO& io = ImGui::GetIO();

	auto clearSelection = [&]() {
		selectedTrimSegments.clear();

		for (SketchPoint& point : geometry.sketch.points) {
			point.selected = false;
		}
		for (SketchLine& line : geometry.sketch.lines) {
			line.selected = false;
		}
		for (SketchCircle& circle : geometry.sketch.circles) {
			circle.selected = false;
		}
		for (SketchArc& arc : geometry.sketch.arcs) {
			arc.selected = false;
		}
		for (SketchRectangle& rect : geometry.sketch.rectangles) {
			rect.selected = false;
		}
	};

	if (isMovingSelection) {
		if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
			Vec2 delta = subtract(pendingCurrentWorld, moveStartWorld);
			SketchModel beforeMove = geometry.sketch;
			if (moveSelectedTrimSegments(delta)) {
				recordSketchUndoState(beforeMove);
			}
			isMovingSelection = false;
			movingTrimSegments.clear();
		}

		return;
	}

	if (ImGui::IsItemHovered()) {
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
			setInitLeftMouse();
			if (hoveredSelectedTrimSegment(currentMousePos)) {
				isMovingSelection = true;
				isSelecting = false;
				moveStartWorld = pendingStartWorld;
				movingTrimSegments = selectedTrimSegments;
				return;
			}
		}

		if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
			isSelecting = true;
		}
	}

	if (!isSelecting) {
		if (ImGui::IsItemHovered() &&
			ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
			if (!io.KeyCtrl) {
				clearSelection();
			}

			if (auto segment = findTrimPreview(currentMousePos)) {
				selectedTrimSegments.push_back(*segment);
			}
		}

		return;
	}

	if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
		SketchBound selection = makeBound(pendingStartWorld, pendingCurrentWorld);

		if (!io.KeyCtrl) {
			clearSelection();
		}

		std::vector<TrimPreviewResult> selectedSegments =
			findTrimPreviewsInRegion(selection);
		selectedTrimSegments.insert(
			selectedTrimSegments.end(),
			selectedSegments.begin(),
			selectedSegments.end()
		);

		isSelecting = false;
	}
}

void SketchView::handleMoveToTool() {
	if (geometry.sketch.activeTool != SketchTool::MoveTo) {
		return;
	}

	// The selection is made before activating this tool. If an undo or an
	// external edit removes it, leave the tool instead of accepting orphaned
	// base/destination clicks.
	if (selectedTrimSegments.empty()) {
		setActiveSketchTool(SketchTool::Select);
		return;
	}

	if (!ImGui::IsItemHovered() ||
		!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		return;
	}

	if (!hasMoveToBase) {
		// The base point belongs to the selected geometry. Resolve it to the exact
		// selected edge so a near click does not bake a pixel-sized offset into the
		// move.
		if (auto base = findSelectedMovePoint(currentMousePos)) {
			moveToBaseWorld = *base;
			hasMoveToBase = true;
		}
		return;
	}

	Vec2 delta = subtract(getSnappedWorld(currentMousePos), moveToBaseWorld);
	SketchModel beforeMove = geometry.sketch;
	if (moveSelectedTrimSegments(delta)) {
		recordSketchUndoState(beforeMove);
	}

	// Keep the tool active and the moved geometry selected so another two-point
	// move can be made immediately. Escape or the highlighted toolbar button exits.
	hasMoveToBase = false;
}

void SketchView::handleTrimTool() {
	if (geometry.sketch.activeTool != SketchTool::Trim) {
		return;
	}

	if (!ImGui::IsItemHovered()) {
		return;
	}

	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		SketchModel beforeTrim = geometry.sketch;
		if (trimSketchAtMouse(currentMousePos)) {
			recordSketchUndoState(beforeTrim);
		}
	}
}

void SketchView::handleErase() {
	if (geometry.sketch.activeTool != SketchTool::Erase) {
		return;
	}

	if (!ImGui::IsItemHovered()) {
		return;
	}

	// Erase on the initial press and continuously while dragging, so sweeping
	// the cursor across the sketch removes every entity it passes over, like a
	// real eraser.
	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
		ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
		SketchModel beforeErase = geometry.sketch;
		if (eraseEntityAtMouse(currentMousePos)) {
			recordSketchUndoState(beforeErase);
		}
	}
}

bool SketchView::handleShortcuts(ImGuiIO& io) {

	bool canUseShortcuts =
		!io.WantTextInput &&
		ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

	if (!canUseShortcuts) {
		return false;
	}

	auto clearInteraction = [&]() {
		isSelecting = false;
		isMovingSelection = false;
		hasMoveToBase = false;
		movingTrimSegments.clear();
		isDrawingEntity = false;
		draggingDimensionID = -1;
	};

	if (ImGui::Shortcut(undoShortcut)) {
		clearInteraction();
		undoSketchEdit();
		return true;
	}

	if (ImGui::Shortcut(redoShortcut)) {
		clearInteraction();
		redoSketchEdit();
		return true;
	}

	// Copy reads the Select tool's segment selection, so it stays gated on that
	// tool. Both chords test their preconditions before ImGui::Shortcut so the
	// chord is only claimed when it would actually do something.
	if (geometry.sketch.activeTool == SketchTool::Select &&
		!selectedTrimSegments.empty() &&
		ImGui::Shortcut(copyShortcut)) {
		copySelectedTrimSegments();
		return true;
	}

	// Paste works from any tool. Land the copy's lower-left corner on the cursor:
	// Ctrl is still down from the chord itself, so snapping is live and the corner
	// lands on a vertex or grid point when one is in range. With the cursor off the
	// canvas there is nothing to anchor to, so fall back to nudging it off the
	// original — the same landing spot the toolbar's Paste button always uses.
	if (!clipboardSegments.empty() &&
		ImGui::Shortcut(pasteShortcut)) {
		pasteClipboardAt(
			ImGui::IsItemHovered() ?
			getSnappedWorld(currentMousePos) :
			offsetPasteTarget()
		);

		return true;
	}

	if (ImGui::Shortcut(resetViewShortcut)) {
		clearInteraction();
		resetView();
		return true;
	}

	if (ImGui::Shortcut(selectToolShortcut)) {
		setActiveSketchTool(SketchTool::Select);
		return true;
	}

	if (ImGui::Shortcut(rulerToolShortcut)) {
		setActiveSketchTool(SketchTool::Dimension);
		return true;
	}

	if (ImGui::Shortcut(trimToolShortcut)) {
		setActiveSketchTool(SketchTool::Trim);
		return true;
	}

	if (ImGui::Shortcut(eraseToolShortcut)) {
		setActiveSketchTool(SketchTool::Erase);
		return true;
	}

	if (ImGui::Shortcut(lineToolShortcut)) {
		setActiveSketchTool(SketchTool::Line);
		return true;
	}

	if (ImGui::Shortcut(rectangleToolShortcut)) {
		setActiveSketchTool(SketchTool::Rectangle);
		return true;
	}

	if (ImGui::Shortcut(circleToolShortcut)) {
		setActiveSketchTool(SketchTool::Circle);
		return true;
	}

	bool deletePressed =
		ImGui::IsKeyPressed(ImGuiKey_Delete, false) ||
		ImGui::IsKeyPressed(ImGuiKey_Backspace, false);

	// delete the dimension label under the cursor, in Select ("no tool") or
	// Dimension mode, regardless of what else is selected
	bool canEditLabels =
		geometry.sketch.activeTool == SketchTool::Select ||
		geometry.sketch.activeTool == SketchTool::Dimension;
	if (deletePressed &&
		canEditLabels &&
		editingDimensionID < 0 &&
		!io.WantTextInput &&
		ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
		if (auto dimensionID = findDimensionLabel(currentMousePos)) {
			clearInteraction();
			SketchModel beforeDelete = geometry.sketch;
			if (geometry.sketch.removeDimension(*dimensionID)) {
				recordSketchUndoState(beforeDelete);
			}
			return true;
		}
	}

	if (geometry.sketch.activeTool == SketchTool::Select &&
		!selectedTrimSegments.empty() &&
		editingDimensionID < 0 &&
		!io.WantTextInput &&
		ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
		deletePressed) {
		clearInteraction();
		SketchModel beforeDelete = geometry.sketch;
		if (deleteSelectedTrimSegments()) {
			recordSketchUndoState(beforeDelete);
		}
		return true;
	}

	return false;
}

void SketchView::handleMouseAndKey() {
	ImGuiIO& io = ImGui::GetIO();
	bool usesDragDrawing =
		geometry.sketch.activeTool == SketchTool::Circle ||
		geometry.sketch.activeTool == SketchTool::Rectangle ||
		geometry.sketch.activeTool == SketchTool::Line;

	toggleSnapping = io.KeyCtrl;

	if (handleShortcuts(io)) {
		return;
	}

	if (ImGui::IsItemHovered()) {
		if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
			camera.calculatePan(io.MouseDelta.x, io.MouseDelta.y);
		}

		if (io.MouseWheel != 0.0f) {
			camera.calculateZoom(io.MouseWheel, currentMousePos);
		}

		if (usesDragDrawing && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
			setInitLeftMouse();
			isDrawingEntity = true;
		}
	}

	handleDimensionLabels();

	handleSelect();

	handleMoveToTool();

	handleDimensionTool();

	handleTrimTool();

	handleErase();

	handleOpenPopup();

	if (!isDrawingEntity) {
		return;
	}


	if (geometry.sketch.activeTool == SketchTool::Line &&
		ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
		Vec2 start = pendingStartWorld;
		Vec2 end = pendingCurrentWorld;

		double length = distance(start, end);

		if (length > 1e-12) {
			SketchModel beforeLine = geometry.sketch;
			geometry.sketch.addLine(start, end);
			recordSketchUndoState(beforeLine);
		}

		isDrawingEntity = false;
	}

	if (geometry.sketch.activeTool == SketchTool::Circle &&
		ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
		Vec2 center = pendingStartWorld;
		Vec2 edge = pendingCurrentWorld;
		double radius = distance(center, edge);

		if (radius > 1e-12) {
			SketchModel beforeCircle = geometry.sketch;
			geometry.sketch.addCircle(center, radius);
			recordSketchUndoState(beforeCircle);
		}

		isDrawingEntity = false;
	}

	if (geometry.sketch.activeTool == SketchTool::Rectangle &&
		ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
		Vec2 cornerA = pendingStartWorld;
		Vec2 cornerB = pendingCurrentWorld;

		if (distance(cornerA, cornerB) > 1e-12) {
			SketchModel beforeRectangle = geometry.sketch;
			geometry.sketch.addRectangle(cornerA, cornerB);
			recordSketchUndoState(beforeRectangle);
		}

		isDrawingEntity = false;
	}

	if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
		isDrawingEntity = false;
	}
}

void SketchView::handleDimensionLabels() {
	SketchTool tool = geometry.sketch.activeTool;

	// label drag/edit is available in Select ("no tool") and Dimension modes
	if (tool != SketchTool::Select && tool != SketchTool::Dimension) {
		return;
	}

	// a drag keeps running until release, even if the cursor leaves the item
	if (draggingDimensionID >= 0) {
		updateDimensionLabelDrag();
		return;
	}

	if (!ImGui::IsItemHovered()) {
		return;
	}

	if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		return;
	}

	auto dimensionID = findDimensionLabel(currentMousePos);
	if (!dimensionID) {
		return;
	}

	// double-click edits the value; a single press starts a drag (and, in the
	// Dimension tool, opens the editor if the press turns out to be a click)
	if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
		openDimensionEditor(*dimensionID);
	}
	else {
		beginDimensionLabelDrag(*dimensionID);
	}
}

void SketchView::handleDimensionTool() {
	if (geometry.sketch.activeTool != SketchTool::Dimension) {
		return;
	}

	// label drag/edit is handled by handleDimensionLabels(); if it grabbed a
	// label this frame, don't also add a new dimension
	if (draggingDimensionID >= 0) {
		return;
	}

	if (!ImGui::IsItemHovered()) {
		return;
	}

	if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		return;
	}

	// a press on a label was already consumed by handleDimensionLabels()
	if (findDimensionLabel(currentMousePos)) {
		return;
	}

	// otherwise, clicking an entity adds a new dimension to it
	if (auto target = findDimensionTarget(currentMousePos)) {
		SketchModel beforeDimension = geometry.sketch;
		int dimensionID = geometry.sketch.addDimension(
			target->type,
			target->entityID,
			target->labelPos
		);
		recordSketchUndoState(beforeDimension);

		openDimensionEditor(dimensionID);
	}
}

void SketchView::beginDimensionLabelDrag(int dimensionID) {
	const SketchDimension* dimension = geometry.sketch.findDimension(dimensionID);
	if (!dimension) {
		return;
	}

	draggingDimensionID = dimensionID;
	dimensionDragMoved = false;
	dimensionDragBefore = geometry.sketch;

	// remember the grab point so the label stays fixed under the cursor
	Vec2 mouseWorld = camera.screenToWorld(currentMousePos);
	dimensionDragOffset = subtract(dimension->labelPos, mouseWorld);
}

void SketchView::updateDimensionLabelDrag() {
	SketchDimension* dimension = geometry.sketch.findDimension(draggingDimensionID);

	// the dimension can vanish mid-drag (e.g. undo); just stop dragging
	if (!dimension) {
		draggingDimensionID = -1;
		return;
	}

	if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
		Vec2 mouseWorld = camera.screenToWorld(currentMousePos);
		dimension->labelPos = Vec2{
			mouseWorld.z + dimensionDragOffset.z,
			mouseWorld.r + dimensionDragOffset.r
		};
		dimensionDragMoved = true;
	}

	if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
		if (dimensionDragMoved) {
			recordSketchUndoState(dimensionDragBefore);
		}
		else if (geometry.sketch.activeTool == SketchTool::Dimension) {
			// a press that never moved is treated as a click: open the editor
			openDimensionEditor(draggingDimensionID);
		}

		draggingDimensionID = -1;
	}
}


void SketchView::drawSketchEntities(ImDrawList* drawList) {

	std::optional<TrimPreviewResult> hoveredSegment =
		geometry.sketch.activeTool == SketchTool::Select &&
		ImGui::IsItemHovered() ?
		findTrimPreview(currentMousePos) :
		std::nullopt;

	// In erase mode the whole entity under the cursor is highlighted, so the
	// user can see what a click (or drag) will remove.
	std::optional<SketchTrimTarget> eraseHover =
		geometry.sketch.activeTool == SketchTool::Erase &&
		ImGui::IsItemHovered() ?
		findTrimTarget(currentMousePos) :
		std::nullopt;

	auto isEraseHovered = [&](SketchEntityType type, int entityID) {
		return eraseHover &&
			eraseHover->type == type &&
			eraseHover->entityID == entityID;
	};

	// sketchLineColor / hoverLineColor / *Thickness come from BaseSurfaceViewer —
	// shared with the mesh inspector so both canvases highlight the same way

	auto drawSegment = [&](const TrimPreviewResult& segment) {
		switch (segment.geometry) {
		case TrimPreviewGeometry::Line:
			drawList->AddLine(
				camera.worldToScreen(segment.a),
				camera.worldToScreen(segment.b),
				hoverLineColor,
				hoverLineThickness
			);
			break;
		case TrimPreviewGeometry::Circle:
			drawList->AddCircle(
				camera.worldToScreen(segment.center),
				camera.worldLengthToScreen(segment.radius),
				hoverLineColor,
				96,
				hoverLineThickness
			);
			break;
		case TrimPreviewGeometry::Arc: {
			double startAngle = segment.startAngle;
			double endAngle = segment.endAngle;
			while (endAngle < startAngle) {
				endAngle += twoPi;
			}

			double span = endAngle - startAngle;
			int segments = std::max(8, (int)(std::abs(span) / twoPi * 96.0));
			Vec2 prev = pointOnCircle(segment.center, segment.radius, startAngle);

			for (int i = 1; i <= segments; i++) {
				double t = (double)(i) / (double)(segments);
				Vec2 next = pointOnCircle(
					segment.center,
					segment.radius,
					startAngle + span * t
				);

				drawList->AddLine(
					camera.worldToScreen(prev),
					camera.worldToScreen(next),
					hoverLineColor,
					hoverLineThickness
				);

				prev = next;
			}
			break;
		}
		default:
			break;
		}
	};

	for (const SketchLine& line : geometry.sketch.lines) {

		const SketchPoint* p0 = geometry.sketch.findPoint(line.p0);
		const SketchPoint* p1 = geometry.sketch.findPoint(line.p1);
		if (!p0 || !p1) {
			continue;
		}

		bool highlight =
			line.selected || isEraseHovered(SketchEntityType::Line, line.id);

		drawList->AddLine(
			camera.worldToScreen(p0->pos),
			camera.worldToScreen(p1->pos),
			highlight ? hoverLineColor : sketchLineColor,
			highlight ? hoverLineThickness : sketchLineThickness
		);
	}

	for (const SketchRectangle& rect : geometry.sketch.rectangles) {
		bool highlight =
			rect.selected || isEraseHovered(SketchEntityType::Rectangle, rect.id);

		drawList->AddRect(
			camera.worldToScreen(rect.min),
			camera.worldToScreen(rect.max),
			highlight ? hoverLineColor : sketchLineColor,
			0.0f,
			0,
			highlight ? hoverLineThickness : sketchLineThickness
		);
	}

	for (const SketchCircle& circle : geometry.sketch.circles) {
		bool highlight =
			circle.selected || isEraseHovered(SketchEntityType::Circle, circle.id);

		drawList->AddCircle(
			camera.worldToScreen(circle.center),
			camera.worldLengthToScreen(circle.radius),
			highlight ? hoverLineColor : sketchLineColor,
			80,
			highlight ? hoverLineThickness : sketchLineThickness
		);
	}

	for (const SketchArc& arc : geometry.sketch.arcs) {
		double endAngle = arc.endAngle;
		while (endAngle < arc.startAngle) {
			endAngle += twoPi;
		}

		double span = endAngle - arc.startAngle;
		int segments = std::max(8, (int)(std::abs(span) / twoPi * 80.0));
		Vec2 prev = pointOnCircle(arc.center, arc.radius, arc.startAngle);
		bool highlight =
			arc.selected || isEraseHovered(SketchEntityType::Arc, arc.id);

		for (int i = 1; i <= segments; i++) {
			double t = (double)(i) / (double)(segments);
			Vec2 next = pointOnCircle(
				arc.center,
				arc.radius,
				arc.startAngle + span * t
			);

			drawList->AddLine(
				camera.worldToScreen(prev),
				camera.worldToScreen(next),
				highlight ? hoverLineColor : sketchLineColor,
				highlight ? hoverLineThickness : sketchLineThickness
			);

			prev = next;
		}
	}

	Vec2 selectionDelta{};
	bool isMoveToPreview =
		geometry.sketch.activeTool == SketchTool::MoveTo && hasMoveToBase;
	const std::vector<TrimPreviewResult>& visibleSelectedSegments =
		isMovingSelection ? movingTrimSegments : selectedTrimSegments;
	if (isMovingSelection) {
		selectionDelta = subtract(pendingCurrentWorld, moveStartWorld);
	}
	else if (isMoveToPreview) {
		selectionDelta = subtract(pendingCurrentWorld, moveToBaseWorld);
	}

	for (const TrimPreviewResult& segment : visibleSelectedSegments) {
		drawSegment(translatedPreview(segment, selectionDelta));
	}

	if (hoveredSegment && !isMovingSelection && !isMoveToPreview) {
		drawSegment(*hoveredSegment);
	}
}

void SketchView::drawDimensions(ImDrawList* drawList) {

	ImU32 labelTextColor = IM_COL32(255, 235, 135, 255);
	ImU32 labelBgColor = IM_COL32(40, 43, 48, 225);
	ImU32 labelBorderColor = IM_COL32(255, 235, 135, 190);
	ImU32 leaderColor = IM_COL32(255, 235, 135, 130);

	// highlight the label under the cursor (only in the interactive canvas, not
	// the off-screen export; the export image is never hovered)
	std::optional<int> hoveredLabel;
	if (ImGui::IsItemHovered()) {
		hoveredLabel = findDimensionLabel(currentMousePos);
	}

	for (const SketchDimension& dimension : geometry.sketch.dimensions) {
		Vec2 anchor = dimension.labelPos;

		switch (dimension.type) {
		case SketchDimensionType::LineLength: {
			const SketchLine* line = geometry.sketch.findLine(dimension.entityID);
			if (line) {
				const SketchPoint* p0 = geometry.sketch.findPoint(line->p0);
				const SketchPoint* p1 = geometry.sketch.findPoint(line->p1);

				if (p0 && p1) {
					anchor = Vec2{
						0.5 * (p0->pos.z + p1->pos.z),
						0.5 * (p0->pos.r + p1->pos.r)
					};
				}
			}
			break;
		}
		case SketchDimensionType::CircleRadius: {
			const SketchCircle* circle =
				geometry.sketch.findCircle(dimension.entityID);

			if (circle) {
				anchor = circle->center;
			}
			break;
		}
		case SketchDimensionType::RectangleWidth:
		case SketchDimensionType::RectangleHeight: {
			const SketchRectangle* rect =
				geometry.sketch.findRectangle(dimension.entityID);

			if (rect) {
				anchor = Vec2{
					0.5 * (rect->min.z + rect->max.z),
					0.5 * (rect->min.r + rect->max.r)
				};
			}
			break;
		}
		}

		ImVec2 labelPos = camera.worldToScreen(dimension.labelPos);
		ImVec2 anchorPos = camera.worldToScreen(anchor);
		std::string label = getDimensionLabel(dimension);
		ImVec2 textSize = ImGui::CalcTextSize(label.c_str());

		ImVec2 rectMin(labelPos.x - 5.0f, labelPos.y - 3.0f);
		ImVec2 rectMax(
			labelPos.x + textSize.x + 5.0f,
			labelPos.y + textSize.y + 3.0f
		);

		bool hovered = hoveredLabel && *hoveredLabel == dimension.id;

		ImU32 bgColor = hovered ? IM_COL32(70, 78, 92, 245) : labelBgColor;
		ImU32 borderColor = hovered ? IM_COL32(255, 250, 190, 255) : labelBorderColor;
		ImU32 textColor = hovered ? IM_COL32(255, 250, 205, 255) : labelTextColor;
		float borderThickness = hovered ? 2.0f : 1.0f;

		drawList->AddLine(anchorPos, labelPos, leaderColor, 1.0f);
		drawList->AddRectFilled(rectMin, rectMax, bgColor, 3.0f);
		drawList->AddRect(rectMin, rectMax, borderColor, 3.0f, 0, borderThickness);
		drawList->AddText(labelPos, textColor, label.c_str());
	}
}

void SketchView::drawDimensionEditor() {
	if (ImGui::BeginPopupModal(
		"Edit Dimension",
		nullptr,
		ImGuiWindowFlags_AlwaysAutoResize
	)) {
		const UnitOption& unit = Units::lengthUnits[gui.project.lengthScale.index];
		double displayValue = fromBaseValue(dimensionEditValue, unit);

		ImGui::SetNextItemWidth(150.0f);

		// NOTE: InputDouble/InputScalar does NOT support EnterReturnsTrue (it
		// asserts). Without it the typed value is written back live, so Apply
		// always sees the latest value; detect Enter via IsItemDeactivatedAfterEdit.
		ImGui::InputDouble("##Value", &displayValue, 0.0, 0.0, "%.6g");

		bool enterPressed =
			ImGui::IsItemDeactivatedAfterEdit() &&
			(ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
				ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));

		ImGui::SameLine();
		ImGui::TextDisabled("%s", unit.name);

		dimensionEditValue = toBaseValue(displayValue, unit);

		bool applyPressed = ImGui::Button("Apply");
		ImGui::SameLine();
		bool cancelPressed = ImGui::Button("Cancel");

		if (enterPressed || applyPressed) {
			SketchModel beforeDimensionEdit = geometry.sketch;
			bool changed = geometry.sketch.setDimensionValue(
				editingDimensionID,
				dimensionEditValue
			);
			if (changed) {
				recordSketchUndoState(beforeDimensionEdit);
			}
			editingDimensionID = -1;
			ImGui::CloseCurrentPopup();
		}

		if (cancelPressed) {
			editingDimensionID = -1;
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}
}

void SketchView::drawTemporarySketch(ImDrawList* drawList) {

	SketchTool tool = geometry.sketch.activeTool;

	ImU32 previewLineColor = IM_COL32(125, 220, 255, 255);
	ImU32 previewFillColor = IM_COL32(125, 220, 255, 35);

	if (isSelecting && tool == SketchTool::Select) {

		ImVec2 startScreen = initLeftMouse;
		ImVec2 currentScreen = currentMousePos;

		ImVec2 rectMin{
			std::min(startScreen.x, currentScreen.x),
			std::min(startScreen.y, currentScreen.y)
		};

		ImVec2 rectMax{
			std::max(startScreen.x, currentScreen.x),
			std::max(startScreen.y, currentScreen.y)
		};

		drawList->AddRectFilled(rectMin, rectMax, previewFillColor);
		drawList->AddRect(rectMin, rectMax, previewLineColor, 0.0f, 0, 2.0f);
	}

	if (tool == SketchTool::MoveTo) {
		if (hasMoveToBase) {
			ImVec2 baseScreen = camera.worldToScreen(moveToBaseWorld);
			ImVec2 targetScreen = camera.worldToScreen(pendingCurrentWorld);
			drawList->AddLine(baseScreen, targetScreen, previewLineColor, 1.5f);
			drawList->AddCircleFilled(baseScreen, 4.0f, previewLineColor);
			drawList->AddCircle(targetScreen, 5.0f, previewLineColor, 16, 2.0f);
		}
		else if (ImGui::IsItemHovered()) {
			if (auto base = findSelectedMovePoint(currentMousePos)) {
				drawList->AddCircleFilled(
					camera.worldToScreen(*base),
					4.0f,
					previewLineColor
				);
			}
		}
	}
}

void SketchView::handleOpenPopup() {

	// only open the context menu for a right-click over the canvas image; without
	// the hover gate a right-click anywhere (the toolbar, or another panel) would
	// pop the sketch menu, since IsMouseClicked is global
	if (ImGui::IsItemHovered() &&
		ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {

		openPopUp = true;

	}
}

void SketchView::drawPopup(ImDrawList* drawList) {
	if (openPopUp) {
		ImGui::OpenPopup("Sketch Popup");
		openPopUp = false;
	}


	if (ImGui::BeginPopup("Sketch Popup")) {

		//addMenuItem
		addMenuItemCopyToClipboard("Copy to clipboard");

		if (ImGui::MenuItem("Reset View", ImGui::GetKeyChordName(resetViewShortcut))) {
			resetView();
		}

		if (undoSketchStates.empty()) {
			ImGui::BeginDisabled();
		}

		if (ImGui::MenuItem("Undo", ImGui::GetKeyChordName(undoShortcut))) {
			undoSketchEdit();
		}

		if (undoSketchStates.empty()) {
			ImGui::EndDisabled();
		}

		if (redoSketchStates.empty()) {
			ImGui::BeginDisabled();
		}

		if (ImGui::MenuItem("Redo", ImGui::GetKeyChordName(redoShortcut))) {
			redoSketchEdit();
		}

		if (redoSketchStates.empty()) {
			ImGui::EndDisabled();
		}

		// Segment naming lives in the Mesh tab; the Geometry/sketch view no
		// longer creates or lists named selections.
		ImGui::EndPopup();
	}
}

void SketchView::drawPendingSketchEntity(ImDrawList* drawList) {
	if (!isDrawingEntity) {
		return;
	}

	SketchTool tool = geometry.sketch.activeTool;

	if (tool != SketchTool::Circle && tool != SketchTool::Rectangle && tool != SketchTool::Line) {
		return;
	}

	ImU32 previewLineColor = IM_COL32(125, 220, 255, 255);
	ImU32 previewFillColor = IM_COL32(125, 220, 255, 35);

	// the snap indicator is drawn once by drawSnapping() (single source of
	// truth); don't draw a second dot here

	if (tool == SketchTool::Line) {
		drawList->AddLine(
			camera.worldToScreen(pendingStartWorld),
			camera.worldToScreen(pendingCurrentWorld),
			previewLineColor,
			1.0f
		);
	}
	else if (tool == SketchTool::Rectangle) {
		ImVec2 startScreen = camera.worldToScreen(pendingStartWorld);
		ImVec2 currentScreen = camera.worldToScreen(pendingCurrentWorld);

		ImVec2 rectMin{
			std::min(startScreen.x, currentScreen.x),
			std::min(startScreen.y, currentScreen.y)
		};

		ImVec2 rectMax{
			std::max(startScreen.x, currentScreen.x),
			std::max(startScreen.y, currentScreen.y)
		};

		drawList->AddRectFilled(rectMin, rectMax, previewFillColor);
		drawList->AddRect(rectMin, rectMax, previewLineColor, 0.0f, 0, 2.0f);
	}
	else if (tool == SketchTool::Circle) {
		Vec2 center = pendingStartWorld;
		Vec2 edge = pendingCurrentWorld;
		double radius = distance(center, edge);
		ImVec2 centerScreen = camera.worldToScreen(center);
		ImVec2 edgeScreen = camera.worldToScreen(edge);

		drawList->AddCircle(
			centerScreen,
			camera.worldLengthToScreen(radius),
			previewLineColor,
			80,
			2.0f
		);

		drawList->AddLine(centerScreen, edgeScreen, IM_COL32(125, 220, 255, 150), 1.0f);
		drawList->AddCircleFilled(centerScreen, 3.0f, previewLineColor);
	}
}

void SketchView::updateCurrentWorld() {

	pendingCurrentWorld = getSnappedWorld(currentMousePos);

}

void SketchView::drawSnapping(ImDrawList* drawList) {

	// preview the exact point getSnappedWorld() would snap to (grid vertices
	// prioritized over sketch edges when the grid is shown)
	if (auto snap = resolveSnap(currentMousePos)) {
		drawList->AddCircleFilled(
			snap->screen,
			4.0f,
			IM_COL32(255, 230, 80, 255)
		);
	}
}

void SketchView::render() {

	updateLengthScale(
		gui.project.lengthScale.value,
		Units::lengthUnits[gui.project.lengthScale.index].name
	);

	consumePendingDelete();
	consumePendingMove();
	consumePendingGroupMove();
	publishSelectionSummary();


	ImGui::SetNextWindowClass(&windowClass);
	ImGui::Begin(UIViewport::SketchTitle);


	ImDrawList* drawList = ImGui::GetWindowDrawList();

	// toolbar lives in the app-wide strip above the dockspace (GUI::drawAppToolbar)

	ImVec2 pos = ImGui::GetCursorScreenPos();
	ImVec2 size = ImGui::GetContentRegionAvail();
	canvasRect = makePaddedRect(pos, size);

	resizeImage();

	drawSurface(canvasRect);
	drawCanvas(drawList, canvasRect, 0.0f, canvasBgColor, canvasOutlineColor);

	camera.setDimensions(
		static_cast<int>(canvasRect.size.x),
		static_cast<int>(canvasRect.size.y),
		canvasRect.min
	);

	// recenter/re-zoom to the loaded project's units if a reset was requested
	applyPendingResetView();

	// update current global mouse pos
	updateCurrentMousePos();
	updateCurrentWorld();

	// handle mouse and keyboard presses
	handleMouseAndKey();
	drawPopup(drawList);

	drawList->PushClipRect(canvasRect.min, canvasRect.max, true);
	drawGrid(drawList);
	drawAxes(drawList);
	drawSketchEntities(drawList);
	drawTrimPreview(drawList);
	drawDimensions(drawList);
	drawPendingSketchEntity(drawList);
	drawTemporarySketch(drawList);
	drawSnapping(drawList);
	drawDimensionEditor();
	drawList->PopClipRect();

	ImGui::End();
}
