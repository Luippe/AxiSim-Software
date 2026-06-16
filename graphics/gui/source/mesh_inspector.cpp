#include "mesh_inspector.h"

#include <format>
#include <algorithm>
#include <glm/glm.hpp>

#include "mesh.h"
#include "colorbar.h"

#include "flag_manager.h"
#include "printer.h"
#include "math_func.h"

#include <iostream>

MeshInspector::MeshInspector(Mesh& mesh, AppConfig& appConfig) :
	mesh(mesh),
	g(mesh.g),
	assets(appConfig.assets),
	BaseSurfaceViewer("graphics/shaders/mesh.vert", "graphics/shaders/mesh.frag") {

	// radial location
	frameBuffer.create2DBuffer(500, 500, GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE);
	createGridBuffer();
}

// ======================================================================
// -----------------------HELPER FUNCTIONS-------------------------------
// ======================================================================
void MeshInspector::updateGridBuffer() {
	GLsizeiptr newBytes =
		(GLsizeiptr)(mesh.gridLineVertices.size() * sizeof(float));

	if (newBytes <= 0) {
		return;
	}

	if (newBytes != gridLineBufferBytes) {
		vertexBuffer.bind();
		glBufferData(
			GL_ARRAY_BUFFER,
			newBytes,
			mesh.gridLineVertices.data(),
			GL_DYNAMIC_DRAW
		);

		vertexBuffer.enableAttribute(
			0,
			2,
			GL_FLOAT,
			2 * sizeof(float),
			(void*)0
		);

		vertexBuffer.unbind();

		gridLineBufferBytes = newBytes;
	}
	else {
		vertexBuffer.bufferSubData(
			newBytes,
			mesh.gridLineVertices.data()
		);
	}
}

int addBoundaryVertexFromGrid(
	std::vector<BoundaryVertex>& vertices,
	GridVertex grid,
	const std::vector<double>& rFace,
	const std::vector<double>& zFace
) {
	if (grid.i < 0 || grid.i >= static_cast<int>(rFace.size()) ||
		grid.j < 0 || grid.j >= static_cast<int>(zFace.size())) {

		std::cout << "Invalid boundary vertex grid index: "
			<< "i=" << grid.i
			<< ", j=" << grid.j
			<< ", rFace.size=" << rFace.size()
			<< ", zFace.size=" << zFace.size()
			<< "\n";

		return -1;
	}

	BoundaryVertex vertex;
	vertex.id = static_cast<int>(vertices.size());
	vertex.grid = grid;
	vertex.hasGridVertex = true;
	vertex.pos = Vec2{
		zFace[grid.j],
		rFace[grid.i]
	};

	vertices.push_back(vertex);

	return vertex.id;
}



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
			[&](const BoundarySegmentGroup& group) {
				return !boundaryGroupStillValid(group, validEdges);
			}
		),
		mesh.boundaryGroups.end()
	);

	return mesh.boundaryGroups.size() != oldSize;
}

bool MeshInspector::boundaryGroupStillValid(
	const BoundarySegmentGroup& group,
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

	for (const BoundarySegmentGroup& group : mesh.boundaryGroups) {
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

void MeshInspector::setGroupTotalLength(BoundarySegmentGroup& group) {
	double totalLength = 0.0;

	for (int segmentID : group.segmentIDs) {
		BoundarySegment* seg = mesh.getBoundarySegmentByID(segmentID);

		if (!seg) {
			continue;
		}

		for (int edgeID : seg->edgeIDs) {
			if (edgeID < 0 ||
				edgeID >= static_cast<int>(mesh.boundaryEdges.size())) {
				continue;
			}

			const BoundaryEdge& edge = mesh.boundaryEdges[edgeID];

			if (!edgeInRange(edge, mesh.boundaryVertices.size())) continue;

			const Vec2& p0 = mesh.boundaryVertices[edge.v0].pos;
			const Vec2& p1 = mesh.boundaryVertices[edge.v1].pos;

			double dz = p1.z - p0.z;
			double dr = p1.r - p0.r;

			totalLength += std::sqrt(dz * dz + dr * dr);
		}
	}

	group.totalLength = static_cast<float>(totalLength);
}

void MeshInspector::setGroupOrientation(BoundarySegmentGroup& group) {

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

void MeshInspector::fillBoundaryGroupEdges(BoundarySegmentGroup& group) {
	group.edges.clear();

	std::unordered_set<MeshEdge, MeshEdgeHash> uniqueEdges;

	for (int segmentID : group.segmentIDs) {
		BoundarySegment* seg = mesh.getBoundarySegmentByID(segmentID);

		if (!seg) {
			continue;
		}

		for (int edgeID : seg->edgeIDs) {
			if (edgeID < 0 ||
				edgeID >= static_cast<int>(mesh.boundaryEdges.size())) {
				continue;
			}

			const BoundaryEdge& edge = mesh.boundaryEdges[edgeID];

			if (edge.hasMeshEdge) {
				uniqueEdges.insert(edge.meshEdge);
			}
		}
	}

	group.edges.assign(uniqueEdges.begin(), uniqueEdges.end());
}

bool MeshInspector::isDomainBoundaryEdge(const MeshEdge& e) const {
	if (e.orient == EdgeOrient::Horizontal) {
		return e.i == 0 || e.i == nrBase;
	}
	else {
		return e.j == 0 || e.j == nzBase;
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
	const std::unordered_set<MeshEdge, MeshEdgeHash>& edges,
	std::vector<BoundaryVertex>& boundaryVertices,
	std::vector<BoundaryEdge>& boundaryEdges,
	const std::vector<double>& rFace,
	const std::vector<double>& zFace
) {
	std::vector<BoundarySegment> segments;

	if (edges.empty()) {
		return segments;
	}

	if (rFace.empty() || zFace.empty()) {
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
		fixed = first.i;
		start = first.j;
		prev = first.j;
	}
	else {
		fixed = first.j;
		start = first.i;
		prev = first.i;
	}

	auto pushSegment = [&]() {
		GridVertex a;
		GridVertex b;

		if (currentOrient == EdgeOrient::Horizontal) {
			a = GridVertex{ fixed, start };
			b = GridVertex{ fixed, prev + 1 };
		}
		else {
			a = GridVertex{ start,     fixed };
			b = GridVertex{ prev + 1, fixed };
		}

		int v0 = addBoundaryVertexFromGrid(
			boundaryVertices,
			a,
			rFace,
			zFace
		);

		int v1 = addBoundaryVertexFromGrid(
			boundaryVertices,
			b,
			rFace,
			zFace
		);

		// Very important: do not create invalid segments.
		if (v0 < 0 || v1 < 0) {
			return;
		}

		BoundarySegment seg{};
		seg.id = static_cast<int>(segments.size());
		seg.groupID = -1;
		seg.loopID = -1;
		seg.source = BoundarySource::Domain;

		for (int p = start; p <= prev; p++) {
			MeshEdge meshEdge{};

			if (currentOrient == EdgeOrient::Horizontal) {
				meshEdge = MeshEdge{
					EdgeOrient::Horizontal,
					fixed,
					p
				};
			}
			else {
				meshEdge = MeshEdge{
					EdgeOrient::Vertical,
					p,
					fixed
				};
			}

			GridVertex a = edgeStart(meshEdge);
			GridVertex b = edgeEnd(meshEdge);

			int v0 = addBoundaryVertexFromGrid(
				boundaryVertices,
				a,
				rFace,
				zFace
			);

			int v1 = addBoundaryVertexFromGrid(
				boundaryVertices,
				b,
				rFace,
				zFace
			);

			if (v0 < 0 || v1 < 0) {
				continue;
			}

			int edgeID = static_cast<int>(boundaryEdges.size());

			BoundaryEdge edge{};
			edge.id = edgeID;
			edge.v0 = v0;
			edge.v1 = v1;
			edge.segmentID = seg.id;
			edge.groupID = -1;
			edge.source = BoundarySource::Domain;
			edge.hasMeshEdge = true;
			edge.meshEdge = meshEdge;

			boundaryEdges.push_back(edge);
			seg.edgeIDs.push_back(edgeID);
		}

		if (!seg.edgeIDs.empty()) {
			segments.push_back(seg);
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
	gridLineBufferBytes =
		(GLsizeiptr)(mesh.gridLineVertices.size() * sizeof(float));

	vertexBuffer.createBuffer(
		gridLineBufferBytes,
		mesh.gridLineVertices.data()
	);

	vertexBuffer.bind();
	vertexBuffer.enableAttribute(
		0,
		2,
		GL_FLOAT,
		2 * sizeof(float),
		(void*)0
	);
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
    if (!toggleDrawRect) return;

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

void MeshInspector::handleCursor(ImGuiIO& io) {

	// do not run this if any of the toggled tools are active, or if a popup is opened
	isPopupOpened = ImGui::IsPopupOpen("Mesh Inspector Popup");
	if (toggleDrawRect ||  toggleRemoveCell || toggleRuler || isPopupOpened) return;

	if (!hoveredId.has_value()) {
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {

			mesh.selectedBoundaryIDs.clear();
			mesh.highlightedBoundarySegmentIDs.clear();
		}
		return;
	}
	else {

		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
			if (!ImGui::IsKeyDown(ImGuiKey_LeftCtrl))
				mesh.selectedBoundaryIDs.clear();
			auto& sel = mesh.selectedBoundaryIDs;
			auto it = sel.find(*hoveredId);
			if (it == sel.end()) sel.insert(*hoveredId);
			else sel.erase(it);
		}
	}
}

void MeshInspector::handleDrawCircle() {

	if (!toggleDrawCircle || mesh.currentMeshType == MeshType::Structured) return;


	if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
		Vec2 initialPhysical = getMousePhysicalCoord(initLeftMouse, mesh.g.R, mesh.g.L);
		Vec2 currentPhysical = getMousePhysicalCoord(currentMousePos, mesh.g.R, mesh.g.L);
		pendingCircle.pending = true;
		pendingCircle.radius = distance(initialPhysical, currentPhysical);
	}

	if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
		pendingCircle.pending = false;
		Vec2 physical = getMousePhysicalCoord(initLeftMouse, mesh.g.R, mesh.g.L);
		mesh.addCircularObstacle(physical, pendingCircle.radius, 80);
	}
}

void MeshInspector::handleDrawRectangle() {

}

bool isMouseNearImage(ImGuiIO& io) {


	ImVec2 imageMin = ImGui::GetItemRectMin();
	ImVec2 imageMax = ImGui::GetItemRectMax();

	float clickPadding = 10.0f;

	ImVec2 hitMin = ImVec2(imageMin.x - clickPadding, imageMin.y - clickPadding);
	ImVec2 hitMax = ImVec2(imageMax.x + clickPadding, imageMax.y + clickPadding);

	ImVec2 mouse = ImGui::GetMousePos();

	bool mouseNearImage =
		mouse.x >= hitMin.x && mouse.x <= hitMax.x &&
		mouse.y >= hitMin.y && mouse.y <= hitMax.y;

	return mouseNearImage;
}

void MeshInspector::handleOpenPopup() {
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
}

void MeshInspector::handleMouse() {

	ImGuiIO& io = ImGui::GetIO();

	// if mouse is not near the image, then dont handle any mouse events
	if (!isMouseNearImage(io)) return;
	
	// update the initial mouse position where left click was pressed
	updateInitialLeftClick(io);

	handleOpenPopup();

	handleDrawCircle();

	// handle zooming in/out
	if (io.MouseWheel != 0.0f) {
		handleZoom(io);
	}

	if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
		handlePan(io);
	}

	handleCursor(io);



	if (mesh.currentMeshType == MeshType::Structured) {
		// constantly update current mouse position and mouse index (cell centered, i think)
		currentMouseIndex = getMouseIndex(g.rFace, g.zFace);
		currentMousePos = gridToScreen((int)currentMouseIndex.x, (int)currentMouseIndex.y, g.rFace, g.zFace);

		handleItemButtonSelect();

		handleItemButtonRemove();
	}
	else if (mesh.currentMeshType == MeshType::Unstructured) {

	}
}


std::optional<int> MeshInspector::findHoveredBoundarySegment() {
	ImVec2 mouse = ImGui::GetIO().MousePos;

	int bestSegmentID = -1;
	float bestDist = pickRadiusPx;

	for (const BoundaryEdge& edge : mesh.boundaryEdges) {
		if (edge.v0 < 0 || edge.v1 < 0) {
			continue;
		}

		if (!edgeInRange(edge, mesh.boundaryVertices.size())) continue;

		const Vec2& p0World = mesh.boundaryVertices[edge.v0].pos;
		const Vec2& p1World = mesh.boundaryVertices[edge.v1].pos;

		ImVec2 p0 = physicalToScreen(p0World, g.L, g.R);
		ImVec2 p1 = physicalToScreen(p1World, g.L, g.R);

		float d = distPointToSegment(mouse, p0, p1);

		if (d < bestDist) {
			bestDist = d;
			bestSegmentID = edge.segmentID;
		}
	}

	if (bestSegmentID < 0) {
		return std::nullopt;
	}

	return bestSegmentID;
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

	//drawBoundarySegments(drawList, g.rFace, g.zFace);
	drawHighlightedCells(drawList, g.obstacleIndices, g.zFace, g.rFace);

	ImGui::End();
	ImGui::PopStyleVar();

	offScreenFBO.endOffScreenImGuiRender(oldFBO, oldViewport, oldDisplaySize, oldFramebufferSize);
}

// ======================================================================
// -----------------------DRAW CALLS-------------------------------------
// ======================================================================
void MeshInspector::drawBoundarySegments(
	ImDrawList* drawList
) {
	//printSize(mesh.boundarySegments);
	for (const BoundarySegment& seg : mesh.boundarySegments) {
		bool selected =
			mesh.selectedBoundaryIDs.find(seg.id) !=
			mesh.selectedBoundaryIDs.end();

		bool hovered =
			hoveredId.has_value() && *hoveredId == seg.id;

		bool highlighted =
			mesh.highlightedBoundarySegmentIDs.find(seg.id) !=
			mesh.highlightedBoundarySegmentIDs.end();

		ImU32 color = drawingColor;
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

		for (int edgeID : seg.edgeIDs) {
			if (edgeID < 0 ||
				edgeID >= static_cast<int>(mesh.boundaryEdges.size())) {
				continue;
			}

			const BoundaryEdge& edge = mesh.boundaryEdges[edgeID];

			if (!edgeInRange(edge, mesh.boundaryVertices.size())) continue;

			Vec2 p0World = mesh.boundaryVertices[edge.v0].pos;
			Vec2 p1World = mesh.boundaryVertices[edge.v1].pos;

			ImVec2 p0 = physicalToScreen(p0World, g.L, g.R);
			ImVec2 p1 = physicalToScreen(p1World, g.L, g.R);

			drawList->AddLine(p0, p1, color, thickness);
		}
	}
}

void MeshInspector::drawToolBar() {
	float toolbarHeight = 40.0f;

	ImGui::BeginChild("##toolbar", ImVec2(0.0f, toolbarHeight), false);

	if (addImageButtonToggle("Ruler", "Ruler", assets.rulerIcon, buttonSize, toggleRuler)) {
		toggleDrawRect = toggleRemoveCell = false;
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
		toggleDrawRect = toggleRuler = false;
	}
	ImGui::SameLine();


	if (addImageButtonToggle("DrawRect", "Draw Rectangle", assets.selectRegionIcon, buttonSize, toggleDrawRect)) {
		toggleRemoveCell = toggleRuler = false;
	}
	ImGui::SameLine();

	if (addImageButtonToggle("DrawCircle", "Draw Circle", assets.drawCircleIcon, buttonSize, toggleDrawCircle)) {
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

	ImGui::Checkbox("Mesh", &mesh.meshMode);

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

	//for (const BoundarySegmentGroup& group : mesh.boundaryGroups) {
	//	printf("%s\n", group.name);
	//}

	// add new boundary group
	if (pendingBoundaryGroup) {
		//check();
		if (drawNamingPopup("Naming Segment", *pendingBoundaryGroup, mesh.boundaryGroups)) {

			if (mesh.currentMeshType == MeshType::Structured) {
				fillBoundaryGroupEdges(*pendingBoundaryGroup);
				setGroupOrientation(*pendingBoundaryGroup);
			}
			else {
				pendingBoundaryGroup->edges.clear();
				pendingBoundaryGroup->includesOrientation = EdgeOrient::Both;
			}

			setGroupTotalLength(*pendingBoundaryGroup);

			printf(
				"new group edge count = %zu\n",
				pendingBoundaryGroup->edges.size()
			);

			if (!pendingBoundaryGroup->segmentIDs.empty()) {
				mesh.boundaryGroups.push_back(std::move(*pendingBoundaryGroup));
			

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

	if (mesh.meshMode) {
		int lineVertexCount = (int)(mesh.gridLineVertices.size() / 2);
		glDrawArrays(GL_LINES, 0, lineVertexCount);

	}

	glLineWidth(1.0f); // reset after drawing

	vertexBuffer.unbind();

	frameBuffer.unbind();

}

// ======================================================================
// -----------------------BUILDING SEGMENTS------------------------------
// ======================================================================
void MeshInspector::buildSegments() {
	std::unordered_set<MeshEdge, MeshEdgeHash> combinedEdges =
		buildCombinedBoundaryEdges(mesh.selectableOuterEdges, g.obstacleIndices);

	mesh.boundaryVertices.clear();
	mesh.boundaryEdges.clear();

	mesh.boundarySegments = buildDisplayBoundaries(
		combinedEdges,
		mesh.boundaryVertices,
		mesh.boundaryEdges,
		g.rFace,
		g.zFace
	);
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
		[&](const BoundarySegmentGroup& group) {
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

void MeshInspector::drawPendingObjects(ImDrawList* drawList) {

	if (pendingCircle.pending) {

		float radiusPx = static_cast<float>(
			pendingCircle.radius * imageSize.x / ((u1 - u0) * g.L)
			);

		drawList->AddCircle(initLeftMouse, radiusPx, drawingColor, 80, 3.0f);

	}
}

void MeshInspector::drawStatusBar() {

	ImGui::Text(
		"Hello"
	);

}

void MeshInspector::render() {
	setBaseNrNz();

	ImGui::Begin("Mesh Inspector");
	updateGridBuffer();

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

	// Build current segments before hover/mouse logic
	if (mesh.currentMeshType == MeshType::Structured) {
		buildSegments();
	}

	// update current global mouse pos
	updateCurrentMousePos();

	// Update hover before mouse logic
	hoveredId = findHoveredBoundarySegment();
	hoveringOverSegment = hoveredId.has_value();

	// Now handle mouse using current hoveredId/current segments
	handleMouse();


	// If the mouse changed obstacles, this rebuilds before drawing
	if (mesh.currentMeshType == MeshType::Structured) {
		buildSegments();
	}

	drawList->PushClipRect(imageMin, imageMax, true);

	drawPendingObjects(drawList);
	drawHighlightedCells(drawList, g.obstacleIndices, g.rFace, g.zFace);
	drawBoundarySegments(drawList);

	drawTextAtSurfacePoint(drawList);
	drawPopup();

	drawList->PopClipRect();


	ImGui::End();
}