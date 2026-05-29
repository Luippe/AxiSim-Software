#include "mesh_inspector.h"

#include <format>
#include <algorithm>
#include <glm/glm.hpp>

#include "scene_view.h"
#include "colorbar.h"

#include "flag_manager.h"
#include "printer.h"
#include "math_func.h"


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
bool isAxisAligned(const GridVertex& a, const GridVertex& b) {
	return a.i == b.i || a.j == b.j;
}

GridVertexEdge makeUndirectedEdge(GridVertex a, GridVertex b) {
	// canonical ordering
	if (a.i > b.i || (a.i == b.i && a.j > b.j)) {
		std::swap(a, b);
	}

	return { a, b };
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

void MeshInspector::rebuildSelectableOuterEdges(
	const std::unordered_set<int>& obstacleIndices
) {
	selectableOuterEdges.clear();

	for (int n : obstacleIndices) {
		if (n < 0 || n >= nrBase * nzBase) {
			continue;
		}

		int i = n / nzBase;
		int j = n % nzBase;

		// Top face of solid cell
		if (!isSolidCell(i - 1, j, obstacleIndices)) {
			selectableOuterEdges.insert({
				EdgeOrient::Horizontal,
				i,
				j
				});
		}

		// Bottom face of solid cell
		if (!isSolidCell(i + 1, j, obstacleIndices)) {
			selectableOuterEdges.insert({
				EdgeOrient::Horizontal,
				i + 1,
				j
				});
		}

		// Left face of solid cell
		if (!isSolidCell(i, j - 1, obstacleIndices)) {
			selectableOuterEdges.insert({
				EdgeOrient::Vertical,
				i,
				j
				});
		}

		// Right face of solid cell
		if (!isSolidCell(i, j + 1, obstacleIndices)) {
			selectableOuterEdges.insert({
				EdgeOrient::Vertical,
				i,
				j + 1
				});
		}
	}
}

BoundarySegment meshEdgeToSegment(const MeshEdge& e) {
	if (e.orient == EdgeOrient::Horizontal) {
		return {
			GridVertex{ e.i, e.j },
			GridVertex{ e.i, e.j + 1 }
		};
	}
	else {
		return {
			GridVertex{ e.i,     e.j },
			GridVertex{ e.i + 1, e.j }
		};
	}
}

using BoundaryGraph =
std::unordered_map<GridVertex, std::vector<GridVertex>, GridVertexHash>;

BoundaryGraph buildBoundaryGraph(
	const std::unordered_set<MeshEdge, MeshEdgeHash>& outerEdges
) {
	BoundaryGraph graph;

	for (const MeshEdge& edge : outerEdges) {
		BoundarySegment seg = meshEdgeToSegment(edge);

		graph[seg.a].push_back(seg.b);
		graph[seg.b].push_back(seg.a);
	}

	return graph;
}

std::vector<std::vector<GridVertex>> traceAllBoundaryLoops(
	const std::unordered_set<MeshEdge, MeshEdgeHash>& outerEdges
) {
	BoundaryGraph graph = buildBoundaryGraph(outerEdges);

	std::vector<std::vector<GridVertex>> loops;
	std::unordered_set<GridVertexEdge, VertexEdgeHash> visitedEdges;

	for (const auto& [startVertex, neighbors] : graph) {
		for (const GridVertex& firstNeighbor : neighbors) {
			GridVertexEdge firstEdge = makeUndirectedEdge(startVertex, firstNeighbor);

			if (visitedEdges.find(firstEdge) != visitedEdges.end()) {
				continue;
			}

			std::vector<GridVertex> loop;

			GridVertex start = startVertex;
			GridVertex prev = startVertex;
			GridVertex current = firstNeighbor;

			loop.push_back(start);

			visitedEdges.insert(firstEdge);

			while (true) {
				loop.push_back(current);

				auto it = graph.find(current);
				if (it == graph.end()) {
					break;
				}

				const std::vector<GridVertex>& currentNeighbors = it->second;

				GridVertex next{ -1, -1 };

				for (const GridVertex& candidate : currentNeighbors) {
					if (!(candidate == prev)) {
						next = candidate;
						break;
					}
				}

				if (next.i == -1) {
					break;
				}

				GridVertexEdge edgeKey = makeUndirectedEdge(current, next);

				if (visitedEdges.find(edgeKey) != visitedEdges.end()) {
					break;
				}

				visitedEdges.insert(edgeKey);

				prev = current;
				current = next;

				if (current == start) {
					break;
				}
			}

			if (loop.size() >= 3) {
				loops.push_back(loop);
			}
		}
	}

	return loops;
}

bool sameDirection(
	const GridVertex& a,
	const GridVertex& b,
	const GridVertex& c
) {
	int di1 = b.i - a.i;
	int dj1 = b.j - a.j;

	int di2 = c.i - b.i;
	int dj2 = c.j - b.j;

	return di1 == di2 && dj1 == dj2;
}

std::vector<GridVertex> simplifyLoop(
	const std::vector<GridVertex>& loop
) {
	if (loop.size() < 3) {
		return loop;
	}

	std::vector<GridVertex> simplified;

	for (std::size_t k = 0; k < loop.size(); k++) {
		const GridVertex& prev = loop[(k + loop.size() - 1) % loop.size()];
		const GridVertex& curr = loop[k];
		const GridVertex& next = loop[(k + 1) % loop.size()];

		if (!sameDirection(prev, curr, next)) {
			simplified.push_back(curr);
		}
	}

	return simplified;
}

std::vector<BoundarySegment> makeBoundarySegments(
	const std::vector<GridVertex>& simplifiedLoop
) {
	std::vector<BoundarySegment> segments;

	if (simplifiedLoop.size() < 2) {
		return segments;
	}

	for (std::size_t k = 0; k < simplifiedLoop.size(); k++) {
		const GridVertex& a = simplifiedLoop[k];
		const GridVertex& b = simplifiedLoop[(k + 1) % simplifiedLoop.size()];

		// Boundary segments on this mesh should never be diagonal.
		if (!isAxisAligned(a, b)) {
			continue;
		}

		segments.push_back({ a, b });
	}

	return segments;
}

std::vector<BoundarySegment> buildDisplayBoundaries(
	const std::unordered_set<MeshEdge, MeshEdgeHash>& outerEdges
) {
	std::vector<BoundarySegment> allSegments;

	std::vector<std::vector<GridVertex>> loops =
		traceAllBoundaryLoops(outerEdges);

	for (const std::vector<GridVertex>& loop : loops) {
		std::vector<GridVertex> simplifiedLoop = simplifyLoop(loop);

		std::vector<BoundarySegment> segments =
			makeBoundarySegments(simplifiedLoop);

		allSegments.insert(
			allSegments.end(),
			segments.begin(),
			segments.end()
		);
	}

	return allSegments;
}



void MeshInspector::drawBoundarySegments(
	const std::vector<BoundarySegment>& segments,
	const std::vector<double>& rFace,
	const std::vector<double>& zFace
) {
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	for (const BoundarySegment& seg : segments) {
		ImVec2 p0 = gridToScreen(seg.a.j, seg.a.i, rFace, zFace);
		ImVec2 p1 = gridToScreen(seg.b.j, seg.b.i, rFace, zFace);

		drawList->AddLine(
			p0,
			p1,
			IM_COL32(255, 80, 80, 255),
			3.0f
		);
	}
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

// ======================================================================
// -----------------------MOUSE HANDLES----------------------------------
// ======================================================================
void MeshInspector::handleMouse() {

	if (!ImGui::IsItemHovered()) return;

	ImGuiIO& io = ImGui::GetIO();

	// constantly update current mouse position and mouse index (cell centered, i think)
	currentMouseIndex = getMouseIndex(g.rFace,g.zFace);
	currentMousePos = gridToScreen((int)currentMouseIndex.x, (int)currentMouseIndex.y, g.rFace, g.zFace);

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
		//printInt(selectableOuterEdges.size());
	}

	if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
		handlePopup();
	}

	if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !toggleSelect && !toggleConnecting) {
		handlePan(io);
	}



	// handle zooming in/out
	if (io.MouseWheel != 0.0f) {
		handleZoom(io);
	}

	// handle mouse hovering.
	if (toggleSelect) return;

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->AddCircleFilled(currentMousePos, circleRadius, IM_COL32(150, 150, 150, 255), 16);
	drawList->AddCircle(currentMousePos, circleRadius, IM_COL32(200, 200, 200, 255), 16, 1.0f);
	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {

		double zPos, rPos;
		getMousePhysicalCoord(currentMousePos, g.rFace, g.zFace, rPos, zPos);
		ImVec2 vecValue = ImVec2((float)zPos, (float)rPos);

		toggleSelectedPoint(points, currentMouseIndex, vecValue, 0.0f);	// value is 0.0f for now
	}
}

void MeshInspector::copyActiveSurfaceToClipboard() {

	GLint oldFBO, oldViewport[4];
	ImVec2 oldDisplaySize, oldFramebufferSize;
	offScreenFBO.create2DBuffer(pendingCopyWidth, pendingCopyHeight, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
	offScreenFBO.beginOffScreenImGuiRender(oldFBO, oldViewport, oldDisplaySize, oldFramebufferSize);

	// build imgui draw commands
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::Begin("##ExportWindow", nullptr, UIFlags::TemporaryWindowFlags);

	ImGui::Image((ImTextureID)(intptr_t)frameBuffer.getTextureID(), ImVec2((float)imageWidth, (float)imageHeight), ImVec2(u0, v1), ImVec2(u1, v0));
	drawHighlightedCells(g.obstacleIndices, g.zFace, g.rFace);

	ImGui::End();
	ImGui::PopStyleVar();

	offScreenFBO.endOffScreenImGuiRender(oldFBO, oldViewport, oldDisplaySize, oldFramebufferSize);
}

// ======================================================================
// -----------------------DRAW CALLS-------------------------------------
// ======================================================================
void MeshInspector::drawToolBar() {
	float toolbarHeight = 32.0f;

	ImGui::BeginChild("##toolbar", ImVec2(0.0f, toolbarHeight), false);

	addImageButtonToggleBool("Connect", assets.connectIcon, ImVec2(22.0f, 22.0f), toggleConnecting);
	setToolTip("Draw Lines");
	ImGui::SameLine();

	addImageButtonResetView(assets.houseIcon, ImVec2(22.0f, 22.0f));
	setToolTip("Reset view");
	ImGui::SameLine();

	addImageButtonClearVector("##ClearPoints", assets.clearIcon, ImVec2(22.0f, 22.0f), points);
	setToolTip("Clear all selected points");
	ImGui::SameLine();

	addImageButtonClearSet("##ClearObstacle", assets.clearIcon, ImVec2(22.0f, 22.0f), g.obstacleIndices);
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

	if (!openPopUp) return;

	if (ImGui::BeginPopup("MeshInspector Popup")) {

		//addMenuItem
		addMenuItemCopyToClipboard("Copy to clipboard");

		addMenuItemClearPoints("Clear points");

		addMenuItemResetView("Reset view");

		ImGui::EndPopup();
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

	std::vector<BoundarySegment> displayBoundaries =
		buildDisplayBoundaries(selectableOuterEdges);

	drawBoundarySegments(
		displayBoundaries,
		g.rFace,
		g.zFace
	);

	drawHighlightedCells(g.obstacleIndices, g.rFace, g.zFace);
	drawTextAtSurfacePoint();
	drawPopup();

	ImGui::End();
}