#include "mesh_inspector.h"

#include <format>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <numbers>
#include <string>
#include <glm/glm.hpp>

#include "mesh.h"
#include "project.h"
#include "geometry.h"
#include "console.h"
#include "mesh_plot.h"

#include "flag_manager.h"
#include "printer.h"
#include "math_func.h"
#include "unit_manager.h"

namespace {
	constexpr double meshInspectorTwoPi = 2.0 * std::numbers::pi;
	constexpr double meshInspectorEpsilon = 1e-9;

	double normalizeInspectorAngle(double angle) {
		angle = std::fmod(angle, meshInspectorTwoPi);
		if (angle < 0.0) {
			angle += meshInspectorTwoPi;
		}
		return angle;
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

		return inspectorOrientationFromFlags(hasHorizontal, hasVertical, hasOther);
	}

	// The p1 and p99 of the cells that carry a value, for the local ramp mode.
	// Percentiles rather than min/max: one sliver at 30 would push every other cell
	// back to the green end, which is the problem the mode exists to solve. False
	// when nothing is measurable, or when every cell landed on the same value --
	// both cases would divide the ramp by a zero span, so the caller keeps the
	// fixed band instead.
	bool inspectorPercentiles(const std::vector<double>& values, double& p1, double& p99) {
		std::vector<double> v;
		v.reserve(values.size());

		for (double x : values) {
			if (std::isfinite(x)) {
				v.push_back(x);
			}
		}

		if (v.empty()) {
			return false;
		}

		std::sort(v.begin(), v.end());

		auto at = [&](double p) {
			size_t i = (size_t)std::llround(p * (double)(v.size() - 1));
			return v[std::min(i, v.size() - 1)];
		};

		p1 = at(0.01);
		p99 = at(0.99);

		return p99 > p1;
	}

	// Quality ramp for the cell-quality overlays: green (well shaped) -> yellow ->
	// red (badly shaped), the conventional reading in every mesher. Kept local
	// rather than taken from Colormap, whose LUTs are picked by the user for
	// solution fields -- a quality legend has to mean the same thing every time.
	ImU32 inspectorQualityColor(double t, int alpha) {
		t = std::clamp(t, 0.0, 1.0);

		int red = (int)(std::min(1.0, 2.0 * t) * 255.0);
		int green = (int)(std::min(1.0, 2.0 * (1.0 - t)) * 255.0);

		return IM_COL32(red, green, 55, alpha);
	}
}

MeshInspector::MeshInspector(Project& project, AppConfig& appConfig, SurfaceView& sharedView) :
	project(project),
	mesh(project.mesh),
	geometry(project.geometry),
	g(mesh.g),
	assets(appConfig.assets),
	BaseSurfaceViewer("graphics/shaders/mesh.vert", "graphics/shaders/mesh.frag", &sharedView) {

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
	GLsizeiptr gridLineBufferBytes =
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

// ---- screen-space geometry for rubber-band selection ----
static bool pointInRect(ImVec2 p, ImVec2 mn, ImVec2 mx) {
	return p.x >= mn.x && p.x <= mx.x &&
		p.y >= mn.y && p.y <= mx.y;
}

// do the two screen-space segments p1p2 and p3p4 cross? (proper/parametric test;
// collinear overlap is left to the endpoint-in-rect check in segmentIntersectsRect)
static bool segmentsCross(ImVec2 p1, ImVec2 p2, ImVec2 p3, ImVec2 p4) {
	auto cross = [](ImVec2 a, ImVec2 b) {
		return a.x * b.y - a.y * b.x;
	};

	ImVec2 r{ p2.x - p1.x, p2.y - p1.y };
	ImVec2 s{ p4.x - p3.x, p4.y - p3.y };

	float rxs = cross(r, s);
	if (std::fabs(rxs) < 1e-6f) {
		return false; // parallel
	}

	ImVec2 qp{ p3.x - p1.x, p3.y - p1.y };
	float t = cross(qp, s) / rxs;
	float u = cross(qp, r) / rxs;

	return t >= 0.0f && t <= 1.0f && u >= 0.0f && u <= 1.0f;
}

// does the screen-space segment [a,b] touch the axis-aligned box [mn,mx]?
static bool segmentIntersectsRect(ImVec2 a, ImVec2 b, ImVec2 mn, ImVec2 mx) {
	if (pointInRect(a, mn, mx) || pointInRect(b, mn, mx)) {
		return true;
	}

	ImVec2 tl{ mn.x, mn.y };
	ImVec2 tr{ mx.x, mn.y };
	ImVec2 br{ mx.x, mx.y };
	ImVec2 bl{ mn.x, mx.y };

	return segmentsCross(a, b, tl, tr) ||
		segmentsCross(a, b, tr, br) ||
		segmentsCross(a, b, br, bl) ||
		segmentsCross(a, b, bl, tl);
}

// ======================================================================
// -----------------------CELL INSPECTION--------------------------------
// ======================================================================
const FVMesh& MeshInspector::inspectMesh() const {
	return mesh.getFVMesh();
}

void MeshInspector::buildInspectMesh() {
	// The FV mesh itself is Mesh's, not ours -- this only makes sure it is current
	// (the mesh type / activeCell dispatch that used to live here moved into
	// Mesh::buildFVMesh, where every consumer gets the same answer).
	mesh.refreshFVMesh();

	// Cell outlines come with it: refreshFVMesh rebuilds Mesh's per-cell corner store
	// in the same call, so anything reading an outline this frame is looking at the
	// mesh that was just built. The inspector used to assemble its own corner quads
	// here, for the multiblock path only.
	inspectMeshDirty = false;
}

int MeshInspector::cellCorners(int cellID, Vec2* out, int maxOut) const {
	const FVMesh& fv = inspectMesh();

	if (!out || cellID < 0 || cellID + 1 >= (int)fv.cellCornerStart.size()) {
		return 0;
	}

	const int begin = fv.cellCornerStart[cellID];
	const int end = fv.cellCornerStart[cellID + 1];

	const int n = end - begin;

	// Cells whose corners could not be recovered carry an empty range, and a polygon
	// too big for the caller's buffer is dropped rather than cut short.
	if (n < 3 || n > maxOut) {
		return 0;
	}

	for (int k = 0; k < n; k++) {
		const int pointID = fv.cellCornerIDs[begin + k];

		if (pointID < 0 || pointID >= (int)fv.points.size()) {
			return 0;
		}

		out[k] = fv.points[pointID];
	}

	return n;
}

int MeshInspector::pickCell(const Vec2& world) const {
	// One polygon test for every mesh path. The corner store is index-aligned with
	// the FVMesh, so the hit IS the cell ID -- no per-path indexing to reconcile.
	//
	// The raster branch this replaced could never return a live cell anyway:
	// Mesh::buildFVMesh routes every structured mesh through createMultiBlockFVMesh,
	// which returns an empty mesh when there are no blocks, so a structured FVMesh
	// with cells in it is always a multiblock one -- and a multiblock cell ID is not
	// a raster i*nz+j.
	const int nCells = (int)inspectMesh().cells.size();

	Vec2 corners[maxCellCorners];

	for (int c = 0; c < nCells; c++) {
		const int n = cellCorners(c, corners, maxCellCorners);

		if (n >= 3 && pointInPolygon(world, corners, n)) {
			return c;
		}
	}

	return -1;
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

	if (selectedCell >= 0) {
		logCellInfoToConsole();
	}
}

// ======================================================================
// -----------------------MOUSE HANDLES----------------------------------
// ======================================================================
void MeshInspector::handleCursor(ImGuiIO& io) {

	// do not run this if any of the toggled tools are active, or if a popup is opened
	bool isPopupOpened = ImGui::IsPopupOpen("Mesh Inspector Popup");
	if (toggleDrawCircle || toggleDrawRect || toggleRuler || isPopupOpened) return;

	// Everything below reacts to a left gesture, and a gesture that began off the
	// canvas is someone else's -- a drag out of another panel, or a click released
	// over us -- so neither the box nor the segment toggle should claim it.
	if (!leftPressedOnCanvas) return;

	// A left-drag across the canvas starts a rubber-band box (panning is on the
	// middle button, so the left drag is free). initLeftMouse already holds the
	// press position; the box runs until release, handled in handleMouse so it
	// survives the cursor slipping off the canvas.
	if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
		isBoxSelecting = true;
		return;
	}

	// A plain left click (no drag): toggle the hovered segment, or clear the
	// selection when clicking empty canvas. Deferred to release so it can be told
	// apart from the start of a box drag.
	if (!ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
		return;
	}

	if (!hoveredId.has_value()) {
		if (!io.KeyCtrl) {
			mesh.selectedBoundaryIDs.clear();
			mesh.highlightedBoundarySegmentIDs.clear();
		}
		return;
	}

	if (!io.KeyCtrl) {
		mesh.selectedBoundaryIDs.clear();
	}

	auto& sel = mesh.selectedBoundaryIDs;
	auto it = sel.find(*hoveredId);
	if (it == sel.end()) sel.insert(*hoveredId);
	else sel.erase(it);
}

void MeshInspector::applyBoxSelection(bool additive) {
	ImVec2 boxMin{
		std::min(initLeftMouse.x, currentMousePos.x),
		std::min(initLeftMouse.y, currentMousePos.y)
	};
	ImVec2 boxMax{
		std::max(initLeftMouse.x, currentMousePos.x),
		std::max(initLeftMouse.y, currentMousePos.y)
	};

	if (!additive) {
		mesh.selectedBoundaryIDs.clear();
	}

	for (const BoundarySegment& seg : mesh.boundarySegments) {
		bool hit = false;

		for (int edgeID : seg.edgeIDs) {
			if (edgeID < 0 ||
				edgeID >= static_cast<int>(mesh.boundaryEdges.size())) {
				continue;
			}

			const BoundaryEdge& edge = mesh.boundaryEdges[edgeID];

			if (!edgeInRange(edge, mesh.boundaryVertices.size())) {
				continue;
			}

			ImVec2 a = camera.worldToScreen(mesh.boundaryVertices[edge.v0].pos);
			ImVec2 b = camera.worldToScreen(mesh.boundaryVertices[edge.v1].pos);

			if (segmentIntersectsRect(a, b, boxMin, boxMax)) {
				hit = true;
				break;
			}
		}

		if (hit) {
			mesh.selectedBoundaryIDs.insert(seg.id);
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

	// the axis lines (r = 0 and z = 0) are deliberately NOT snap candidates: they
	// span the whole canvas, so an ROI drawn anywhere near an axis got pulled onto
	// it. The origin above stays snappable -- a single point, not a line across the
	// view.

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

	// A box selection in progress owns the left button until release, even if the
	// cursor is dragged off the canvas -- handled before the near-image gate so the
	// rubber-band can't get stuck.
	if (isBoxSelecting) {
		if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
			applyBoxSelection(io.KeyCtrl);
			isBoxSelecting = false;
		}
		return;
	}

	// Where the press LANDED decides whether a later drag may act on this canvas, so
	// it is recorded ahead of the near-image gate -- the gate only knows where the
	// cursor is now. Without this, dragging in from outside arrives with the button
	// already down and IsMouseDragging already true, so the rubber-band opened
	// instantly, anchored at whatever stale initLeftMouse the last real press left.
	// The anchor is taken in the same breath, so that whether the gesture started
	// here and where it started cannot come from two different presses.
	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		leftPressedOnCanvas = isMouseNearImage(io);

		if (leftPressedOnCanvas) {
			updateInitialLeftClick(io);
		}
	}
	else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
		!ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {

		// Button is up, and this is not the release frame handleCursor still acts on:
		// forget the press. Only the four tab viewports' active one is submitted per
		// frame, so a press made while another tab was up is never seen here -- and
		// without this the answer left over from the last real canvas press would be
		// inherited by that gesture when the tab switch brought us back mid-drag.
		leftPressedOnCanvas = false;
	}

	// if mouse is not near the image, then dont handle any mouse events
	if (!isMouseNearImage(io)) return;

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
	drawCanvas(drawList, canvasRect, 0.0f);

	drawList->PushClipRect(canvasRect.min, canvasRect.max, true);
	drawAxes(drawList);
	// The cell overlays paint under the mesh lines, so the cells stay readable.
	// Only one of the three ever paints (the toolbar toggles are exclusive), so
	// the order between them is moot.
	drawAspectRatio(drawList);
	drawElementQuality(drawList);
	drawSizing(drawList);
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

	// A structured mesh is always multiblock, so the uniform raster grid (g.zFace /
	// g.rFace) is never what gets drawn -- the block nodes are.
	if (mesh.currentMeshType == MeshType::Structured) {
		// Draw straight from the block node coordinates (real world r-z) through the
		// same camera transform the geometry outline uses, so mesh and geometry line
		// up exactly -- no displayZ/g.L normalize-and-recover round-trip.
		auto worldLine = [&](const MBNode& a, const MBNode& b) {
			drawList->AddLine(
				camera.worldToScreen(Vec2{ a.z, a.r }),
				camera.worldToScreen(Vec2{ b.z, b.r }),
				lineColor,
				1.0f
			);
		};

		for (const Block& b : mesh.multiBlock.blocks) {
			forEachBlockGridSegment(b, worldLine);
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

void MeshInspector::drawBoundarySegments(
	ImDrawList* drawList
) {
	for (const BoundarySegment& seg : mesh.boundarySegments) {
		bool selected =
			mesh.selectedBoundaryIDs.find(seg.id) !=
			mesh.selectedBoundaryIDs.end();

		bool hovered =
			hoveredId.has_value() && *hoveredId == seg.id;

		bool highlighted =
			mesh.highlightedBoundarySegmentIDs.find(seg.id) !=
			mesh.highlightedBoundarySegmentIDs.end();

		ImU32 color = sketchLineColor;
		float thickness = sketchLineThickness;

		if (hovered) {
			color = hoverLineColor;
			thickness = hoverLineThickness;
		}

		// GUI-driven boundary-group highlight â€” a distinct state from hover/select,
		// so it keeps its own color
		if (highlighted) {
			color = IM_COL32(255, 80, 80, 255);
			thickness = hoverLineThickness;
		}

		if (selected) {
			color = hoverLineColor;
			thickness = hoverLineThickness;
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
	// CFD-style ribbon: tools grouped by workflow into named sections
	// (home | region of influence | view). The region tools trade their captions
	// for their section name; the tooltip holds the fuller description.
	beginToolbar();

	// --- home ---
	beginSection();
	if (addImageButton("Reset", "Home", "Reset view", assets.icon("house"))) {
		resetView();
	}
	ImGui::SameLine();
	if (addImageButton("Copy", "Copy", "Copy to clipboard", assets.icon("clipboard")) || consoleCopy) {
		pendingCopyWidth = frameBuffer.width;
		pendingCopyHeight = frameBuffer.height;
		pendingCopy = true;
		consoleCopy = false;
	}
	endSection("Home");

	// --- region of influence ---
	beginSection();
	if (addImageButtonToggle("ROICircle", nullptr, "Draw circular region of influence", assets.icon("draw-circle"), toggleDrawCircle)) {
		toggleDrawRect = false;
		toggleInspectCell = false;
	}
	ImGui::SameLine();
	if (addImageButtonToggle("ROIRect", nullptr, "Draw rectangular region of influence", assets.icon("draw-rectangle"), toggleDrawRect)) {
		toggleDrawCircle = false;
		toggleInspectCell = false;
	}
	endSection("Region of Influence");

	// --- view ---
	beginSection();
	if (addImageButtonToggle("InspectCell", "Inspect", "Inspect cell mesh data (click a cell)", assets.icon("select"), toggleInspectCell)) {
		toggleDrawCircle = false;
		toggleDrawRect = false;
		toggleRuler = false;
		selectedCell = -1;
		inspectMeshDirty = true;
	}
	ImGui::SameLine();
	addImageButtonToggle("ToggleMesh", "Mesh", "Toggle mesh", assets.icon("mesh"), toggleMesh);
	ImGui::SameLine();
	// One overlay at a time: they shade the same cells from the same corner with the
	// same ramp, so two at once would leave the top one's colors over the bottom
	// one's legend.
	if (addImageButtonToggle("AspectRatio", "Aspect", "Shade cells by aspect ratio", assets.icon("quality"), toggleAspectRatio)) {
		toggleElementQuality = false;
		toggleSizing = false;
		inspectMeshDirty = true;	// the overlay needs the cell outlines too
	}
	ImGui::SameLine();
	if (addImageButtonToggle("ElementQuality", "Quality", "Shade cells by element quality", assets.icon("quality"), toggleElementQuality)) {
		toggleAspectRatio = false;
		toggleSizing = false;
		inspectMeshDirty = true;
	}
	ImGui::SameLine();
	if (addImageButtonToggle("Sizing", "Size", "Shade cells by the mesher's target size", assets.icon("quality"), toggleSizing)) {
		toggleAspectRatio = false;
		toggleElementQuality = false;
		inspectMeshDirty = true;
	}
	ImGui::SameLine();
	// A ramp mode for the two shape overlays rather than an overlay itself, so it
	// clears nothing -- and there is nothing for it to rescale unless one is up.
	ImGui::BeginDisabled(!toggleAspectRatio && !toggleElementQuality);
	addImageButtonToggle(
		"LocalScaling",
		"Local",
		"Scale the ramp to this mesh (p1-p99) instead of the metric's fixed band",
		assets.icon("ruler"),
		localQualityScaling
	);
	ImGui::EndDisabled();
	ImGui::SameLine();
	// Its own window, not an overlay, so it clears none of the toggles above and
	// stays readable next to whichever one is up.
	addImageButtonToggle(
		"QualityHistogram",
		"Plot",
		"Plot the distribution of every quality metric",
		assets.icon("add_plot"),
		showQualityHistogram
	);

	endSection("View");

	endToolbar();
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

	// add new boundary group
	if (pendingBoundaryGroup) {
		if (drawNamingPopup("Naming Segment", *pendingBoundaryGroup, mesh.boundaryGroups)) {

			if (mesh.currentMeshType == MeshType::Structured) {
				fillBoundaryGroupEdges(*pendingBoundaryGroup);
			}
			else {
				pendingBoundaryGroup->edges.clear();
			}

			setGroupOrientation(*pendingBoundaryGroup);
			setGroupTotalLength(*pendingBoundaryGroup);

			if (!pendingBoundaryGroup->segmentIDs.empty()) {
				mesh.boundaryGroups.push_back(std::move(*pendingBoundaryGroup));

				// FVFace::boundaryGroupID is stamped when the cached FV mesh is
				// built, so a new group means the cache no longer matches.
				mesh.markFVMeshDirty();
				inspectMeshDirty = true;
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
		buildCombinedBoundaryEdges(mesh.selectableOuterEdges);

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
	const std::unordered_set<MeshEdge, MeshEdgeHash>& selectableOuterEdges
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

void MeshInspector::drawBoxSelection(ImDrawList* drawList) {
	if (!isBoxSelecting) {
		return;
	}

	// same highlighter-blue rubber-band the sketch view's box-select uses, so the
	// two canvases read the same
	const ImU32 fillColor = IM_COL32(125, 220, 255, 35);
	const ImU32 lineColor = IM_COL32(125, 220, 255, 255);

	ImVec2 rectMin{
		std::min(initLeftMouse.x, currentMousePos.x),
		std::min(initLeftMouse.y, currentMousePos.y)
	};
	ImVec2 rectMax{
		std::max(initLeftMouse.x, currentMousePos.x),
		std::max(initLeftMouse.y, currentMousePos.y)
	};

	drawList->AddRectFilled(rectMin, rectMax, fillColor);
	drawList->AddRect(rectMin, rectMax, lineColor, 0.0f, 0, 2.0f);
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

std::string MeshInspector::buildCellInfoText(int cellID) const {
	const FVMesh& fv = inspectMesh();

	if (cellID < 0 || cellID >= (int)fv.cells.size()) {
		return {};
	}

	const FVCell& cell = fv.cells[cellID];
	std::string info;
	char line[160];

	std::snprintf(line, sizeof(line), "Cell #%d", cellID);
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

	info += "\n----------------";

	// --- shape metrics (Mesh measures them alongside the FV mesh) ---
	// Both are triangle-only, so a quad cell reads n/a rather than a wrong number.
	if (cellID < (int)mesh.quality.aspectRatios.size() &&
		std::isfinite(mesh.quality.aspectRatios[cellID])) {
		std::snprintf(line, sizeof(line), "\naspect ratio: %.3f",
			mesh.quality.aspectRatios[cellID]);
		info += line;
	}
	else {
		info += "\naspect ratio: n/a";
	}

	if (cellID < (int)mesh.quality.elementQuality.size() &&
		std::isfinite(mesh.quality.elementQuality[cellID])) {
		std::snprintf(line, sizeof(line), "\nelement quality: %.3f",
			mesh.quality.elementQuality[cellID]);
		info += line;
	}
	else {
		info += "\nelement quality: n/a";
	}

	// --- target size (what the mesher aimed for here, not what it achieved) ---
	const double targetSize = cellTargetSize(cellID);

	if (targetSize > 0.0) {
		std::snprintf(line, sizeof(line), "\ntarget size (%s): %.4g",
			currentUnitName, targetSize * project.lengthScale.value);
		info += line;
	}

	// --- per-face geometry: neighbour, edge length, face non-orthogonality ---
	if (!cell.faceIDs.empty()) {
		info += "\nfaces (nb | len | non-orth):";

		constexpr double radToDeg = 57.29577951308232;

		for (int fid : cell.faceIDs) {
			if (fid < 0 || fid >= (int)fv.faces.size()) {
				continue;
			}

			const FVFace& f = fv.faces[fid];

			if (f.neighbor < 0) {
				std::snprintf(line, sizeof(line), "\n  f%-5d bdry   %.4g", fid, f.length2D);
				info += line;
				continue;
			}

			int nb = (f.owner == cellID) ? f.neighbor : f.owner;

			const FVCell& P = fv.cells[f.owner];
			const FVCell& N = fv.cells[f.neighbor];

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

	return info;
}

void MeshInspector::logCellInfoToConsole() {
	if (!console) {
		return;
	}

	std::string info = buildCellInfoText(selectedCell);
	if (info.empty()) {
		return;
	}

	console->addSeparator();
	console->addLine(info);
	console->addSeparator();
}

void MeshInspector::drawCellInfo(ImDrawList* drawList) {
	if (selectedCell < 0) {
		return;
	}

	if (selectedCell >= (int)inspectMesh().cells.size()) {
		selectedCell = -1; // stale selection (mesh changed underneath us)
		return;
	}

	ImVec2 canvasMin = canvasRect.min;
	ImVec2 canvasMax = canvasRect.max;

	const ImU32 fillCol = IM_COL32(255, 235, 60, 70);
	const ImU32 lineCol = IM_COL32(255, 235, 60, 255);

	drawList->PushClipRect(canvasMin, canvasMax, true);

	Vec2 corners[maxCellCorners];
	ImVec2 pts[maxCellCorners];

	const int n = cellCorners(selectedCell, corners, maxCellCorners);

	for (int k = 0; k < n; k++) {
		pts[k] = camera.worldToScreen(corners[k]);
	}

	if (n >= 3) {
		drawList->AddConvexPolyFilled(pts, n, fillCol);
		drawList->AddPolyline(pts, n, lineCol, ImDrawFlags_Closed, 2.0f);
	}

	drawList->PopClipRect();
}

void MeshInspector::drawQualityOverlay(
	ImDrawList* drawList,
	const std::vector<double>& values,
	double lo,
	double hi,
	const char* label
) {
	const FVMesh& fv = inspectMesh();

	// Mesh::refreshFVMesh measures the metrics as it builds the cells, so a length
	// mismatch means we are looking at a mesh nobody has measured yet (a project
	// mid-load, or one whose geometry moved). Shading it would color cells by
	// another mesh's numbers, so draw nothing instead.
	if (values.size() != fv.cells.size() || values.empty()) {
		return;
	}

	const double span = hi - lo;
	const int alpha = toggleMesh ? 150 : 200;	// keep the mesh lines legible on top

	bool painted = false;

	drawList->PushClipRect(canvasRect.min, canvasRect.max, true);

	// One outline path for every mesh type. The corner store is rebuilt by the same
	// refreshFVMesh call that measures the cells, so cell c means the same thing in
	// both -- which is what the old three-way dispatch (multiblock quads, raster
	// grid, triangulation) had to establish by matching cell counts, since a cell ID
	// meant something different in each and painting through the wrong one drew the
	// whole field in the wrong places.
	Vec2 corners[maxCellCorners];
	ImVec2 pts[maxCellCorners];

	for (int c = 0; c < (int)values.size(); c++) {
		const double v = values[c];

		// Every metric reports a cell it could not measure as NaN -- a quad under a
		// triangle formula, a cell with no sizing, a degenerate one whose ratio came
		// back infinite. Skip it rather than paint it the shade a clamp would give.
		if (!std::isfinite(v)) {
			continue;
		}

		const int n = cellCorners(c, corners, maxCellCorners);

		if (n < 3) {
			continue;
		}

		for (int k = 0; k < n; k++) {
			pts[k] = camera.worldToScreen(corners[k]);
		}

		drawList->AddConvexPolyFilled(
			pts,
			n,
			inspectorQualityColor((v - lo) / span, alpha)
		);
		painted = true;
	}

	drawList->PopClipRect();

	// A legend over an unshaded canvas would be claiming a scale nothing was drawn
	// against, so it only goes up once at least one cell carries a color.
	if (painted) {
		drawQualityLegend(drawList, lo, hi, label);
	}
}

void MeshInspector::drawAspectRatio(ImDrawList* drawList) {
	if (!toggleAspectRatio) {
		return;
	}

	// Global: 1.0 -- an equilateral cell -- is the green end and 2.0 the red end.
	// Absolute, so the same colour means the same shape on every mesh, but a good
	// mesh reads flat green (measured: a 2x1 box tops out at 1.12, a 5 degree wedge
	// tip reaches 29.8). Local trades that comparability for spread across whatever
	// this mesh actually contains.
	double lo = 1.0;
	double hi = 2.0;
	const char* label = "aspect";

	double p1 = 0.0;
	double p99 = 0.0;

	if (localQualityScaling && inspectorPercentiles(mesh.quality.aspectRatios, p1, p99)) {
		lo = p1;		// small is good here, so p1 is the green end
		hi = p99;
		label = "aspect (local)";
	}

	drawQualityOverlay(drawList, mesh.quality.aspectRatios, lo, hi, label);
}

void MeshInspector::drawElementQuality(ImDrawList* drawList) {
	if (!toggleElementQuality) {
		return;
	}

	// Global: the mean ratio is already normalized -- 1 equilateral, 0 degenerate --
	// so the fixed ramp is its full definition range. lo and hi run backwards
	// throughout because here the LARGEST value is the good one; an inverted
	// triangle comes out negative and pins at red, which is correct.
	double lo = 1.0;
	double hi = 0.0;
	const char* label = "quality";

	double p1 = 0.0;
	double p99 = 0.0;

	if (localQualityScaling && inspectorPercentiles(mesh.quality.elementQuality, p1, p99)) {
		lo = p99;		// large is good here, so p99 is the green end
		hi = p1;
		label = "quality (local)";
	}

	drawQualityOverlay(drawList, mesh.quality.elementQuality, lo, hi, label);
}

double MeshInspector::cellTargetSize(int cellID) const {
	const FVMesh& fv = inspectMesh();
	const std::vector<double>& h = mesh.unstructuredSizing;

	// The sizing field is indexed by mesh node, and on the triangulated path the FV
	// mesh's point list IS that node list -- so matching lengths is what says the two
	// belong to the same mesh. A structured/multiblock FVMesh or a loaded project
	// fails this and has no sizing to report.
	if (h.empty() || h.size() != fv.points.size()) {
		return -1.0;
	}

	if (cellID < 0 || cellID + 1 >= (int)fv.cellCornerStart.size()) {
		return -1.0;
	}

	const int begin = fv.cellCornerStart[cellID];
	const int end = fv.cellCornerStart[cellID + 1];

	double sum = 0.0;
	int n = 0;

	for (int k = begin; k < end; k++) {
		const int pointID = fv.cellCornerIDs[k];

		if (pointID < 0 || pointID >= (int)h.size()) {
			return -1.0;
		}

		// one unmeasured corner makes the average meaningless, so drop the cell
		// rather than report a mean of the corners that happen to have a size
		if (!std::isfinite(h[pointID]) || h[pointID] <= 0.0) {
			return -1.0;
		}

		sum += h[pointID];
		n++;
	}

	return n > 0 ? sum / n : -1.0;
}

void MeshInspector::drawSizing(ImDrawList* drawList) {
	if (!toggleSizing) {
		return;
	}

	const FVMesh& fv = inspectMesh();

	if (fv.cells.empty() || mesh.unstructuredSizing.size() != fv.points.size()) {
		return;
	}

	// The overlay shades cells, so the nodal field is collapsed to one value per cell
	// here and handed to the shared ramp. Display units, so the legend reads in
	// whatever the project shows lengths in rather than in metres.
	const double toDisplay = project.lengthScale.value;

	std::vector<double> values(fv.cells.size(), std::numeric_limits<double>::quiet_NaN());

	double lo = std::numeric_limits<double>::max();
	double hi = -std::numeric_limits<double>::max();

	for (int c = 0; c < (int)values.size(); c++) {
		const double size = cellTargetSize(c);

		if (size <= 0.0) {
			continue;	// left at NaN, which the ramp skips as unmeasurable
		}

		values[c] = size * toDisplay;

		lo = std::min(lo, values[c]);
		hi = std::max(hi, values[c]);
	}

	if (lo > hi) {
		return;		// nothing measurable on this mesh
	}

	// A perfectly uniform field would divide by a zero span. Nudge the top end so it
	// all paints at the fine end instead of turning every cell into a NaN color.
	if (hi <= lo) {
		hi = lo + std::max(lo * 1e-6, std::numeric_limits<double>::min());
	}

	// Unlike the shape metrics there is no fixed range to label this with -- the
	// scale is the field's own, so the unit has to be on the legend to mean anything.
	const std::string label = std::string("size (") + currentUnitName + ")";

	drawQualityOverlay(drawList, values, lo, hi, label.c_str());
}

void MeshInspector::drawQualityLegend(
	ImDrawList* drawList,
	double lo,
	double hi,
	const char* label
) {
	// Three metrics share this corner and the same green-to-red ramp, so without the
	// end numbers and the metric name a color says nothing about which scale it is
	// on -- 1 is a flawless cell on both shape bars, but it is the bottom of the
	// aspect bar and the top of the quality one.
	const float pad = 10.0f;
	const float barWidth = 16.0f;
	const float barHeight = 110.0f;
	const float lineHeight = ImGui::GetTextLineHeight();

	ImVec2 barMin(
		canvasRect.max.x - pad - barWidth,
		canvasRect.min.y + pad + lineHeight
	);
	ImVec2 barMax(barMin.x + barWidth, barMin.y + barHeight);

	// worst at the top, best at the bottom -- the way a colorbar is read
	const int steps = 32;
	for (int s = 0; s < steps; s++) {
		float y0 = barMin.y + barHeight * (float)s / steps;
		float y1 = barMin.y + barHeight * (float)(s + 1) / steps;

		double t = 1.0 - ((double)s + 0.5) / steps;

		drawList->AddRectFilled(
			ImVec2(barMin.x, y0),
			ImVec2(barMax.x, y1),
			inspectorQualityColor(t, 255)
		);
	}

	const ImU32 textColor = IM_COL32(230, 235, 245, 255);
	const ImU32 frameColor = IM_COL32(90, 100, 120, 255);

	drawList->AddRect(barMin, barMax, frameColor);

	// right-aligned against the bar, so the numbers stay put as they change width
	auto addLabel = [&](const char* text, float y) {
		float width = ImGui::CalcTextSize(text).x;
		drawList->AddText(ImVec2(barMin.x - 6.0f - width, y), textColor, text);
	};

	char endLabel[32];

	std::snprintf(endLabel, sizeof(endLabel), "%.3g", hi);
	addLabel(endLabel, barMin.y - 2.0f);

	std::snprintf(endLabel, sizeof(endLabel), "%.3g", lo);
	addLabel(endLabel, barMax.y - lineHeight + 2.0f);

	addLabel(label, canvasRect.min.y + pad);
}

void MeshInspector::render() {
	setBaseNrNz();

	updateLengthScale(
		project.lengthScale.value,
		Units::lengthUnits[project.lengthScale.index].name
	);

	ImGui::SetNextWindowClass(&windowClass);
	ImGui::Begin(UIViewport::MeshInspectorTitle);

	ImDrawList* drawList = ImGui::GetWindowDrawList();

	// toolbar lives in the app-wide strip above the dockspace (GUI::drawAppToolbar)

	ImVec2 pos = ImGui::GetCursorScreenPos();
	ImVec2 size = ImGui::GetContentRegionAvail();
	canvasRect = makePaddedRect(pos, size);

	resizeImage();

	camera.setDimensions(
		canvasRect.size.x,
		canvasRect.size.y,
		canvasRect.min
	);

	// recenter/re-zoom to the loaded project's units if a reset was requested
	applyPendingResetView();

	// buildSegments() is never called for a structured mesh any more: it rebuilds the
	// outline from the uniform raster grid (g.rFace/g.zFace), which does NOT coincide
	// with the trellis block nodes drawMeshLines() draws from, so the outline would
	// snap to a staircase offset from the mesh lines. Every structured mesh is
	// multiblock, so all of them keep the exact sketch-derived boundary from
	// convertSketchToStructuredMesh, which shares the trellis coords.

	// update current global mouse pos
	updateCurrentMousePos();

	// keep the inspection snapshot in sync while anything that reads it is active.
	// The quality overlays need it for the same reason picking does: both read the
	// per-cell corner store, which is only rebuilt when the FVMesh is.
	if ((toggleInspectCell || qualityOverlayActive()) && inspectMeshDirty) {
		buildInspectMesh();
	}

	// Update hover before mouse logic (suppressed while inspecting cells)
	if (toggleInspectCell) {
		hoveredId = std::nullopt;
	}
	else {
		hoveredId = findHoveredBoundarySegment();
	}

	// Now handle mouse using current hoveredId/current segments
	handleMouse();


	// same fill/border as the sketch view so the two 2D canvases match
	drawCanvas(drawList, canvasRect, 0.0f, canvasBgColor, canvasOutlineColor);

	drawList->PushClipRect(canvasRect.min, canvasRect.max, true);
	drawAxes(drawList);
	// The cell overlays paint under the mesh lines, so the cells stay readable.
	// Only one of the three ever paints (the toolbar toggles are exclusive), so
	// the order between them is moot.
	drawAspectRatio(drawList);
	drawElementQuality(drawList);
	drawSizing(drawList);
	drawMeshLines(drawList);
	drawRegionsOfInfluence(drawList);
	drawPendingObjects(drawList);
	drawSnapping(drawList);
	drawBoundarySegments(drawList);
	drawBoxSelection(drawList);
	drawTextAtSurfacePoint(drawList);
	if (toggleInspectCell) {
		drawCellInfo(drawList);
	}
	drawList->PopClipRect();

	drawPopup();

	ImGui::End();

	// Outside the viewport's Begin/End: this is a top-level window of its own.
	drawQualityHistogramWindow();
}

void MeshInspector::drawQualityHistogramWindow() {
	if (!showQualityHistogram) {
		return;
	}

	// No window class here, unlike every other panel: this one is meant to be
	// dragged and docked wherever the user wants it, which is exactly what the
	// viewers' NoDockWindowFlags class forbids.
	ImGui::SetNextWindowSize(ImVec2(520.0f, 560.0f), ImGuiCond_FirstUseEver);

	// The window's own close button drives the toolbar toggle, so the two cannot
	// disagree about whether it is up.
	if (ImGui::Begin("Mesh Quality###MeshQualityHistograms", &showQualityHistogram)) {
		drawMeshQualityHistograms(mesh.quality, histogramLogCount);
	}

	ImGui::End();
}
