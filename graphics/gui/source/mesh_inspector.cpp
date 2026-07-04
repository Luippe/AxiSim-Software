#include "mesh_inspector.h"

#include <format>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <glm/glm.hpp>

#include "mesh.h"
#include "project.h"
#include "geometry.h"
#include "colorbar.h"

#include "flag_manager.h"
#include "printer.h"
#include "math_func.h"
#include "unit_manager.h"

namespace {
	constexpr double meshInspectorTwoPi = 6.28318530717958647692;
	constexpr double meshInspectorEpsilon = 1e-9;

	double normalizeInspectorAngle(double angle) {
		angle = std::fmod(angle, meshInspectorTwoPi);
		if (angle < 0.0) {
			angle += meshInspectorTwoPi;
		}
		return angle;
	}

	double positiveInspectorAngleSpan(double startAngle, double endAngle) {
		double start = normalizeInspectorAngle(startAngle);
		double end = normalizeInspectorAngle(endAngle);
		while (end < start) {
			end += meshInspectorTwoPi;
		}
		return end - start;
	}

	Vec2 inspectorPointOnCircle(Vec2 center, double radius, double angle) {
		return Vec2{
			center.z + radius * std::cos(angle),
			center.r + radius * std::sin(angle)
		};
	}

	Vec2 inspectorInterpolate(Vec2 a, Vec2 b, double t) {
		return Vec2{
			a.z + (b.z - a.z) * t,
			a.r + (b.r - a.r) * t
		};
	}

	float inspectorPixelDistance(ImVec2 a, ImVec2 b) {
		float dx = a.x - b.x;
		float dy = a.y - b.y;
		return std::sqrt(dx * dx + dy * dy);
	}

	double inspectorDot(Vec2 a, Vec2 b) {
		return a.z * b.z + a.r * b.r;
	}

	Vec2 inspectorSubtract(Vec2 a, Vec2 b) {
		return Vec2{ a.z - b.z, a.r - b.r };
	}

	Vec2 inspectorClosestPointOnSegment(Vec2 p, Vec2 a, Vec2 b) {
		Vec2 ab = inspectorSubtract(b, a);
		double len2 = inspectorDot(ab, ab);

		if (len2 <= meshInspectorEpsilon) {
			return a;
		}

		double t = inspectorDot(inspectorSubtract(p, a), ab) / len2;
		t = std::clamp(t, 0.0, 1.0);

		return inspectorInterpolate(a, b, t);
	}

	double inspectorAngleOfPoint(Vec2 center, Vec2 point) {
		return normalizeInspectorAngle(
			std::atan2(point.r - center.r, point.z - center.z)
		);
	}

	bool inspectorAngleOnArc(double angle, const SketchArc& arc) {
		double start = normalizeInspectorAngle(arc.startAngle);
		double end = arc.endAngle;
		while (end < start) {
			end += meshInspectorTwoPi;
		}

		angle = normalizeInspectorAngle(angle);
		if (angle < start) {
			angle += meshInspectorTwoPi;
		}

		return angle >= start - 1e-7 && angle <= end + 1e-7;
	}

	EdgeOrient inferInspectorPathOrientation(
		const std::vector<Vec2>& points,
		double tol
	) {
		bool hasHorizontal = false;
		bool hasVertical = false;
		bool hasOther = false;

		for (int i = 0; i < (int)points.size() - 1; i++) {
			Vec2 a = points[i];
			Vec2 b = points[i + 1];

			double dz = b.z - a.z;
			double dr = b.r - a.r;
			double length2 = dz * dz + dr * dr;

			if (length2 <= tol * tol) {
				continue;
			}

			if (std::abs(dr) <= tol) {
				hasHorizontal = true;
			}
			else if (std::abs(dz) <= tol) {
				hasVertical = true;
			}
			else {
				hasOther = true;
			}
		}

		if (hasOther || (hasHorizontal && hasVertical)) {
			return EdgeOrient::Both;
		}

		if (hasVertical) {
			return EdgeOrient::Vertical;
		}

		return EdgeOrient::Horizontal;
	}

	EdgeOrient inspectorOrientationFromFlags(
		bool hasHorizontal,
		bool hasVertical,
		bool hasOther
	) {
		if (hasOther || (hasHorizontal && hasVertical)) {
			return EdgeOrient::Both;
		}

		if (hasVertical) {
			return EdgeOrient::Vertical;
		}

		return EdgeOrient::Horizontal;
	}
}

MeshInspector::MeshInspector(Project& project, AppConfig& appConfig) :
	project(project),
	mesh(project.mesh),
	geometry(project.geometry),
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
int addBoundaryVertexFromGrid(
	std::vector<BoundaryVertex>& vertices,
	GridVertex grid,
	const std::vector<double>& rFace,
	const std::vector<double>& zFace
) {
	if (grid.i < 0 || grid.i >= static_cast<int>(rFace.size()) ||
		grid.j < 0 || grid.j >= static_cast<int>(zFace.size())) {

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
	bool hasOther = false;

	if (!group.edges.empty()) {
		for (const MeshEdge& edge : group.edges) {

			if (edge.orient == EdgeOrient::Horizontal) {
				hasHorizontal = true;
			}
			else if (edge.orient == EdgeOrient::Vertical) {
				hasVertical = true;
			}
			else {
				hasOther = true;
			}

			if (hasOther || (hasHorizontal && hasVertical)) {
				group.includesOrientation = EdgeOrient::Both;
				return;
			}
		}
	}
	else {
		double tol = std::max(std::max(g.L, g.R), 1.0) * 1e-8;

		for (int segmentID : group.segmentIDs) {
			BoundarySegment* segment = mesh.getBoundarySegmentByID(segmentID);
			if (!segment) {
				continue;
			}

			EdgeOrient orient =
				inferInspectorPathOrientation(segment->controlPoints, tol);

			if (orient == EdgeOrient::Horizontal) {
				hasHorizontal = true;
			}
			else if (orient == EdgeOrient::Vertical) {
				hasVertical = true;
			}
			else {
				hasOther = true;
			}

			if (hasOther || (hasHorizontal && hasVertical)) {
				group.includesOrientation = EdgeOrient::Both;
				return;
			}
		}
	}

	group.includesOrientation =
		inspectorOrientationFromFlags(hasHorizontal, hasVertical, hasOther);
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

int meshInspectorCellIndexAt(const std::vector<double>& faces, double x) {
	if (faces.size() < 2) {
		return -1;
	}

	if (x < faces.front() || x > faces.back()) {
		return -1;
	}

	auto it = std::upper_bound(faces.begin(), faces.end(), x);
	int index = static_cast<int>(it - faces.begin()) - 1;

	return std::clamp(index, 0, static_cast<int>(faces.size()) - 2);
}

// ======================================================================
// -----------------------CELL INSPECTION--------------------------------
// ======================================================================
static double cellPickSign(const Vec2& p, const Vec2& a, const Vec2& b) {
	return (p.z - b.z) * (a.r - b.r) - (a.z - b.z) * (p.r - b.r);
}

void MeshInspector::buildInspectMesh() {
	if (mesh.currentMeshType == MeshType::Structured) {
		int nCells = std::max(g.nr * g.nz, 0);

		// createStructuredMesh indexes activeCell[n] directly, so make sure it
		// is sized. Fall back to an all-fluid grid if the sketch hasn't been
		// rasterized yet.
		if ((int)g.activeCell.size() == nCells && nCells > 0) {
			inspectFVMesh = mesh.createFVMesh(g.activeCell);
		}
		else {
			std::vector<uint8_t> allFluid(nCells, 1);
			inspectFVMesh = mesh.createFVMesh(allFluid);
		}
	}
	else {
		// unstructured ignores the activeCell argument
		inspectFVMesh = mesh.createFVMesh({});
	}

	inspectMeshDirty = false;
}

int MeshInspector::pickCell(const Vec2& world) const {
	if (mesh.currentMeshType == MeshType::Structured) {
		if (g.zFace.size() < 2 || g.rFace.size() < 2) {
			return -1;
		}

		int j = meshInspectorCellIndexAt(g.zFace, world.z);
		int i = meshInspectorCellIndexAt(g.rFace, world.r);

		if (i < 0 || j < 0) {
			return -1;
		}

		int n = i * g.nz + j;

		if (n < 0 || n >= (int)inspectFVMesh.cells.size()) {
			return -1;
		}

		return n;
	}

	// unstructured: FV cells map 1:1 to triangles, so a point-in-triangle test
	// gives the cell index directly.
	const std::vector<Vec2>& pts = mesh.unstructuredPoints;
	const std::vector<Triangle>& tris = mesh.unstructuredTriangles;

	for (int c = 0; c < (int)tris.size(); c++) {
		const Triangle& t = tris[c];

		if (t.v0 < 0 || t.v1 < 0 || t.v2 < 0) continue;
		if (t.v0 >= (int)pts.size() ||
			t.v1 >= (int)pts.size() ||
			t.v2 >= (int)pts.size()) {
			continue;
		}

		double d1 = cellPickSign(world, pts[t.v0], pts[t.v1]);
		double d2 = cellPickSign(world, pts[t.v1], pts[t.v2]);
		double d3 = cellPickSign(world, pts[t.v2], pts[t.v0]);

		bool hasNeg = (d1 < 0.0) || (d2 < 0.0) || (d3 < 0.0);
		bool hasPos = (d1 > 0.0) || (d2 > 0.0) || (d3 > 0.0);

		if (!(hasNeg && hasPos)) {
			return c;
		}
	}

	return -1;
}

double MeshInspector::cellNonOrthogonality(
	int cellID,
	double& avgDeg,
	int& interiorFaces
) const {
	avgDeg = 0.0;
	interiorFaces = 0;

	if (cellID < 0 || cellID >= (int)inspectFVMesh.cells.size()) {
		return -1.0;
	}

	const FVCell& cell = inspectFVMesh.cells[cellID];

	double maxDeg = 0.0;
	double sumDeg = 0.0;

	constexpr double radToDeg = 57.29577951308232;

	for (int fid : cell.faceIDs) {
		if (fid < 0 || fid >= (int)inspectFVMesh.faces.size()) {
			continue;
		}

		const FVFace& f = inspectFVMesh.faces[fid];

		if (f.neighbor < 0) {
			continue; // boundary face: no neighbour centroid to measure against
		}

		if (f.owner < 0 || f.owner >= (int)inspectFVMesh.cells.size() ||
			f.neighbor >= (int)inspectFVMesh.cells.size()) {
			continue;
		}

		const FVCell& P = inspectFVMesh.cells[f.owner];
		const FVCell& N = inspectFVMesh.cells[f.neighbor];

		// d: centroid-to-centroid vector;  S (f.normal): face normal
		double dz = N.center.z - P.center.z;
		double dr = N.center.r - P.center.r;
		double dLen = std::sqrt(dz * dz + dr * dr);
		double nLen = std::sqrt(f.normal.z * f.normal.z + f.normal.r * f.normal.r);

		if (dLen < 1e-30 || nLen < 1e-30) {
			continue;
		}

		// angle between the centroid line and the face normal (0 = orthogonal)
		double cosAng = (dz * f.normal.z + dr * f.normal.r) / (dLen * nLen);
		cosAng = std::clamp(cosAng, -1.0, 1.0);

		double ang = std::acos(std::abs(cosAng)) * radToDeg;

		maxDeg = std::max(maxDeg, ang);
		sumDeg += ang;
		interiorFaces++;
	}

	if (interiorFaces == 0) {
		return -1.0;
	}

	avgDeg = sumDeg / interiorFaces;
	return maxDeg;
}

void MeshInspector::handleCellSelection(ImGuiIO& io) {
	if (!isMouseNearImage(io)) {
		return;
	}

	// treat a left release as a pick only when the mouse wasn't dragged
	// (a drag is a pan/zoom gesture, not a selection)
	if (!ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
		return;
	}

	ImVec2 drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
	if (drag.x * drag.x + drag.y * drag.y > 9.0f) {
		return;
	}

	Vec2 world = camera.screenToWorld(ImGui::GetMousePos());
	selectedCell = pickCell(world); // -1 if the click missed the mesh (deselect)
}

// ======================================================================
// -----------------------MOUSE HANDLES----------------------------------
// ======================================================================
void MeshInspector::handleCursor(ImGuiIO& io) {

	// do not run this if any of the toggled tools are active, or if a popup is opened
	isPopupOpened = ImGui::IsPopupOpen("Mesh Inspector Popup");
	if (toggleDrawCircle || toggleDrawRect || toggleRuler || isPopupOpened) return;

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

std::optional<MeshSnapResult> MeshInspector::findSnap(ImVec2 mouse) {
	constexpr float snapRadiusPx = 10.0f;

	Vec2 mouseWorld = camera.screenToWorld(mouse);
	std::optional<MeshSnapResult> best;

	auto tryCandidate = [&](MeshSnapType type, Vec2 world, int entityID) {
		ImVec2 screen = camera.worldToScreen(world);
		float distPx = inspectorPixelDistance(screen, mouse);

		if (distPx > snapRadiusPx) {
			return;
		}

		if (!best || distPx < best->distancePx) {
			best = MeshSnapResult{
				type,
				world,
				screen,
				distPx,
				entityID
			};
		}
	};

	{
		Vec2 origin{ 0.0, 0.0 };
		ImVec2 originScreen = camera.worldToScreen(origin);
		float originDistancePx = inspectorPixelDistance(originScreen, mouse);

		if (originDistancePx <= snapRadiusPx) {
			return MeshSnapResult{
				MeshSnapType::Vertex,
				origin,
				originScreen,
				originDistancePx,
				-102
			};
		}
	}

	tryCandidate(MeshSnapType::Line, Vec2{ mouseWorld.z, 0.0 }, -100);
	tryCandidate(MeshSnapType::Line, Vec2{ 0.0, mouseWorld.r }, -101);

	const SketchModel& sketch = geometry.sketch;

	for (const SketchPoint& point : sketch.points) {
		tryCandidate(MeshSnapType::Vertex, point.pos, point.id);
	}

	for (const SketchLine& line : sketch.lines) {
		const SketchPoint* p0 = sketch.findPoint(line.p0);
		const SketchPoint* p1 = sketch.findPoint(line.p1);

		if (!p0 || !p1) {
			continue;
		}

		Vec2 closest = inspectorClosestPointOnSegment(mouseWorld, p0->pos, p1->pos);
		tryCandidate(MeshSnapType::Line, closest, line.id);
	}

	for (const SketchRectangle& rect : sketch.rectangles) {
		Vec2 corners[4] = {
			Vec2{ rect.min.z, rect.min.r },
			Vec2{ rect.max.z, rect.min.r },
			Vec2{ rect.max.z, rect.max.r },
			Vec2{ rect.min.z, rect.max.r }
		};

		for (int edge = 0; edge < 4; edge++) {
			Vec2 a = corners[edge];
			Vec2 b = corners[(edge + 1) % 4];

			tryCandidate(MeshSnapType::Vertex, a, rect.id);
			tryCandidate(
				MeshSnapType::Line,
				inspectorClosestPointOnSegment(mouseWorld, a, b),
				rect.id
			);
		}

		tryCandidate(
			MeshSnapType::Vertex,
			Vec2{
				0.5 * (rect.min.z + rect.max.z),
				0.5 * (rect.min.r + rect.max.r)
			},
			rect.id
		);
	}

	for (const SketchCircle& circle : sketch.circles) {
		double dz = mouseWorld.z - circle.center.z;
		double dr = mouseWorld.r - circle.center.r;
		double len = std::sqrt(dz * dz + dr * dr);

		if (len > 1e-30) {
			tryCandidate(
				MeshSnapType::Circle,
				Vec2{
					circle.center.z + circle.radius * dz / len,
					circle.center.r + circle.radius * dr / len
				},
				circle.id
			);
		}

		tryCandidate(MeshSnapType::Vertex, circle.center, circle.id);
	}

	for (const SketchArc& arc : sketch.arcs) {
		double angle = inspectorAngleOfPoint(arc.center, mouseWorld);

		if (inspectorAngleOnArc(angle, arc)) {
			tryCandidate(
				MeshSnapType::Circle,
				inspectorPointOnCircle(arc.center, arc.radius, angle),
				arc.id
			);
		}

		tryCandidate(
			MeshSnapType::Vertex,
			inspectorPointOnCircle(arc.center, arc.radius, arc.startAngle),
			arc.id
		);
		tryCandidate(
			MeshSnapType::Vertex,
			inspectorPointOnCircle(arc.center, arc.radius, arc.endAngle),
			arc.id
		);
		tryCandidate(MeshSnapType::Vertex, arc.center, arc.id);
	}

	for (const BoundaryVertex& vertex : mesh.boundaryVertices) {
		tryCandidate(MeshSnapType::Vertex, vertex.pos, vertex.id);
	}

	for (const BoundaryEdge& edge : mesh.boundaryEdges) {
		if (!edgeInRange(edge, mesh.boundaryVertices.size())) {
			continue;
		}

		Vec2 a = mesh.boundaryVertices[edge.v0].pos;
		Vec2 b = mesh.boundaryVertices[edge.v1].pos;
		tryCandidate(
			MeshSnapType::Line,
			inspectorClosestPointOnSegment(mouseWorld, a, b),
			edge.id
		);
	}

	return best;
}

Vec2 MeshInspector::getSnappedWorld(ImVec2 mouse) {
	if (!toggleSnapping) {
		return camera.screenToWorld(mouse);
	}

	if (auto snap = findSnap(mouse)) {
		return snap->world;
	}

	return camera.screenToWorld(mouse);
}

void MeshInspector::handleDrawRegionOfInfluence() {
	if (mesh.currentMeshType == MeshType::Structured) {
		return;
	}

	bool drawingCircle = toggleDrawCircle;
	bool drawingRect = toggleDrawRect;

	if (!drawingCircle && !drawingRect) {
		return;
	}

	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		roiStartWorld = getSnappedWorld(currentMousePos);
		roiCurrentWorld = roiStartWorld;
		initLeftMouse = camera.worldToScreen(roiStartWorld);
	}

	roiCurrentWorld = getSnappedWorld(currentMousePos);

	if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
		if (drawingCircle) {
			pendingCircle.pending = true;
			pendingCircle.radius = distance(roiStartWorld, roiCurrentWorld);
		}
		else {
			pendingRect.pending = true;
			pendingRect.p0 = roiStartWorld;
			pendingRect.p1 = roiCurrentWorld;
		}
	}

	if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
		double defaultSpacing = std::max(std::min(mesh.g.L, mesh.g.R) / 10.0, 1e-6);
		double targetSpacing = std::max(std::min(mesh.g.L, mesh.g.R) / 80.0, 1e-6);

		MeshRegionOfInfluence region{};
		region.id = mesh.nextRegionOfInfluenceID++;
		region.enabled = true;
		region.targetSpacing = targetSpacing;
		region.outsideSpacing = defaultSpacing;

		if (drawingCircle) {
			double radius = distance(roiStartWorld, roiCurrentWorld);

			if (radius <= 1e-12) {
				pendingCircle.pending = false;
				return;
			}

			region.shape = MeshRegionShape::Circle;
			region.center = roiStartWorld;
			region.radius = radius;
			region.transitionThickness = radius * 0.5;
			region.min = Vec2{
				roiStartWorld.z - radius,
				roiStartWorld.r - radius
			};
			region.max = Vec2{
				roiStartWorld.z + radius,
				roiStartWorld.r + radius
			};
		}
		else {
			double zMin = std::min(roiStartWorld.z, roiCurrentWorld.z);
			double zMax = std::max(roiStartWorld.z, roiCurrentWorld.z);
			double rMin = std::min(roiStartWorld.r, roiCurrentWorld.r);
			double rMax = std::max(roiStartWorld.r, roiCurrentWorld.r);

			if (zMax - zMin <= 1e-12 || rMax - rMin <= 1e-12) {
				pendingRect.pending = false;
				return;
			}

			region.shape = MeshRegionShape::Rectangle;
			region.min = Vec2{ zMin, rMin };
			region.max = Vec2{ zMax, rMax };
			region.center = Vec2{
				0.5 * (zMin + zMax),
				0.5 * (rMin + rMax)
			};
			region.radius = 0.5 * std::min(zMax - zMin, rMax - rMin);
			region.transitionThickness = 0.5 * std::min(zMax - zMin, rMax - rMin);
		}

		mesh.regionsOfInfluence.push_back(region);
		pendingCircle.pending = false;
		pendingRect.pending = false;
	}
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
	toggleSnapping = io.KeyCtrl;

	handleOpenPopup();

	// handle zooming and panning
	if (io.MouseWheel != 0.0f) {
		camera.calculateZoom(io.MouseWheel, currentMousePos);
	}

	if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
		camera.calculatePan(io.MouseDelta.x, io.MouseDelta.y);
	}

	if (toggleInspectCell) {
		// inspect mode owns the left click; skip boundary selection / ROI
		handleCellSelection(io);
	}
	else {
		handleCursor(io);
		handleDrawRegionOfInfluence();
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

		ImVec2 p0 = camera.worldToScreen(p0World);
		ImVec2 p1 = camera.worldToScreen(p1World);

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

	ImVec2 exportSize((float)pendingCopyWidth, (float)pendingCopyHeight);
	ImGui::Image((ImTextureID)(intptr_t)frameBuffer.getTextureID(), exportSize, ImVec2(0.0, 1.0f), ImVec2(1.0f, 0.0f));
	
	canvasRect = makePaddedRect(ImGui::GetItemRectMin(), exportSize);
	camera.setDimensions(
		canvasRect.size.x,
		canvasRect.size.y,
		canvasRect.min
	);

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawCanvas(drawList, canvasRect, 5.0f);

	drawList->PushClipRect(canvasRect.min, canvasRect.max, true);
	drawAxes(drawList);
	drawHighlightedCells2D(drawList);
	drawMeshLines(drawList);
	drawRegionsOfInfluence(drawList);
	drawPendingObjects(drawList);
	drawSnapping(drawList);
	drawBoundarySegments(drawList);
	drawTextAtSurfacePoint(drawList);
	if (toggleInspectCell) {
		drawCellInfo(drawList);
	}
	drawList->PopClipRect();

	ImGui::End();
	ImGui::PopStyleVar();

	offScreenFBO.endOffScreenImGuiRender(oldFBO, oldViewport, oldDisplaySize, oldFramebufferSize);
}

// ======================================================================
// -----------------------DRAW CALLS-------------------------------------
// ======================================================================
void MeshInspector::drawMeshLines(ImDrawList* drawList) {
	if (!toggleMesh) {
		return;
	}

	const ImU32 lineColor = IM_COL32(190, 205, 225, 155);

	if (mesh.currentMeshType == MeshType::Structured &&
		!g.zFace.empty() &&
		!g.rFace.empty()) {
		double rMin = g.rFace.front();
		double rMax = g.rFace.back();
		double zMin = g.zFace.front();
		double zMax = g.zFace.back();

		for (double z : g.zFace) {
			drawList->AddLine(
				camera.worldToScreen(Vec2{ z, rMin }),
				camera.worldToScreen(Vec2{ z, rMax }),
				lineColor,
				1.0f
			);
		}

		for (double r : g.rFace) {
			drawList->AddLine(
				camera.worldToScreen(Vec2{ zMin, r }),
				camera.worldToScreen(Vec2{ zMax, r }),
				lineColor,
				1.0f
			);
		}
	}
	else {
		for (int i = 0; i + 3 < (int)mesh.gridLineVertices.size(); i += 4) {
			Vec2 p0{
				0.5 * (mesh.gridLineVertices[i + 0] + 1.0) * g.L,
				0.5 * (mesh.gridLineVertices[i + 1] + 1.0) * g.R
			};

			Vec2 p1{
				0.5 * (mesh.gridLineVertices[i + 2] + 1.0) * g.L,
				0.5 * (mesh.gridLineVertices[i + 3] + 1.0) * g.R
			};

			drawList->AddLine(
				camera.worldToScreen(p0),
				camera.worldToScreen(p1),
				lineColor,
				1.0f
			);
		}
	}
}

void MeshInspector::drawHighlightedCells2D(ImDrawList* drawList) {
	if (g.zFace.size() < 2 || g.rFace.size() < 2) {
		return;
	}

	int nz = (int)g.zFace.size() - 1;
	int nr = (int)g.rFace.size() - 1;



	for (int n : g.obstacleIndices) {
		int i = n / nz;
		int j = n % nz;

		if (j < 0 || j >= nz || i < 0 || i >= nr) {
			continue;
		}

		ImVec2 p0 = camera.worldToScreen(Vec2{ g.zFace[j], g.rFace[i] });
		ImVec2 p1 = camera.worldToScreen(Vec2{ g.zFace[j + 1], g.rFace[i + 1] });

		ImVec2 rectMin(
			std::min(p0.x, p1.x),
			std::min(p0.y, p1.y)
		);

		ImVec2 rectMax(
			std::max(p0.x, p1.x),
			std::max(p0.y, p1.y)
		);

		drawList->AddRectFilled(
			rectMin,
			rectMax,
			IM_COL32(151, 151, 151, 255)
		);
	}
}

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

			ImVec2 p0 = camera.worldToScreen(p0World);
			ImVec2 p1 = camera.worldToScreen(p1World);

			drawList->AddLine(p0, p1, color, thickness);
		}
	}
}

void MeshInspector::drawToolBar() {
	float toolbarHeight = 40.0f;

	ImGui::BeginChild("##toolbar", ImVec2(0.0f, toolbarHeight), false);

	if (addImageButton("Reset", "Reset View", assets.houseIcon, buttonSize)) {
		camera.home();
	}
	ImGui::SameLine();

	if (addImageButtonToggle(
		"ROICircle",
		"Draw circular region of influence",
		assets.drawCircleIcon,
		buttonSize,
		toggleDrawCircle
	)) {
		toggleDrawRect = false;
		toggleInspectCell = false;
	}
	ImGui::SameLine();

	if (addImageButtonToggle(
		"ROIRect",
		"Draw rectangular region of influence",
		assets.selectRegionIcon,
		buttonSize,
		toggleDrawRect
	)) {
		toggleDrawCircle = false;
		toggleInspectCell = false;
	}
	ImGui::SameLine();

	if (addImageButtonToggle(
		"InspectCell",
		"Inspect cell mesh data (click a cell)",
		assets.selectIcon,
		buttonSize,
		toggleInspectCell
	)) {
		toggleDrawCircle = false;
		toggleDrawRect = false;
		toggleRuler = false;
		selectedCell = -1;
		inspectMeshDirty = true;
	}

	ImGui::SameLine();

	addImageButtonToggle(
		"ToggleMesh",
		"Toggle Mesh",
		assets.fillCellIcon,
		buttonSize,
		toggleMesh
	);

	ImGui::SameLine();

	if (addImageButton("Copy", "Copy to clipboard", assets.copyIcon, buttonSize) || consoleCopy) {
		pendingCopyWidth = frameBuffer.width;
		pendingCopyHeight = frameBuffer.height;
		pendingCopy = true;
		consoleCopy = false;
	}


	ImGui::EndChild();
}

void MeshInspector::drawTextAtSurfacePoint(ImDrawList* drawList) {

	for (const SurfacePoint& point : points) {

		ImVec2 screenPos = camera.worldToScreen(
			Vec2{ point.vecValue.x, point.vecValue.y }
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
			camera.home();
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
			}
			else {
				pendingBoundaryGroup->edges.clear();
			}

			setGroupOrientation(*pendingBoundaryGroup);
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

		float radiusPx = camera.worldLengthToScreen(pendingCircle.radius);

		drawList->AddCircle(initLeftMouse, radiusPx, drawingColor, 80, 3.0f);

	}

	if (pendingRect.pending) {
		ImVec2 p0 = camera.worldToScreen(pendingRect.p0);
		ImVec2 p1 = camera.worldToScreen(pendingRect.p1);

		ImVec2 rectMin(
			std::min(p0.x, p1.x),
			std::min(p0.y, p1.y)
		);

		ImVec2 rectMax(
			std::max(p0.x, p1.x),
			std::max(p0.y, p1.y)
		);

		drawList->AddRect(rectMin, rectMax, drawingColor, 0.0f, 0, 3.0f);
	}

}

void MeshInspector::drawSnapping(ImDrawList* drawList) {
	if (!toggleSnapping || (!toggleDrawCircle && !toggleDrawRect)) {
		return;
	}

	if (pendingCircle.pending || pendingRect.pending) {
		drawList->AddCircleFilled(
			camera.worldToScreen(roiStartWorld),
			3.0f,
			IM_COL32(255, 230, 80, 255)
		);
	}

	if (auto snap = findSnap(currentMousePos)) {
		drawList->AddCircleFilled(
			snap->screen,
			4.0f,
			IM_COL32(255, 230, 80, 255)
		);
	}

}

void MeshInspector::drawRegionsOfInfluence(ImDrawList* drawList) {

	const ImU32 strokeColor = IM_COL32(83, 188, 255, 230);
	const ImU32 fillColor = IM_COL32(83, 188, 255, 35);

	for (const MeshRegionOfInfluence& region : mesh.regionsOfInfluence) {
		if (!region.enabled) {
			continue;
		}

		if (region.shape == MeshRegionShape::Rectangle) {
			ImVec2 p0 = camera.worldToScreen(region.min);
			ImVec2 p1 = camera.worldToScreen(region.max);

			ImVec2 rectMin(
				std::min(p0.x, p1.x),
				std::min(p0.y, p1.y)
			);

			ImVec2 rectMax(
				std::max(p0.x, p1.x),
				std::max(p0.y, p1.y)
			);

			drawList->AddRectFilled(rectMin, rectMax, fillColor);
			drawList->AddRect(rectMin, rectMax, strokeColor, 0.0f, 0, 2.0f);
		}
		else {
			float radiusPx = camera.worldLengthToScreen(region.radius);
			ImVec2 center = camera.worldToScreen(region.center);

			drawList->AddCircleFilled(center, radiusPx, fillColor, 80);
			drawList->AddCircle(center, radiusPx, strokeColor, 80, 2.0f);
		}
	}
}

void MeshInspector::drawCellInfo(ImDrawList* drawList) {
	if (selectedCell < 0) {
		return;
	}

	if (selectedCell >= (int)inspectFVMesh.cells.size()) {
		selectedCell = -1; // stale selection (mesh changed underneath us)
		return;
	}

	ImVec2 canvasMin = canvasRect.min;
	ImVec2 canvasMax = canvasRect.max;

	const ImU32 fillCol = IM_COL32(255, 235, 60, 70);
	const ImU32 lineCol = IM_COL32(255, 235, 60, 255);

	// --- highlight the pinned cell ---
	drawList->PushClipRect(canvasMin, canvasMax, true);

	if (mesh.currentMeshType == MeshType::Structured) {
		int i = selectedCell / std::max(g.nz, 1);
		int j = selectedCell % std::max(g.nz, 1);

		if (i >= 0 && i + 1 < (int)g.rFace.size() &&
			j >= 0 && j + 1 < (int)g.zFace.size()) {
			ImVec2 p0 = camera.worldToScreen(Vec2{ g.zFace[j], g.rFace[i] });
			ImVec2 p1 = camera.worldToScreen(Vec2{ g.zFace[j + 1], g.rFace[i + 1] });

			ImVec2 rmin(std::min(p0.x, p1.x), std::min(p0.y, p1.y));
			ImVec2 rmax(std::max(p0.x, p1.x), std::max(p0.y, p1.y));

			drawList->AddRectFilled(rmin, rmax, fillCol);
			drawList->AddRect(rmin, rmax, lineCol, 0.0f, 0, 2.0f);
		}
	}
	else if (selectedCell < (int)mesh.unstructuredTriangles.size()) {
		const std::vector<Vec2>& pts = mesh.unstructuredPoints;
		const Triangle& t = mesh.unstructuredTriangles[selectedCell];

		if (t.v0 >= 0 && t.v1 >= 0 && t.v2 >= 0 &&
			t.v0 < (int)pts.size() &&
			t.v1 < (int)pts.size() &&
			t.v2 < (int)pts.size()) {
			ImVec2 a = camera.worldToScreen(pts[t.v0]);
			ImVec2 b = camera.worldToScreen(pts[t.v1]);
			ImVec2 d = camera.worldToScreen(pts[t.v2]);

			drawList->AddTriangleFilled(a, b, d, fillCol);
			drawList->AddTriangle(a, b, d, lineCol, 2.0f);
		}
	}

	drawList->PopClipRect();

	// --- build the info text ---
	const FVCell& cell = inspectFVMesh.cells[selectedCell];

	std::string info;
	char line[160];

	std::snprintf(line, sizeof(line), "Cell #%d", selectedCell);
	info += line;

	std::snprintf(line, sizeof(line), "\ncenter:  z %.6g   r %.6g", cell.center.z, cell.center.r);
	info += line;

	if (cell.area2D > 0.0) {
		std::snprintf(line, sizeof(line), "\narea2D:  %.6g", cell.area2D);
		info += line;
	}

	std::snprintf(line, sizeof(line), "\nvolume:  %.6g", cell.volume);
	info += line;

	std::snprintf(line, sizeof(line), "\nfaces:   %d", (int)cell.faceIDs.size());
	info += line;

	std::snprintf(line, sizeof(line), "\nactive:  %s%s",
		cell.active ? "yes" : "no",
		cell.solid ? "   (solid)" : "");
	info += line;

	// --- non-orthogonality (the mesh-quality measure) ---
	double avgDeg = 0.0;
	int interiorFaces = 0;
	double maxDeg = cellNonOrthogonality(selectedCell, avgDeg, interiorFaces);

	info += "\n----------------";
	if (maxDeg < 0.0) {
		info += "\nnon-orthogonality: n/a (no interior faces)";
	}
	else {
		std::snprintf(line, sizeof(line),
			"\nnon-orthogonality (deg):\n  max %.3f   avg %.3f", maxDeg, avgDeg);
		info += line;
	}

	// --- per-face geometry: neighbour, edge length, face non-orthogonality ---
	if (!cell.faceIDs.empty()) {
		info += "\nfaces (nb | len | non-orth):";

		constexpr double radToDeg = 57.29577951308232;

		for (int fid : cell.faceIDs) {
			if (fid < 0 || fid >= (int)inspectFVMesh.faces.size()) {
				continue;
			}

			const FVFace& f = inspectFVMesh.faces[fid];

			if (f.neighbor < 0) {
				std::snprintf(line, sizeof(line), "\n  f%-5d bdry   %.4g", fid, f.length2D);
				info += line;
				continue;
			}

			int nb = (f.owner == selectedCell) ? f.neighbor : f.owner;

			const FVCell& P = inspectFVMesh.cells[f.owner];
			const FVCell& N = inspectFVMesh.cells[f.neighbor];

			double dz = N.center.z - P.center.z;
			double dr = N.center.r - P.center.r;
			double dLen = std::sqrt(dz * dz + dr * dr);
			double nLen = std::sqrt(f.normal.z * f.normal.z + f.normal.r * f.normal.r);

			double ang = 0.0;
			if (dLen > 1e-30 && nLen > 1e-30) {
				double cosAng = (dz * f.normal.z + dr * f.normal.r) / (dLen * nLen);
				cosAng = std::clamp(cosAng, -1.0, 1.0);
				ang = std::acos(std::abs(cosAng)) * radToDeg;
			}

			std::snprintf(line, sizeof(line), "\n  f%-5d nb %-5d %.4g  %.2f deg",
				fid, nb, f.length2D, ang);
			info += line;
		}
	}

	// --- draw the panel (top-left of the canvas) ---
	const ImVec2 pad(8.0f, 6.0f);
	ImVec2 origin(canvasMin.x + 10.0f, canvasMin.y + 10.0f);

	ImVec2 ts = ImGui::CalcTextSize(info.c_str());

	ImVec2 rmin = origin;
	ImVec2 rmax(origin.x + ts.x + pad.x * 2.0f, origin.y + ts.y + pad.y * 2.0f);

	drawList->AddRectFilled(rmin, rmax, IM_COL32(15, 20, 28, 235), 4.0f);
	drawList->AddRect(rmin, rmax, IM_COL32(90, 120, 150, 200), 4.0f, 0, 1.0f);
	drawList->AddText(ImVec2(origin.x + pad.x, origin.y + pad.y),
		IM_COL32(230, 235, 245, 255), info.c_str());
}

void MeshInspector::render() {
	setBaseNrNz();

	updateLengthScale(
		project.lengthScale.value,
		Units::lengthUnits[project.lengthScale.index].name
	);

	ImGui::Begin("Mesh Inspector");

	ImDrawList* drawList = ImGui::GetWindowDrawList();

	drawToolBar();

	ImVec2 pos = ImGui::GetCursorScreenPos();
	ImVec2 size = ImGui::GetContentRegionAvail();
	canvasRect = makePaddedRect(pos, size);

	resizeImage();

	camera.setDimensions(
		canvasRect.size.x,
		canvasRect.size.y,
		canvasRect.min
	);

	// Build current segments before hover/mouse logic
	if (mesh.currentMeshType == MeshType::Structured) {
		buildSegments();
	}

	// update current global mouse pos
	updateCurrentMousePos();

	// keep the inspection snapshot in sync while inspect mode is active
	if (toggleInspectCell && inspectMeshDirty) {
		buildInspectMesh();
	}

	// Update hover before mouse logic (suppressed while inspecting cells)
	if (toggleInspectCell) {
		hoveredId = std::nullopt;
		hoveringOverSegment = false;
	}
	else {
		hoveredId = findHoveredBoundarySegment();
		hoveringOverSegment = hoveredId.has_value();
	}

	// Now handle mouse using current hoveredId/current segments
	handleMouse();


	drawCanvas(drawList, canvasRect, 5.0f);

	drawList->PushClipRect(canvasRect.min, canvasRect.max, true);
	drawAxes(drawList);
	drawHighlightedCells2D(drawList);
	drawMeshLines(drawList);
	drawRegionsOfInfluence(drawList);
	drawPendingObjects(drawList);
	drawSnapping(drawList);
	drawBoundarySegments(drawList);
	drawTextAtSurfacePoint(drawList);
	if (toggleInspectCell) {
		drawCellInfo(drawList);
	}
	drawList->PopClipRect();

	drawPopup();

	ImGui::End();
}
