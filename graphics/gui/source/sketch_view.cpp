#include "sketch_view.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "project.h"
#include "gui.h"

#include "geometry.h"
#include "math_func.h"

using namespace sketchmath;

SketchView::SketchView(Project& project, GUI& gui) :
	geometry(project.geometry),
	gui(gui),
	assets(gui.appConfig.assets),
	BaseSurfaceViewer("graphics/shaders/sketch.vert", "graphics/shaders/sketch.frag") {
	frameBuffer.create2DBuffer(500, 500, GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE);
}

void SketchView::clearToolToggles() {
	toggleRuler = false;
	toggleRemoveCell = false;
	toggleTrim = false;
	toggleDrawLine = false;
	toggleDrawRect = false;
	toggleDrawCircle = false;
	isDrawingEntity = false;
}

void SketchView::setActiveSketchTool(SketchTool tool) {
	clearToolToggles();
	geometry.sketch.activeTool = tool;

	switch (tool) {
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

	{
		Vec2 origin{
			0.0,
			0.0
		};

		ImVec2 originScreen = camera.worldToScreen(origin);
		float originDistancePx = pixelDistance(originScreen, mouse);
		if (originDistancePx <= snapRadiusPx) {
			return SnapResult{
				SnapType::Vertex,
				origin,
				originScreen,
				originDistancePx,
				-102
			};
		}
	}

	// snap to axis
	// snap to x-axis: r = 0
	{
		Vec2 closest{
			mouseWorld.z,
			0.0
		};

		tryCandidate(SnapType::Line, closest, -100);
	}

	// snap to y-axis: z = 0
	{
		Vec2 closest{
			0.0,
			mouseWorld.r
		};

		tryCandidate(SnapType::Line, closest, -101);
	}

	// snap to vertices
	for (const SketchPoint& point : geometry.sketch.points) {
		tryCandidate(SnapType::Vertex, point.pos, point.id);
	}

	// snap to lines / edges
	for (const SketchLine& line : geometry.sketch.lines) {
		const SketchPoint& p0 = geometry.sketch.points[line.p0];
		const SketchPoint& p1 = geometry.sketch.points[line.p1];

		Vec2 closest = closestPointOnSegment(mouseWorld, p0.pos, p1.pos);
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
	char buffer[64] = {};

	switch (dimension.type) {
	case SketchDimensionType::LineLength:
		std::snprintf(buffer, sizeof(buffer), "%.6g", value);
		break;
	case SketchDimensionType::CircleRadius:
		std::snprintf(buffer, sizeof(buffer), "R %.6g", value);
		break;
	case SketchDimensionType::RectangleWidth:
		std::snprintf(buffer, sizeof(buffer), "W %.6g", value);
		break;
	case SketchDimensionType::RectangleHeight:
		std::snprintf(buffer, sizeof(buffer), "H %.6g", value);
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

void SketchView::drawToolBar() {

	float toolbarHeight = 40.0f;

	ImGui::BeginChild("##toolbar", ImVec2(0.0f, toolbarHeight), false);

	if (addImageButton("Reset", "Reset View", assets.houseIcon, buttonSize)) {
		resetView();
		camera.home();
	}
	ImGui::SameLine();

	addToolbarSeparator(toolbarHeight);

	if (addImageButtonToggle("Ruler", "Ruler", assets.rulerIcon, buttonSize, toggleRuler)) {
		setActiveSketchTool(toggleRuler ? SketchTool::Dimension : SketchTool::Select);
	}
	ImGui::SameLine();

	if (addImageButtonToggle("Trim", "Trim", assets.trimIcon, buttonSize, toggleTrim)) {
		setActiveSketchTool(toggleTrim ? SketchTool::Trim : SketchTool::Select);
	}
	ImGui::SameLine();

	if (addImageButtonToggle("Erase", "Erase", assets.eraseIcon, buttonSize, toggleEraser)) {
		setActiveSketchTool(toggleEraser ? SketchTool::Erase : SketchTool::Select);
	}
	ImGui::SameLine();

	 
	addToolbarSeparator(toolbarHeight);

	if (addImageButtonToggle("DrawLine", "Draw Straight Line", assets.drawLineIcon, buttonSize, toggleDrawLine)) {
		setActiveSketchTool(toggleDrawLine ? SketchTool::Line : SketchTool::Select);
	}
	ImGui::SameLine();

	if (addImageButtonToggle("DrawRect", "Draw Rectangle", assets.selectRegionIcon, buttonSize, toggleDrawRect)) {
		setActiveSketchTool(toggleDrawRect ? SketchTool::Rectangle : SketchTool::Select);
	}
	ImGui::SameLine();

	if (addImageButtonToggle("DrawCircle", "Draw Circle", assets.drawCircleIcon, buttonSize, toggleDrawCircle)) {
		setActiveSketchTool(toggleDrawCircle ? SketchTool::Circle : SketchTool::Select);
	}
	ImGui::SameLine();

	addToolbarSeparator(toolbarHeight);

	if (addImageButton("Copy", "Copy to clipboard", assets.copyIcon, buttonSize) || consoleCopy) {
		pendingCopyWidth = frameBuffer.width;
		pendingCopyHeight = frameBuffer.height;
		pendingCopy = true;
		consoleCopy = false;
	}

	ImGui::SameLine();

	ImGui::EndChild();
}

Vec2 SketchView::getSnappedWorld(ImVec2 mouse) {
	if (!toggleSnapping) {
		return camera.screenToWorld(mouse);
	}

	if (auto snap = findSnap(mouse)) {
		return snap->world;
	}

	return camera.screenToWorld(mouse);
}

void SketchView::setInitLeftMouse() {
	pendingStartWorld = getSnappedWorld(currentMousePos);
	initLeftMouse = camera.worldToScreen(pendingStartWorld);
}

void SketchView::handleSelect() {

	if (!(geometry.sketch.activeTool == SketchTool::Select)) return;

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

	if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
		if (auto dimensionID = findDimensionLabel(currentMousePos)) {
			openDimensionEditor(*dimensionID);
			return;
		}
	}

	if (ImGui::IsItemHovered()) {
		if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
			isSelecting = true;
		}

		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
			setInitLeftMouse();
		}
	}

	if (!isSelecting) {
		if (ImGui::IsItemHovered() &&
			ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
			if (!io.KeyShift) {
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

		if (!io.KeyShift) {
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

void SketchView::handleTrimTool() {
	if (geometry.sketch.activeTool != SketchTool::Trim) {
		return;
	}

	if (!ImGui::IsItemHovered()) {
		return;
	}

	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		trimSketchAtMouse(currentMousePos);
	}
}

void SketchView::handleMouseAndKey() {
	ImGuiIO& io = ImGui::GetIO();
	bool usesDragDrawing =
		geometry.sketch.activeTool == SketchTool::Circle ||
		geometry.sketch.activeTool == SketchTool::Rectangle ||
		geometry.sketch.activeTool == SketchTool::Line;

	toggleSnapping = io.KeyCtrl;

	bool deletePressed =
		ImGui::IsKeyPressed(ImGuiKey_Delete, false) ||
		ImGui::IsKeyPressed(ImGuiKey_Backspace, false);
	if (geometry.sketch.activeTool == SketchTool::Select &&
		!selectedTrimSegments.empty() &&
		editingDimensionID < 0 &&
		!io.WantTextInput &&
		ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
		deletePressed) {
		isSelecting = false;
		isDrawingEntity = false;
		deleteSelectedTrimSegments();
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

	handleSelect();

	handleDimensionTool();

	handleTrimTool();

	if (!isDrawingEntity) {
		return;
	}


	if (geometry.sketch.activeTool == SketchTool::Line &&
		ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
		Vec2 start = pendingStartWorld;
		Vec2 end = pendingCurrentWorld;

		double length = distance(start, end);

		if (length > 1e-12) {
			geometry.sketch.addLine(start, end);
		}

		isDrawingEntity = false;
	}

	if (geometry.sketch.activeTool == SketchTool::Circle &&
		ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
		Vec2 center = pendingStartWorld;
		Vec2 edge = pendingCurrentWorld;
		double radius = distance(center, edge);

		if (radius > 1e-12) {
			geometry.sketch.addCircle(center, radius);
		}

		isDrawingEntity = false;
	}

	if (geometry.sketch.activeTool == SketchTool::Rectangle &&
		ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
		Vec2 cornerA = pendingStartWorld;
		Vec2 cornerB = pendingCurrentWorld;

		if (distance(cornerA, cornerB) > 1e-12) {
			geometry.sketch.addRectangle(cornerA, cornerB);
		}

		isDrawingEntity = false;
	}

	if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
		isDrawingEntity = false;
	}
}

void SketchView::handleDimensionTool() {
	if (geometry.sketch.activeTool != SketchTool::Dimension) {
		return;
	}

	if (!ImGui::IsItemHovered()) {
		return;
	}

	if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		return;
	}

	if (auto dimensionID = findDimensionLabel(currentMousePos)) {
		openDimensionEditor(*dimensionID);
		return;
	}

	if (auto target = findDimensionTarget(currentMousePos)) {
		int dimensionID = geometry.sketch.addDimension(
			target->type,
			target->entityID,
			target->labelPos
		);

		openDimensionEditor(dimensionID);
	}
}


void SketchView::drawAxes(ImDrawList* drawList) {
	ImVec2 origin = camera.worldToScreen(Vec2{ 0.0, 0.0 });

	drawList->PushClipRect(imageMin, imageMax, true);

	if (origin.y >= imageMin.y && origin.y <= imageMax.y) {
		drawList->AddLine(
			ImVec2(imageMin.x, origin.y),
			ImVec2(imageMax.x, origin.y),
			IM_COL32(210, 55, 55, 255),
			1.5f
		);

		drawList->AddText(
			ImVec2(imageMax.x - 18.0f, origin.y + 6.0f),
			IM_COL32(230, 80, 80, 255),
			"x"
		);
	}

	if (origin.x >= imageMin.x && origin.x <= imageMax.x) {
		drawList->AddLine(
			ImVec2(origin.x, imageMin.y),
			ImVec2(origin.x, imageMax.y),
			IM_COL32(55, 190, 95, 255),
			1.5f
		);

		drawList->AddText(
			ImVec2(origin.x + 6.0f, imageMin.y + 6.0f),
			IM_COL32(80, 220, 120, 255),
			"y"
		);
	}

	drawList->AddCircleFilled(origin, 3.5f, IM_COL32(235, 235, 235, 255));

	drawList->PopClipRect();
}

void SketchView::drawSketchEntities(ImDrawList* drawList) {
	drawList->PushClipRect(imageMin, imageMax, true);

	std::optional<TrimPreviewResult> hoveredSegment =
		geometry.sketch.activeTool == SketchTool::Select &&
		ImGui::IsItemHovered() ?
		findTrimPreview(currentMousePos) :
		std::nullopt;

	const ImU32 sketchLineColor = IM_COL32(65, 150, 255, 255);
	const ImU32 hoverLineColor = IM_COL32(255, 225, 80, 255);
	const float sketchLineThickness = 2.0f;
	const float hoverLineThickness = 3.5f;

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

		const SketchPoint& p0 = geometry.sketch.points[line.p0];
		const SketchPoint& p1 = geometry.sketch.points[line.p1];

		drawList->AddLine(
			camera.worldToScreen(p0.pos),
			camera.worldToScreen(p1.pos),
			line.selected ? hoverLineColor : sketchLineColor,
			line.selected ? hoverLineThickness : sketchLineThickness
		);
	}

	for (const SketchRectangle& rect : geometry.sketch.rectangles) {
		drawList->AddRect(
			camera.worldToScreen(rect.min),
			camera.worldToScreen(rect.max),
			rect.selected ? hoverLineColor : sketchLineColor,
			0.0f,
			0,
			rect.selected ? hoverLineThickness : sketchLineThickness
		);
	}

	for (const SketchCircle& circle : geometry.sketch.circles) {
		drawList->AddCircle(
			camera.worldToScreen(circle.center),
			camera.worldLengthToScreen(circle.radius),
			circle.selected ? hoverLineColor : sketchLineColor,
			80,
			circle.selected ? hoverLineThickness : sketchLineThickness
		);
	}

	for (const SketchArc& arc : geometry.sketch.arcs) {
		double span = arc.endAngle - arc.startAngle;
		int segments = std::max(8, (int)(std::abs(span) / twoPi * 80.0));
		Vec2 prev = pointOnCircle(arc.center, arc.radius, arc.startAngle);

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
				arc.selected ? hoverLineColor : sketchLineColor,
				arc.selected ? hoverLineThickness : sketchLineThickness
			);

			prev = next;
		}
	}

	for (const TrimPreviewResult& segment : selectedTrimSegments) {
		drawSegment(segment);
	}

	if (hoveredSegment) {
		drawSegment(*hoveredSegment);
	}

	drawList->PopClipRect();
}

void SketchView::drawDimensions(ImDrawList* drawList) {
	drawList->PushClipRect(imageMin, imageMax, true);

	ImU32 labelTextColor = IM_COL32(255, 235, 135, 255);
	ImU32 labelBgColor = IM_COL32(40, 43, 48, 225);
	ImU32 labelBorderColor = IM_COL32(255, 235, 135, 190);
	ImU32 leaderColor = IM_COL32(255, 235, 135, 130);

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

		drawList->AddLine(anchorPos, labelPos, leaderColor, 1.0f);
		drawList->AddRectFilled(rectMin, rectMax, labelBgColor, 3.0f);
		drawList->AddRect(rectMin, rectMax, labelBorderColor, 3.0f, 0, 1.0f);
		drawList->AddText(labelPos, labelTextColor, label.c_str());
	}

	drawList->PopClipRect();
}

void SketchView::drawDimensionEditor() {
	if (ImGui::BeginPopupModal(
		"Edit Dimension",
		nullptr,
		ImGuiWindowFlags_AlwaysAutoResize
	)) {
		ImGui::SetNextItemWidth(150.0f);

		bool enterPressed = ImGui::InputDouble(
			"##Value",
			&dimensionEditValue,
			0.0,
			0.0,
			"%.6g",
			ImGuiInputTextFlags_EnterReturnsTrue
		);

		bool applyPressed = ImGui::Button("Apply");
		ImGui::SameLine();
		bool cancelPressed = ImGui::Button("Cancel");

		if (enterPressed || applyPressed) {
			geometry.sketch.setDimensionValue(
				editingDimensionID,
				dimensionEditValue
			);
			print(dimensionEditValue);
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

	drawList->PushClipRect(imageMin, imageMax, true);

	if (toggleSnapping) {
		drawList->AddCircleFilled(
			camera.worldToScreen(pendingStartWorld),
			3.0f,
			IM_COL32(255, 230, 80, 255)
		);

		if (auto snap = findSnap(currentMousePos)) {
			drawList->AddCircleFilled(
				snap->screen,
				4.0f,
				IM_COL32(255, 230, 80, 255)
			);
		}
	}

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

	drawList->PopClipRect();
}

void SketchView::updateCurrentWorld() {

	pendingCurrentWorld = getSnappedWorld(currentMousePos);

}

void SketchView::drawSnapping(ImDrawList* drawList) {

	if (toggleSnapping) {
		if (auto snap = findSnap(currentMousePos)) {
			drawList->AddCircleFilled(
				snap->screen,
				4.0f,
				IM_COL32(255, 230, 80, 255)
			);
		}
	}
}

void SketchView::render() {

	ImGui::Begin("Sketch View");

	ImDrawList* drawList = ImGui::GetWindowDrawList();

	drawToolBar();

	ImVec2 pos = ImGui::GetCursorScreenPos();
	ImVec2 size = ImGui::GetContentRegionAvail();
	Rect viewRect = makePaddedRect(pos, size);

	resizeImage(viewRect.size());

	drawSurface(viewRect);
	drawCanvas(drawList, viewRect, 0.0f, sketchBgColor, outlineColor);

	camera.setDimensions(
		static_cast<int>(imageSize.x),
		static_cast<int>(imageSize.y),
		imageMin
	);

	// update current global mouse pos
	updateCurrentMousePos();
	updateCurrentWorld();

	// handle mouse and keyboard presses
	handleMouseAndKey();


	drawAxes(drawList);
	drawSketchEntities(drawList);
	drawTrimPreview(drawList);
	drawDimensions(drawList);
	drawPendingSketchEntity(drawList);
	drawTemporarySketch(drawList);
	drawSnapping(drawList);
	drawDimensionEditor();

	ImGui::End();
}
