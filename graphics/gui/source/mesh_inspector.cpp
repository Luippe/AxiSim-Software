#include "mesh_inspector.h"

#include <format>
#include <algorithm>
#include <glm/glm.hpp>

#include "scene_view.h"
#include "colorbar.h"

#include "flag_manager.h"
#include "printer.h"
#include "math_func.h"

#include <iostream>

MeshInspector::MeshInspector(Mesh& mesh, AppAssets& assets) :
	mesh(mesh),
	g(mesh.g),
	assets(assets),
	BaseSurfaceViewer("graphics/shaders/mesh.vert", "graphics/shaders/mesh.frag") {

	// radial location
	frameBuffer.create2DBuffer(500, 500, GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE);
	createGridBuffer();
}

// ======================================================================
// -----------------------HELPER FUNCTIONS-------------------------------
// ======================================================================
int edgeLengthInFaces(const BoundarySegment& seg) {
	int di = std::abs(seg.b.i - seg.a.i);
	int dj = std::abs(seg.b.j - seg.a.j);

	return di + dj;
}

bool MeshInspector::isDomainBoundaryEdge(const MeshEdge& e) const {
	if (e.orient == EdgeOrient::Horizontal) {
		return e.i == 0 || e.i == nrBase;
	}
	else {
		return e.j == 0 || e.j == nzBase;
	}
}

void debugCheckBoundaryMerge(
	const std::unordered_set<MeshEdge, MeshEdgeHash>& rawEdges,
	const std::vector<BoundarySegment>& segments
) {
	int rawCount = static_cast<int>(rawEdges.size());
	int mergedFaceCount = 0;

	for (const BoundarySegment& seg : segments) {
		bool diagonal =
			seg.a.i != seg.b.i &&
			seg.a.j != seg.b.j;

		if (diagonal) {
			std::cout << "ERROR: diagonal boundary segment found\n";
			std::cout << "a = (" << seg.a.i << ", " << seg.a.j << ")\n";
			std::cout << "b = (" << seg.b.i << ", " << seg.b.j << ")\n";
		}

		mergedFaceCount += edgeLengthInFaces(seg);
	}

	if (rawCount != mergedFaceCount) {
		std::cout << "ERROR: boundary merge lost or added edges\n";
		std::cout << "raw edge count: " << rawCount << "\n";
		std::cout << "merged face count: " << mergedFaceCount << "\n";
	}
}

bool isAxisAligned(const GridVertex& a, const GridVertex& b) {
	return a.i == b.i || a.j == b.j;
}

int MeshInspector::cellIndex(int i, int j) const {
	return i * nzBase + j;
}

bool MeshInspector::isInsideCellGrid(int i, int j) const {
	return i >= 0 && i < nrBase &&
		j >= 0 && j < nzBase;
}

bool MeshInspector::isSolidCell(
	int i,
	int j,
	const std::unordered_set<int>& obstacleIndices
) const {
	if (!isInsideCellGrid(i, j)) {
		return false;
	}

	int n = cellIndex(i, j);

	return obstacleIndices.find(n) != obstacleIndices.end();
}

GridVertex edgeStart(const MeshEdge& e) {
	return GridVertex{ e.i, e.j };
}

GridVertex edgeEnd(const MeshEdge& e) {
	if (e.orient == EdgeOrient::Horizontal) {
		return GridVertex{ e.i, e.j + 1 };
	}
	else {
		return GridVertex{ e.i + 1, e.j };
	}
}

void MeshInspector::rebuildSelectableOuterEdges(
	const std::unordered_set<int>& obstacleIndices
) {
	mesh.selectableOuterEdges.clear();

	for (int n : obstacleIndices) {
		if (n < 0 || n >= nrBase * nzBase) {
			continue;
		}

		int i = n / nzBase;
		int j = n % nzBase;

		// Top face of solid cell
		if (!isSolidCell(i - 1, j, obstacleIndices)) {
			mesh.selectableOuterEdges.insert({
				EdgeOrient::Horizontal,
				i,
				j
				});
		}

		// Bottom face of solid cell
		if (!isSolidCell(i + 1, j, obstacleIndices)) {
			mesh.selectableOuterEdges.insert({
				EdgeOrient::Horizontal,
				i + 1,
				j
				});
		}

		// Left face of solid cell
		if (!isSolidCell(i, j - 1, obstacleIndices)) {
			mesh.selectableOuterEdges.insert({
				EdgeOrient::Vertical,
				i,
				j
				});
		}

		// Right face of solid cell
		if (!isSolidCell(i, j + 1, obstacleIndices)) {
			mesh.selectableOuterEdges.insert({
				EdgeOrient::Vertical,
				i,
				j + 1
				});
		}
	}
}



std::unordered_map<GridVertex, int, GridVertexHash>
buildVertexDegreeMap(
	const std::unordered_set<MeshEdge, MeshEdgeHash>& edges
) {
	std::unordered_map<GridVertex, int, GridVertexHash> degree;

	for (const MeshEdge& e : edges) {
		GridVertex a = edgeStart(e);
		GridVertex b = edgeEnd(e);

		degree[a]++;
		degree[b]++;
	}

	return degree;
}

bool MeshInspector::domainEdgeTouchesSolid(
	const MeshEdge& e,
	const std::unordered_set<int>& obstacleIndices
) const {
	if (e.orient == EdgeOrient::Horizontal) {
		// Top domain boundary
		if (e.i == 0) {
			int cellI = 0;
			int cellJ = e.j;
			return isSolidCell(cellI, cellJ, obstacleIndices);
		}

		// Bottom domain boundary
		if (e.i == nrBase) {
			int cellI = nrBase - 1;
			int cellJ = e.j;
			return isSolidCell(cellI, cellJ, obstacleIndices);
		}
	}
	else {
		// Left domain boundary
		if (e.j == 0) {
			int cellI = e.i;
			int cellJ = 0;
			return isSolidCell(cellI, cellJ, obstacleIndices);
		}

		// Right domain boundary
		if (e.j == nzBase) {
			int cellI = e.i;
			int cellJ = nzBase - 1;
			return isSolidCell(cellI, cellJ, obstacleIndices);
		}
	}

	return false;
}

std::vector<BoundarySegment> buildDisplayBoundaries(
	const std::unordered_set<MeshEdge, MeshEdgeHash>& edges
) {
	std::vector<BoundarySegment> segments;

	if (edges.empty()) {
		return segments;
	}

	std::unordered_map<GridVertex, int, GridVertexHash> degree =
		buildVertexDegreeMap(edges);

	std::vector<MeshEdge> sortedEdges(edges.begin(), edges.end());

	std::sort(
		sortedEdges.begin(),
		sortedEdges.end(),
		[](const MeshEdge& a, const MeshEdge& b) {
			if (a.orient != b.orient) {
				return static_cast<int>(a.orient) <
					static_cast<int>(b.orient);
			}

			if (a.orient == EdgeOrient::Horizontal) {
				if (a.i != b.i) {
					return a.i < b.i;
				}

				return a.j < b.j;
			}
			else {
				if (a.j != b.j) {
					return a.j < b.j;
				}

				return a.i < b.i;
			}
		}
	);

	MeshEdge first = sortedEdges[0];

	EdgeOrient currentOrient = first.orient;

	int fixed = 0;
	int start = 0;
	int prev = 0;

	if (first.orient == EdgeOrient::Horizontal) {
		fixed = first.i; // fixed row
		start = first.j;
		prev = first.j;
	}
	else {
		fixed = first.j; // fixed column
		start = first.i;
		prev = first.i;
	}

	auto pushSegment = [&]() {
		if (currentOrient == EdgeOrient::Horizontal) {
			segments.push_back({
				GridVertex{ fixed, start },
				GridVertex{ fixed, prev + 1 }
				});
		}
		else {
			segments.push_back({
				GridVertex{ start,     fixed },
				GridVertex{ prev + 1, fixed }
				});
		}
		};

	for (std::size_t k = 1; k < sortedEdges.size(); k++) {
		const MeshEdge& e = sortedEdges[k];

		int eFixed = 0;
		int ePos = 0;

		if (e.orient == EdgeOrient::Horizontal) {
			eFixed = e.i;
			ePos = e.j;
		}
		else {
			eFixed = e.j;
			ePos = e.i;
		}

		bool sameLine =
			e.orient == currentOrient &&
			eFixed == fixed;

		bool adjacent =
			ePos == prev + 1;

		bool safeToMerge = false;

		if (sameLine && adjacent) {
			GridVertex sharedVertex;

			if (currentOrient == EdgeOrient::Horizontal) {
				sharedVertex = GridVertex{ fixed, ePos };
			}
			else {
				sharedVertex = GridVertex{ ePos, fixed };
			}

			auto it = degree.find(sharedVertex);

			int vertexDegree = 0;
			if (it != degree.end()) {
				vertexDegree = it->second;
			}

			// Only merge through simple vertices.
			// If degree is not 2, it is a corner/junction/diagonal-touch point.
			safeToMerge = vertexDegree == 2;
		}

		if (sameLine && adjacent && safeToMerge) {
			prev = ePos;
		}
		else {
			pushSegment();

			currentOrient = e.orient;

			if (e.orient == EdgeOrient::Horizontal) {
				fixed = e.i;
				start = e.j;
				prev = e.j;
			}
			else {
				fixed = e.j;
				start = e.i;
				prev = e.i;
			}
		}
	}

	pushSegment();

	return segments;
}


void MeshInspector::setBaseNrNz() {
	nrBase = g.nr;
	nzBase = g.nz;
}

void MeshInspector::createGridBuffer() {

	vertexBuffer.createBuffer(mesh.gridLineVertices.size() * sizeof(float), mesh.gridLineVertices.data());
	vertexBuffer.bind();
	vertexBuffer.enableAttribute(0, 2, GL_FLOAT, 2 * sizeof(float), (void*)0);
	vertexBuffer.unbind();

}

MeshEdge MeshInspector::nearestEdgeFromGridPoint(const ImVec2& p) {

	int jFace = std::clamp(int(std::round(p.x)), 0, nzBase);
	int iFace = std::clamp(int(std::round(p.y)), 0, nrBase);

	int jCell = std::clamp(int(std::floor(p.x)), 0, nzBase - 1);
	int iCell = std::clamp(int(std::floor(p.y)), 0, nrBase - 1);

	float distToVertical = std::abs(p.x - float(jFace));
	float distToHorizontal = std::abs(p.y - float(iFace));

	if (distToVertical < distToHorizontal) {
		// Vertical face at axial face jFace,
		// spanning radial cell iCell.
		return MeshEdge{
			EdgeOrient::Vertical,
			iCell,
			jFace
		};
	}
	else {
		// Horizontal face at radial face iFace,
		// spanning axial cell jCell.
		return MeshEdge{
			EdgeOrient::Horizontal,
			iFace,
			jCell
		};
	}
}

float distPointToSegment(ImVec2 p, ImVec2 a, ImVec2 b) {
	ImVec2 ab(b.x - a.x, b.y - a.y);
	ImVec2 ap(p.x - a.x, p.y - a.y);

	float ab2 = ab.x * ab.x + ab.y * ab.y;

	if (ab2 <= 1e-8f) {
		float dx = p.x - a.x;
		float dy = p.y - a.y;
		return std::sqrt(dx * dx + dy * dy);
	}

	float t = (ap.x * ab.x + ap.y * ab.y) / ab2;
	t = std::clamp(t, 0.0f, 1.0f);

	ImVec2 closest(
		a.x + t * ab.x,
		a.y + t * ab.y
	);

	float dx = p.x - closest.x;
	float dy = p.y - closest.y;

	return std::sqrt(dx * dx + dy * dy);
}

// ======================================================================
// -----------------------MOUSE HANDLES----------------------------------
// ======================================================================
void MeshInspector::handleItemButtonSelect() {
	if (!toggleSelect) return;

	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && toggleSelect) {

		addHighlightCell(g.obstacleIndices, (int)currentMouseIndex.y * nzBase + (int)currentMouseIndex.x);
		initMouseIndex = currentMouseIndex;

	}

	if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && toggleSelect) {
		addHighlightCell(g.obstacleIndices, (int)currentMouseIndex.y * nzBase + (int)currentMouseIndex.x);
	}

	if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && toggleSelect && ImGui::IsKeyPressed(ImGuiKey_LeftCtrl)) {
		highlightCellsInRect(g.obstacleIndices, initMouseIndex, currentMouseIndex, nzBase, nrBase);
	}

	if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && toggleSelect) {
		rebuildSelectableOuterEdges(g.obstacleIndices);
		//printInt(mesh.selectableOuterEdges.size());
	}

}

void MeshInspector::handleItemButtonConnecting() {

	if (!toggleConnecting) return;


}

void MeshInspector::handleCursor(ImGuiIO& io) {
	if (toggleSelect || toggleConnecting) return;

	if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
		handlePan(io);
	}







	if (!hoveredId.has_value()) {

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddCircleFilled(currentMousePos, circleRadius, IM_COL32(150, 150, 150, 255), 16);
		drawList->AddCircle(currentMousePos, circleRadius, IM_COL32(200, 200, 200, 255), 16, 1.0f);
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !hoveredId.has_value()) {

			double zPos, rPos;
			getMousePhysicalCoord(currentMousePos, g.rFace, g.zFace, rPos, zPos);
			ImVec2 vecValue = ImVec2((float)zPos, (float)rPos);

			toggleSelectedPoint(points, currentMouseIndex, vecValue, 0.0f);	// value is 0.0f for now
		}

	}
	else {
		
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hoveredId.has_value()) {
			std::unordered_set<int>& selectedBoundaryIds = mesh.selectedBoundaryIds;

			auto it = selectedBoundaryIds.find(*hoveredId);

			if (it == selectedBoundaryIds.end()) {
				selectedBoundaryIds.insert(*hoveredId);
			}
			else {
				selectedBoundaryIds.erase(it);
			}
		}
	}
}

void MeshInspector::handleMouse() {

	if (!ImGui::IsItemHovered()) return;

	ImGuiIO& io = ImGui::GetIO();

	// constantly update current mouse position and mouse index (cell centered, i think)
	currentMouseIndex = getMouseIndex(g.rFace,g.zFace);
	currentMousePos = gridToScreen((int)currentMouseIndex.x, (int)currentMouseIndex.y, g.rFace, g.zFace);

	handleItemButtonSelect();

	handleCursor(io);

	// handled regardless
	if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
		if (hoveredId.has_value() && mesh.selectedBoundaryIds.contains(*hoveredId)) {
			hoveringOverSelectedSegment = true;
		}
		else {
			hoveringOverSelectedSegment = false;
		}
		openPopUp = true;
	}

	// handle zooming in/out
	if (io.MouseWheel != 0.0f) {
		handleZoom(io);
	}

}

std::optional<int> MeshInspector::findHoveredBoundarySegment(
	const std::vector<double>& rFace,
	const std::vector<double>& zFace
) {
	ImVec2 mouse = ImGui::GetIO().MousePos;

	int bestId = -1;
	float bestDist = pickRadiusPx;

	for (const BoundarySegment& seg : mesh.boundarySegments) {
		ImVec2 p0 = gridToScreen(seg.a.j, seg.a.i, rFace, zFace);
		ImVec2 p1 = gridToScreen(seg.b.j, seg.b.i, rFace, zFace);

		float d = distPointToSegment(mouse, p0, p1);

		if (d < bestDist) {
			bestDist = d;
			bestId = seg.id;
		}
	}

	if (bestId == -1) {
		return std::nullopt;
	}

	return bestId;
}

void MeshInspector::copyActiveSurfaceToClipboard() {

	GLint oldFBO, oldViewport[4];
	ImVec2 oldDisplaySize, oldFramebufferSize;
	offScreenFBO.create2DBuffer(pendingCopyWidth, pendingCopyHeight, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
	offScreenFBO.beginOffScreenImGuiRender(oldFBO, oldViewport, oldDisplaySize, oldFramebufferSize);

	// build imgui draw commands
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::Begin("##ExportWindow", nullptr, UIFlags::TemporaryWindowFlags);

	ImGui::Image((ImTextureID)(intptr_t)frameBuffer.getTextureID(), ImVec2((float)imageWidth, (float)imageHeight), ImVec2(0.0, 1.0f), ImVec2(1.0f, 0.0f));
	drawBoundarySegments(g.rFace, g.zFace);
	drawHighlightedCells(g.obstacleIndices, g.zFace, g.rFace);

	ImGui::End();
	ImGui::PopStyleVar();

	offScreenFBO.endOffScreenImGuiRender(oldFBO, oldViewport, oldDisplaySize, oldFramebufferSize);
}

// ======================================================================
// -----------------------DRAW CALLS-------------------------------------
// ======================================================================
void MeshInspector::drawBoundarySegments(
	const std::vector<double>& rFace,
	const std::vector<double>& zFace
) {
	ImDrawList* drawList = ImGui::GetWindowDrawList();

	for (const BoundarySegment& seg : mesh.boundarySegments) {
		ImVec2 p0 = gridToScreen(seg.a.j, seg.a.i, rFace, zFace);
		ImVec2 p1 = gridToScreen(seg.b.j, seg.b.i, rFace, zFace);

		bool selected = mesh.selectedBoundaryIds.find(seg.id) != mesh.selectedBoundaryIds.end();

		bool hovered = hoveredId.has_value() && *hoveredId == seg.id;
			

		ImU32 color = IM_COL32(255, 80, 80, 255);
		float thickness = 2.0f;

		if (hovered) {
			color = IM_COL32(255, 255, 0, 255);
			thickness = 4.0f;
		}

		if (selected) {
			color = IM_COL32(0, 180, 255, 255);
			thickness = 4.0f;
		}

		drawList->AddLine(p0, p1, color, thickness);
	}
}

void MeshInspector::drawToolBar() {
	float toolbarHeight = 32.0f;

	ImGui::BeginChild("##toolbar", ImVec2(0.0f, toolbarHeight), false);

	addImageButtonResetView(assets.houseIcon, ImVec2(22.0f, 22.0f));
	setToolTip("Reset view");
	ImGui::SameLine();

	addImageButtonClearVector("##ClearPoints", assets.clearIcon, ImVec2(22.0f, 22.0f), points);
	setToolTip("Clear all selected points");
	ImGui::SameLine();

	addImageButtonRunCustomFuncs(
		"##Custom",
		assets.clearIcon,
		ImVec2(22.0f, 22.0f),
		[&]() { g.obstacleIndices.clear(); },
		[&]() { mesh.selectableOuterEdges.clear(); },
		[&]() { mesh.boundarySegments.clear(); }
	);

	setToolTip("Clear all selected cells");
	ImGui::SameLine();

	addImageButtonToggleBool("Select", assets.selectRegionIcon, ImVec2(22.0f, 22.0f), toggleSelect);
	setToolTip("Select");
	ImGui::SameLine();

	addImageButtonCopyToClipboard("Copy", assets.copyIcon, ImVec2(22.0f, 22.0f));
	setToolTip("Copy to clipboard");
	ImGui::SameLine();

	ImGui::EndChild();
	ImGui::Separator();
}

void MeshInspector::drawTextAtSurfacePoint() {

	ImVec2 imageMin = ImGui::GetItemRectMin();

	ImDrawList* drawList = ImGui::GetWindowDrawList();

	for (const SurfacePoint& point : points) {

		// convert grid index to normalized texture coordinate
		int j = static_cast<int>(point.dataPos.x);
		int i = static_cast<int>(point.dataPos.y);

		float u = static_cast<float>(g.zFace[j] / g.L);
		float v = static_cast<float>(g.rFace[i] / g.R);

		// skip points outside the current zoomed/panned view
		if (u < u0 || u > u1 || v < v0 || v > v1) {
			continue;
		}

		// convert normalized texture coordinate to image-local position
		float sx = (u - u0) / (u1 - u0);

		// use this if your image is drawn with ImVec2(u0, v1), ImVec2(u1, v0)
		float sy = (v1 - v) / (v1 - v0);

		ImVec2 screenPos(
			imageMin.x + sx * imageWidth,
			imageMin.y + sy * imageHeight
		);

		std::string label = std::format(
			"z: {:.2f}\nr: {:.2f}",
			point.vecValue.x,
			point.vecValue.y
		);

		drawList->AddCircleFilled(screenPos, circleRadius, IM_COL32(150, 150, 150, 255), 16);
		drawList->AddCircle(screenPos, circleRadius, IM_COL32(200, 200, 200, 255), 16, 1.0f);
		drawList->AddText(ImVec2(screenPos.x + 10.0f, screenPos.y), IM_COL32(255, 255, 255, 255), label.c_str());

	}
}



void MeshInspector::drawPopup() {

	if (openPopUp) {
		ImGui::OpenPopup("Mesh Inspector Popup");
		openPopUp = false;
	}

	bool openNamingPopup = false;

	if (ImGui::BeginPopup("Mesh Inspector Popup")) {

		//addMenuItem
		addMenuItemCopyToClipboard("Copy to clipboard");

		addMenuItemClearPoints("Clear points");

		addMenuItemResetView("Reset view");
		
		// draw naming menu item
		if (hoveringOverSelectedSegment) {
			if (ImGui::MenuItem("Name Segment")) {
				mesh.createBoundaryGroupFromSelection();

				if (!mesh.boundaryGroups.empty()) {

					BoundarySegmentGroup& group = mesh.boundaryGroups.back();

					mesh.namingBoundaryGroupID = group.id;

					std::snprintf(
						group.nameBuffer,
						sizeof(group.nameBuffer),
						"%s",
						group.name.c_str()
					);

					openNamingPopup = true;
				}
			}
		}
		ImGui::EndPopup();
	}

	// open naming popup
	if (openNamingPopup) {
		ImGui::OpenPopup("Naming Segment");
	}

	if (BoundarySegmentGroup* group = mesh.getBoundaryGroupByID(mesh.namingBoundaryGroupID)) {
		if (drawNamingPopup("Naming Segment", *group)) {
			mesh.namingBoundaryGroupID = -1;
		}
	}

}

void MeshInspector::renderPreview() {

	frameBuffer.bind();

	glClearColor(0.4f, 0.4f, 0.4f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	shader.use();

	float viewX0 = 2.0f * u0 - 1.0f;
	float viewX1 = 2.0f * u1 - 1.0f;

	float viewY0 = 2.0f * v0 - 1.0f;
	float viewY1 = 2.0f * v1 - 1.0f;

	
	shader.SetVec2("viewMin", viewX0, viewY0);
	shader.SetVec2("viewMax", viewX1, viewY1);

	vertexBuffer.bind();

	glLineWidth(1.0f); // change this value
	int lineVertexCount = (int)(mesh.gridLineVertices.size() / 2);
	glDrawArrays(GL_LINES, 0, lineVertexCount);
	glLineWidth(1.0f); // reset after drawing

	vertexBuffer.unbind();

	frameBuffer.unbind();

	ImGui::Image((ImTextureID)(intptr_t)frameBuffer.getTextureID(), ImVec2((float)imageWidth, (float)imageHeight), ImVec2(0.0, 1.0f), ImVec2(1.0f, 0.0f));

}
// ======================================================================
// -----------------------BUILDING SEGMENTS------------------------------
// ======================================================================
void MeshInspector::buildSegments() {

	// combine obstacle edges and domain edges to make conbined edges
	std::unordered_set<MeshEdge, MeshEdgeHash> combinedEdges = buildCombinedBoundaryEdges(mesh.selectableOuterEdges, g.obstacleIndices);

	// build the segments using the new combinedEdges
	mesh.boundarySegments = buildDisplayBoundaries(combinedEdges);

	debugCheckBoundaryMerge(combinedEdges, mesh.boundarySegments);

	// assign id
	for (int k = 0; k < (int)mesh.boundarySegments.size(); k++) {
		mesh.boundarySegments[k].id = k;
	}
}

std::unordered_set<MeshEdge, MeshEdgeHash>
MeshInspector::buildCombinedBoundaryEdges(
	const std::unordered_set<MeshEdge, MeshEdgeHash>& selectableOuterEdges,
	const std::unordered_set<int>& obstacleIndices
) {
	std::unordered_set<MeshEdge, MeshEdgeHash> combinedEdges;

	// Add obstacle edges, but skip obstacle edges that lie exactly on the domain boundary.
	for (const MeshEdge& e : mesh.selectableOuterEdges) {
		if (isDomainBoundaryEdge(e)) {
			continue;
		}

		combinedEdges.insert(e);
	}

	// Add domain edges only where the adjacent cell is not solid.
	std::unordered_set<MeshEdge, MeshEdgeHash> domainEdges =
		buildDomainBoundaryEdges();

	for (const MeshEdge& e : domainEdges) {
		if (domainEdgeTouchesSolid(e, obstacleIndices)) {
			continue;
		}

		combinedEdges.insert(e);
	}

	return combinedEdges;
}

std::unordered_set<MeshEdge, MeshEdgeHash> MeshInspector::buildDomainBoundaryEdges() const {
	std::unordered_set<MeshEdge, MeshEdgeHash> edges;

	// Top domain boundary: i = 0, j = 0..nzBase-1
	for (int j = 0; j < nzBase; j++) {
		edges.insert({
			EdgeOrient::Horizontal,
			0,
			j
			});
	}

	// Bottom domain boundary: i = nrBase, j = 0..nzBase-1
	for (int j = 0; j < nzBase; j++) {
		edges.insert({
			EdgeOrient::Horizontal,
			nrBase,
			j
			});
	}

	// Left domain boundary: j = 0, i = 0..nrBase-1
	for (int i = 0; i < nrBase; i++) {
		edges.insert({
			EdgeOrient::Vertical,
			i,
			0
			});
	}

	// Right domain boundary: j = nzBase, i = 0..nrBase-1
	for (int i = 0; i < nrBase; i++) {
		edges.insert({
			EdgeOrient::Vertical,
			i,
			nzBase
			});
	}

	return edges;
}

// ======================================================================
// -----------------------MAIN RENDER LOOP-------------------------------
// ======================================================================
void MeshInspector::render() {

	setBaseNrNz();

	ImGui::Begin("Mesh Inspector");

	vertexBuffer.bufferSubData(mesh.gridLineVertices.size() * sizeof(float), mesh.gridLineVertices.data());

	drawToolBar();
	resizeImage(0.0f, 0.0f);
	renderPreview();

	handleMouse();

	// generate id for hovered segments
	hoveredId = findHoveredBoundarySegment(g.rFace, g.zFace);
	if (hoveredId) {
		hoveringOverSegment = true;
	}
	else {
		hoveringOverSegment = false;
	}

	// create segments
	buildSegments();
	drawBoundarySegments(g.rFace, g.zFace);

	// draw highlighted cells and texts
	drawHighlightedCells(g.obstacleIndices, g.rFace, g.zFace);
	drawTextAtSurfacePoint();
	drawPopup();

	ImGui::End();
}