#include "mesh_inspector.h"

#include <format>
#include <algorithm>
#include <glm/glm.hpp>

#include "project.h"
#include "solver.h"
#include "mesh.h"
#include "colorbar.h"

#include "flag_manager.h"
#include "printer.h"
#include "math_func.h"

#include <iostream>

MeshInspector::MeshInspector(Project& project, AppAssets& assets) :
	solver(project.solver),
	mesh(project.mesh),
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
void MeshInspector::clearObstacles() {
	g.obstacleIndices.clear();
	syncAfterObstacleEdit();
}

void MeshInspector::removeObstacleCellsInRect(
	const ImVec2& start,
	const ImVec2& end
) {
	int j0 = std::clamp((int)std::min(start.x, end.x), 0, nzBase - 1);
	int j1 = std::clamp((int)std::max(start.x, end.x), 0, nzBase - 1);

	int i0 = std::clamp((int)std::min(start.y, end.y), 0, nrBase - 1);
	int i1 = std::clamp((int)std::max(start.y, end.y), 0, nrBase - 1);

	for (int i = i0; i <= i1; i++) {
		for (int j = j0; j <= j1; j++) {
			int cell = i * nzBase + j;
			removeObstacleCell(cell);
		}
	}
}

bool MeshInspector::removeObstacleCell(int cellIndex) {
	return g.obstacleIndices.erase(cellIndex) > 0;
}

bool MeshInspector::removeInvalidBoundaryGroups() {
	std::unordered_set<MeshEdge, MeshEdgeHash> validEdges =
		buildCombinedBoundaryEdges(mesh.selectableOuterEdges, g.obstacleIndices);

	std::size_t oldSize = mesh.boundaryGroups.size();

	mesh.boundaryGroups.erase(
		std::remove_if(
			mesh.boundaryGroups.begin(),
			mesh.boundaryGroups.end(),
			[&](const BoundaryGroup& group) {
				return !boundaryGroupStillValid(group, validEdges);
			}
		),
		mesh.boundaryGroups.end()
	);

	return mesh.boundaryGroups.size() != oldSize;
}

bool MeshInspector::boundaryGroupStillValid(
	const BoundaryGroup& group,
	const std::unordered_set<MeshEdge, MeshEdgeHash>& validEdges
) const {
	if (group.edges.empty()) {
		return false;
	}

	for (const MeshEdge& edge : group.edges) {
		if (validEdges.find(edge) == validEdges.end()) {
			return false;
		}
	}

	return true;
}

void MeshInspector::syncAfterObstacleEdit() {
	rebuildSelectableOuterEdges(g.obstacleIndices);

	removeInvalidBoundaryGroups();

	mesh.selectedBoundaryIDs.clear();
	mesh.highlightedBoundarySegmentIDs.clear();

	hoveredId.reset();
	obstacleError.clear();
}

void MeshInspector::tryAddObstacleCellsInRect(
	const ImVec2& start,
	const ImVec2& end
) {
	int j0 = std::clamp((int)std::min(start.x, end.x), 0, nzBase - 1);
	int j1 = std::clamp((int)std::max(start.x, end.x), 0, nzBase - 1);

	int i0 = std::clamp((int)std::min(start.y, end.y), 0, nrBase - 1);
	int i1 = std::clamp((int)std::max(start.y, end.y), 0, nrBase - 1);

	for (int i = i0; i <= i1; i++) {
		for (int j = j0; j <= j1; j++) {
			int cell = i * nzBase + j;
			tryAddObstacleCell(cell);
		}
	}
}

bool MeshInspector::tryAddObstacleCell(int cellIndex) {
	if (obstacleCellTouchesBoundaryGroup(cellIndex)) {
		obstacleError = "Obstacle cannot touch an existing boundary group.";
		return false;
	}

	addHighlightCell(g.obstacleIndices, cellIndex);
	obstacleError.clear();

	return true;
}

bool MeshInspector::obstacleCellTouchesBoundaryGroup(int cellIndex) const {
	int i = cellIndex / nzBase;
	int j = cellIndex % nzBase;

	if (!isInsideCellGrid(i, j)) {
		return true;
	}

	std::array<MeshEdge, 4> cellEdges = getCellEdges(i, j);

	for (const BoundaryGroup& group : mesh.boundaryGroups) {
		for (const MeshEdge& cellEdge : cellEdges) {
			auto it = std::find(
				group.edges.begin(),
				group.edges.end(),
				cellEdge
			);

			if (it != group.edges.end()) {
				return true;
			}
		}
	}

	return false;
}

std::array<MeshEdge, 4> MeshInspector::getCellEdges(int i, int j) const {
	return {
		MeshEdge{ EdgeOrient::Horizontal, i,     j     }, // top
		MeshEdge{ EdgeOrient::Horizontal, i + 1, j     }, // bottom
		MeshEdge{ EdgeOrient::Vertical,   i,     j     }, // left
		MeshEdge{ EdgeOrient::Vertical,   i,     j + 1 }  // right
	};
}

void MeshInspector::setGroupTotalLength(BoundaryGroup& group) {

	double totalLength = 0.0;

	for (const int& id : group.segmentIDs) {

		BoundarySegment* seg = mesh.getBoundarySegmentByID(id);


		double r0 = g.rFace[seg->a.i];
		double z0 = g.zFace[seg->a.j];

		double r1 = g.rFace[seg->b.i];
		double z1 = g.zFace[seg->b.j];

		totalLength += abs(r0 - r1);
		totalLength += abs(z0 - z1);

	}

	group.totalLength = totalLength;

}

void MeshInspector::setGroupOrientation(BoundaryGroup& group) {

	bool hasVertical = false;
	bool hasHorizontal = false;

	for (const MeshEdge& edge : group.edges) {

		if (edge.orient == EdgeOrient::Horizontal) {
			hasHorizontal = true;
		}
		else if (edge.orient == EdgeOrient::Vertical) {
			hasVertical = true;
		}

		if (hasHorizontal && hasVertical) {
			group.includesOrientation = EdgeOrient::Both;
			return;
		}
	}

	if (hasHorizontal) {
		group.includesOrientation = EdgeOrient::Horizontal;
	}
	else if (hasVertical) {
		group.includesOrientation = EdgeOrient::Vertical;
	}
}

void MeshInspector::fillBoundaryGroupEdges(BoundaryGroup& group) {
	group.edges.clear();

	std::unordered_set<MeshEdge, MeshEdgeHash> uniqueEdges;

	for (int selectedID : group.segmentIDs) {

		auto it = std::find_if(
			mesh.boundarySegments.begin(),
			mesh.boundarySegments.end(),
			[&](const BoundarySegment& seg) {
				return seg.id == selectedID;
			}
		);

		if (it == mesh.boundarySegments.end()) {
			continue;
		}

		std::vector<MeshEdge> edges = edgesFromBoundarySegment(*it);

		for (const MeshEdge& edge : edges) {
			uniqueEdges.insert(edge);
		}
	}

	group.edges.assign(uniqueEdges.begin(), uniqueEdges.end());
}

std::vector<MeshEdge> MeshInspector::edgesFromBoundarySegment(
	const BoundarySegment& seg
) const {
	std::vector<MeshEdge> edges;

	// Horizontal segment
	if (seg.a.i == seg.b.i) {
		int i = seg.a.i;

		int j0 = std::min(seg.a.j, seg.b.j);
		int j1 = std::max(seg.a.j, seg.b.j);

		for (int j = j0; j < j1; j++) {
			edges.push_back({
				EdgeOrient::Horizontal,
				i,
				j
				});
		}
	}

	// Vertical segment
	else if (seg.a.j == seg.b.j) {
		int j = seg.a.j;

		int i0 = std::min(seg.a.i, seg.b.i);
		int i1 = std::max(seg.a.i, seg.b.i);

		for (int i = i0; i < i1; i++) {
			edges.push_back({
				EdgeOrient::Vertical,
				i,
				j
				});
		}
	}

	return edges;
}

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
    if (!toggleDrawCell) return;

    int cell = (int)currentMouseIndex.y * nzBase + (int)currentMouseIndex.x;

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        tryAddObstacleCell(cell);
        initMouseIndex = currentMouseIndex;
    }

    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {

        if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl)) {
            tryAddObstacleCellsInRect(initMouseIndex, currentMouseIndex);
        }
        else {
            tryAddObstacleCell(cell);
        }
    }

	if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
		syncAfterObstacleEdit();
	}
}

void MeshInspector::handleItemButtonRemove() {
	if (!toggleRemoveCell) return;

	int cell = (int)currentMouseIndex.y * nzBase + (int)currentMouseIndex.x;

	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		initMouseIndex = currentMouseIndex;
		removeObstacleCell(cell);
	}

	if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
		if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl)) {
			removeObstacleCellsInRect(initMouseIndex, currentMouseIndex);
		}
		else {
			removeObstacleCell(cell);
		}
	}

	if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
		syncAfterObstacleEdit();
	}
}

void MeshInspector::handleItemButtonConnecting() {

	if (!toggleConnecting) return;


}

void MeshInspector::handleCursor(ImGuiIO& io) {

	if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
		handlePan(io);
	}

	// do not run this if any of the toggled tools are active, or if a popup is opened
	isPopupOpened = ImGui::IsPopupOpen("Mesh Inspector Popup");
	if (toggleDrawCell || toggleConnecting || toggleRemoveCell || toggleRuler || isPopupOpened) return;




	if (!hoveredId.has_value()) {
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {

			mesh.selectedBoundaryIDs.clear();
			mesh.highlightedBoundarySegmentIDs.clear();
		}
	}
	else {

		if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
			std::unordered_set<int>& selectedBoundaryIDs = mesh.selectedBoundaryIDs;

			auto it = selectedBoundaryIDs.find(*hoveredId);

			if (it == selectedBoundaryIDs.end()) {
				selectedBoundaryIDs.insert(*hoveredId);
			}
			else {
				selectedBoundaryIDs.erase(it);
			}
		}
		else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
			mesh.selectedBoundaryIDs.clear();
			std::unordered_set<int>& selectedBoundaryIDs = mesh.selectedBoundaryIDs;

			auto it = selectedBoundaryIDs.find(*hoveredId);

			if (it == selectedBoundaryIDs.end()) {
				selectedBoundaryIDs.insert(*hoveredId);
			}
			else {
				selectedBoundaryIDs.erase(it);
			}
		}
	}
}

void MeshInspector::handleMouse() {

	ImGuiIO& io = ImGui::GetIO();

	ImVec2 imageMin = ImGui::GetItemRectMin();
	ImVec2 imageMax = ImGui::GetItemRectMax();
	float clickPadding = 10.0f;

	ImVec2 hitMin = ImVec2(imageMin.x - clickPadding, imageMin.y - clickPadding);
	ImVec2 hitMax = ImVec2(imageMax.x + clickPadding, imageMax.y + clickPadding);

	ImVec2 mouse = ImGui::GetMousePos();

	bool mouseNearImage =
		mouse.x >= hitMin.x && mouse.x <= hitMax.x &&
		mouse.y >= hitMin.y && mouse.y <= hitMax.y;

	if (!mouseNearImage) return;

	// constantly update current mouse position and mouse index (cell centered, i think)
	currentMouseIndex = getMouseIndex(g.rFace,g.zFace);
	currentMousePos = gridToScreen((int)currentMouseIndex.x, (int)currentMouseIndex.y, g.rFace, g.zFace);

	handleItemButtonSelect();

	handleItemButtonRemove();
	handleCursor(io);

	// handled regardless
	if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {

		if (hoveredId.has_value() && mesh.selectedBoundaryIDs.contains(*hoveredId)) {
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
	
	ImDrawList* drawList = ImGui::GetWindowDrawList();

	drawBoundarySegments(drawList, g.rFace, g.zFace);
	drawHighlightedCells(drawList, g.obstacleIndices, g.zFace, g.rFace);

	ImGui::End();
	ImGui::PopStyleVar();

	offScreenFBO.endOffScreenImGuiRender(oldFBO, oldViewport, oldDisplaySize, oldFramebufferSize);
}

// ======================================================================
// -----------------------DRAW CALLS-------------------------------------
// ======================================================================
void MeshInspector::drawBoundarySegments(
	ImDrawList* drawList,
	const std::vector<double>& rFace,
	const std::vector<double>& zFace
) {

	for (const BoundarySegment& seg : mesh.boundarySegments) {
		ImVec2 p0 = gridToScreen(seg.a.j, seg.a.i, rFace, zFace);
		ImVec2 p1 = gridToScreen(seg.b.j, seg.b.i, rFace, zFace);

		bool selected = mesh.selectedBoundaryIDs.find(seg.id) != mesh.selectedBoundaryIDs.end();

		bool hovered = hoveredId.has_value() && *hoveredId == seg.id;
			
		bool highlighted = mesh.highlightedBoundarySegmentIDs.find(seg.id) != mesh.highlightedBoundarySegmentIDs.end();


		ImU32 color = IM_COL32(203, 209, 224, 255);
		float thickness = 3.0f;

		if (hovered) {
			color = IM_COL32(255, 255, 0, 255);
			thickness = 4.0f;
		}

		if (highlighted) {
			color = IM_COL32(255, 80, 80, 255);
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
	float toolbarHeight = 40.0f;

	ImGui::BeginChild("##toolbar", ImVec2(0.0f, toolbarHeight), false);

	if (addImageButtonToggle("Ruler", "Ruler", assets.rulerIcon, buttonSize, toggleRuler)) {
		toggleDrawCell = toggleRemoveCell = false;
	}
	ImGui::SameLine();

	if (addImageButtonToggle("Fill", "Fill cells", assets.fillCellIcon, buttonSize, toggleFillCells)) {

	}
	ImGui::SameLine();

	if (addImageButton("Reset", "Reset View", assets.houseIcon, buttonSize)) {
		resetView();
	}
	ImGui::SameLine();

	if (addImageButton("Erase All", "Erase All", assets.clearIcon, buttonSize)) {
		clearObstacles();
	}
	ImGui::SameLine();

	if (addImageButtonToggle("Erase", "Erase", assets.eraseIcon, buttonSize, toggleRemoveCell)) {
		toggleDrawCell = toggleRuler = false;
	}
	ImGui::SameLine();

	if (addImageButtonToggle("Draw", "Draw", assets.selectRegionIcon, buttonSize, toggleDrawCell)) {
		toggleRemoveCell = toggleRuler = false;
	}
	ImGui::SameLine();

	if (addImageButton("Copy", "Copy to clipboard", assets.copyIcon, buttonSize) || consoleCopy) {
		pendingCopyWidth = frameBuffer.width;
		pendingCopyHeight = frameBuffer.height;
		pendingCopy = true;
		consoleCopy = false;
	}
	ImGui::SameLine();

	ImGui::EndChild();
}

void MeshInspector::drawTextAtSurfacePoint(ImDrawList* drawList) {

	ImVec2 imageMin = ImGui::GetItemRectMin();

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

		if (ImGui::MenuItem("Reset View")) {
			resetView();
		}
		

		// draw naming menu item
		if (hoveringOverSelectedSegment) {

			if (ImGui::MenuItem("Name Segment")) {

				pendingBoundaryGroup = mesh.createBoundaryGroupFromSelection();
				pendingBoundaryGroupBC = mesh.createBoundaryGroupBCFromID(pendingBoundaryGroup->id);

				if (pendingBoundaryGroup) {

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

	//for (const BoundaryGroup& group : mesh.boundaryGroups) {
	//	printf("%s\n", group.name);
	//}

	// add new boundary group
	if (pendingBoundaryGroup) {
		//check();
		if (drawNamingPopup("Naming Segment", *pendingBoundaryGroup, mesh.boundaryGroups)) {

			fillBoundaryGroupEdges(*pendingBoundaryGroup);

			setGroupOrientation(*pendingBoundaryGroup);

			setGroupTotalLength(*pendingBoundaryGroup);

			printf(
				"new group edge count = %zu\n",
				pendingBoundaryGroup->edges.size()
			);

			if (!pendingBoundaryGroup->edges.empty()) {

				mesh.boundaryGroups.push_back(std::move(*pendingBoundaryGroup));
				solver.boundaryGroupBCs.push_back(std::move(*pendingBoundaryGroupBC));

				printf(
					"boundary group count = %zu\n",
					mesh.boundaryGroups.size()
				);
			}

			pendingBoundaryGroup.reset();
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

	// add obstacle edges, but skip obstacle edges that lie exactly on the domain boundary.
	for (const MeshEdge& e : selectableOuterEdges) {
		if (isDomainBoundaryEdge(e)) {
			continue;
		}

		combinedEdges.insert(e);
	}

	// add domain edges only where the adjacent cell is not solid.
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
bool MeshInspector::deleteBoundaryGroupByID(int groupID) {
	auto& groups = mesh.boundaryGroups;

	auto it = std::remove_if(
		groups.begin(),
		groups.end(),
		[&](const BoundaryGroup& group) {
			return group.id == groupID;
		}
	);

	if (it == groups.end()) {
		return false; // no group with this ID was found
	}

	groups.erase(it, groups.end());

	// Clear temporary UI state that may refer to old boundary segments/groups
	mesh.selectedBoundaryIDs.clear();
	mesh.highlightedBoundarySegmentIDs.clear();

	// If you were naming/editing this group, cancel it
	if (pendingBoundaryGroup && pendingBoundaryGroup->id == groupID) {
		pendingBoundaryGroup.reset();
	}

	// Optional: clear error text
	obstacleError.clear();

	return true;
}

void MeshInspector::drawStatusBar() {

	ImGui::Text(
		"Hello"
	);

}

void MeshInspector::render() {
	setBaseNrNz();

	ImGui::Begin("Mesh Inspector");
	vertexBuffer.bufferSubData(
		mesh.gridLineVertices.size() * sizeof(float),
		mesh.gridLineVertices.data()
	);

	ImDrawList* drawList = ImGui::GetWindowDrawList();

	drawToolBar();

	ImVec2 pos = ImGui::GetCursorScreenPos();
	ImVec2 size = ImGui::GetContentRegionAvail();

	Rect canvasRect = makePaddedRect(
		pos,
		size,
		0.0f,
		0.0f,
		0.0f,
		0.0f
	);

	Rect surfaceRect = makePaddedRect(
		pos,
		size,
		20.0f,
		20.0f,
		50.0f,
		50.0f
	);

	resizeImage(surfaceRect.size());

	// draw canvas first, then the image on top
	drawCanvas(drawList, canvasRect, 5.0f);

	renderPreview();

	drawSurface(surfaceRect);

	//ImGui::SetCursorScreenPos()

	//drawStatusBar();

	// Build current segments before hover/mouse logic
	buildSegments();

	// Update hover before mouse logic
	hoveredId = findHoveredBoundarySegment(g.rFace, g.zFace);
	hoveringOverSegment = hoveredId.has_value();

	// Now handle mouse using current hoveredId/current segments
	handleMouse();

	// If the mouse changed obstacles, this rebuilds before drawing
	buildSegments();

	ImVec2 imageMin = ImGui::GetItemRectMin();
	ImVec2 imageMax = ImGui::GetItemRectMax();


	drawList->PushClipRect(imageMin, imageMax, true);

	drawHighlightedCells(drawList, g.obstacleIndices, g.rFace, g.zFace);
	drawBoundarySegments(drawList, g.rFace, g.zFace);

	drawTextAtSurfacePoint(drawList);
	drawPopup();

	drawList->PopClipRect();




	ImGui::End();
}