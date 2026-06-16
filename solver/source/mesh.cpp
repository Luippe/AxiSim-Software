#include "mesh.h"
#include "console.h"

#include "CDT.h"
#include <iostream>
#include "time_manager.h"
#include "solver_struct.h"
#include "printer.h"
#include <glm/trigonometric.hpp>
#include <algorithm>

#include "math_func.h"



Mesh::Mesh(Config& config) : g(config.g) {
	initializeUnstructuredDomain(5,5);
}

PointKey makePointKey(Vec2 p, double tol) {
	return PointKey{
		(long long)std::llround(p.z / tol),
		(long long)std::llround(p.r / tol)
	};
}

bool Mesh::hasDomainBoundarySegments() const {
	for (const BoundarySegment& seg : boundarySegments) {
		if (seg.source == BoundarySource::Domain) {
			return true;
		}
	}
	return false;
}

int Mesh::createObstacleBoundaryGroup(const std::string& name) {
	BoundarySegmentGroup obstacle{};

	obstacle.id = getAvailableBoundaryGroupID();
	obstacle.name = name;
	obstacle.type = BoundaryType::WALL;

	std::snprintf(
		obstacle.nameBuffer,
		sizeof(obstacle.nameBuffer),
		"%s",
		obstacle.name.c_str()
	);

	boundaryGroups.push_back(obstacle);

	return obstacle.id;
}

void Mesh::clearUnstructuredGeometry() {
	unstructuredPoints.clear();
	unstructuredTriangles.clear();

	boundaryVertices.clear();
	boundaryEdges.clear();
	boundarySegments.clear();
	boundaryGroups.clear();

	selectedBoundaryIDs.clear();
	highlightedBoundarySegmentIDs.clear();

	vertices.clear();
	indices.clear();
	gridLineVertices.clear();
}

void Mesh::initializeUnstructuredDomain(
	int nzPoints,
	int nrPoints
) {
	meshType = MeshType::Unstructured;

	clearUnstructuredGeometry();

	// ----------------------------
	// 1. Create base point cloud
	// ----------------------------
	for (int i = 0; i < nrPoints; i++) {
		double r = g.R * static_cast<double>(i) /
			static_cast<double>(nrPoints - 1);

		for (int j = 0; j < nzPoints; j++) {
			double z = g.L * static_cast<double>(j) /
				static_cast<double>(nzPoints - 1);

			unstructuredPoints.push_back({ z, r });
		}
	}

	// ----------------------------
	// 2. Create boundary vertices
	// For this initial grid:
	// boundaryVertexID == pointID
	// ----------------------------
	for (int n = 0; n < static_cast<int>(unstructuredPoints.size()); n++) {
		BoundaryVertex v{};
		v.id = n;
		v.pointID = n;
		v.pos = unstructuredPoints[n];
		v.hasGridVertex = false;

		boundaryVertices.push_back(v);
	}

	// ----------------------------
	// 3. Add four unnamed side segments
	// ----------------------------
	createDefaultUnstructuredDomainBoundarySegments(
		nzPoints,
		nrPoints
	);
}

void Mesh::createDefaultUnstructuredDomainBoundarySegments(
	int nzPoints,
	int nrPoints
) {

	if (hasDomainBoundarySegments()) {
		return;
	}

	auto pointID = [&](int i, int j) {
		return i * nzPoints + j;
		};

	// r = 0 side
	std::vector<int> bottomVertices;
	for (int j = 0; j < nzPoints; j++) {
		bottomVertices.push_back(pointID(0, j));
	}

	addBoundarySegmentFromVertices(
		bottomVertices,
		BoundarySource::Domain
	);

	// r = R side
	std::vector<int> topVertices;
	for (int j = 0; j < nzPoints; j++) {
		topVertices.push_back(pointID(nrPoints - 1, j));
	}

	addBoundarySegmentFromVertices(
		topVertices,
		BoundarySource::Domain
	);

	// z = 0 side
	std::vector<int> leftVertices;
	for (int i = 0; i < nrPoints; i++) {
		leftVertices.push_back(pointID(i, 0));
	}

	addBoundarySegmentFromVertices(
		leftVertices,
		BoundarySource::Domain
	);

	// z = L side
	std::vector<int> rightVertices;
	for (int i = 0; i < nrPoints; i++) {
		rightVertices.push_back(pointID(i, nzPoints - 1));
	}

	addBoundarySegmentFromVertices(
		rightVertices,
		BoundarySource::Domain
	);
}

int Mesh::addBoundarySegmentFromVertices(
	const std::vector<int>& vertexIDs,
	BoundarySource source
) {
	if (vertexIDs.size() < 2) {
		return -1;
	}

	int segmentID = (int)(boundarySegments.size());

	BoundarySegment segment{};
	segment.id = segmentID;
	segment.groupID = -1;
	segment.loopID = -1;
	segment.source = source;

	for (int vertexID : vertexIDs) {
		if (vertexID < 0 ||
			vertexID >= (int)(boundaryVertices.size())) {
			continue;
		}

		segment.controlPoints.push_back(
			boundaryVertices[vertexID].pos
		);
	}

	if (segment.controlPoints.size() < 2) {
		return -1;
	}

	for (int k = 0; k < (int)(vertexIDs.size()) - 1; k++) {
		int v0 = vertexIDs[k];
		int v1 = vertexIDs[k + 1];

		if (v0 < 0 || v1 < 0) {
			continue;
		}

		if (v0 >= (int)(boundaryVertices.size()) ||
			v1 >= (int)(boundaryVertices.size())) {
			continue;
		}

		int edgeID = (int)(boundaryEdges.size());

		BoundaryEdge edge{};
		edge.id = edgeID;
		edge.v0 = v0;
		edge.v1 = v1;
		edge.segmentID = segmentID;
		edge.groupID = -1;
		edge.source = source;
		edge.hasMeshEdge = false;

		boundaryEdges.push_back(edge);
		segment.edgeIDs.push_back(edgeID);
	}

	if (segment.edgeIDs.empty()) {
		return -1;
	}

	boundarySegments.push_back(segment);

	return segmentID;
}

void Mesh::addCircularObstacle(
	Vec2 center,
	double radius,
	int nObstaclePoints
) {
	nObstaclePoints = std::max(nObstaclePoints, 8);

	int segmentID = (int)(boundarySegments.size());

	BoundarySegment segment{};
	segment.id = segmentID;
	segment.groupID = -1;
	segment.loopID = getAvailableLoopID();
	segment.source = BoundarySource::Obstacle;

	segment.sizing.enabled = true;
	segment.sizing.targetSpacing =
		2.0 * PI * radius / (double)(nObstaclePoints);
	segment.sizing.bias = 1.0;

	std::vector<int> vertexIDs;
	vertexIDs.reserve(nObstaclePoints);

	for (int k = 0; k < nObstaclePoints; k++) {
		double theta =
			2.0 * PI * (double)(k) /
			(double)(nObstaclePoints);

		Vec2 p{};
		p.z = center.z + radius * std::cos(theta);
		p.r = center.r + radius * std::sin(theta);

		segment.controlPoints.push_back(p);

		int vertexID = addUnstructuredBoundaryVertex(p);
		vertexIDs.push_back(vertexID);
	}

	if (!segment.controlPoints.empty()) {
		segment.controlPoints.push_back(segment.controlPoints.front());
	}

	for (int k = 0; k < nObstaclePoints; k++) {
		int v0 = vertexIDs[k];
		int v1 = vertexIDs[(k + 1) % nObstaclePoints];

		if (v0 == v1) {
			continue;
		}

		int edgeID = (int)(boundaryEdges.size());

		BoundaryEdge edge{};
		edge.id = edgeID;
		edge.v0 = v0;
		edge.v1 = v1;
		edge.segmentID = segmentID;
		edge.groupID = -1;
		edge.source = BoundarySource::Obstacle;
		edge.hasMeshEdge = false;

		boundaryEdges.push_back(edge);
		segment.edgeIDs.push_back(edgeID);
	}

	boundarySegments.push_back(segment);
}

void Mesh::runConstrainedDelaunay() {
	unstructuredTriangles.clear();

	std::vector<CDTConstraintEdge> constraints;
	constraints.reserve(boundaryEdges.size());

	for (const BoundaryEdge& edge : boundaryEdges) {
		if (edge.v0 < 0 || edge.v1 < 0) {
			continue;
		}

		if (!edgeInRange(edge, boundaryVertices.size())) continue;

		const BoundaryVertex& a = boundaryVertices[edge.v0];
		const BoundaryVertex& b = boundaryVertices[edge.v1];

		if (a.pointID < 0 || b.pointID < 0) {
			continue;
		}

		constraints.push_back({
			static_cast<std::size_t>(a.pointID),
			static_cast<std::size_t>(b.pointID)
			});
	}

	CDT::Triangulation<double> cdt;

	cdt.insertVertices(
		unstructuredPoints.begin(),
		unstructuredPoints.end(),
		[](const Vec2& p) { return p.z; },
		[](const Vec2& p) { return p.r; }
	);

	cdt.insertEdges(
		constraints.begin(),
		constraints.end(),
		[](const CDTConstraintEdge& e) { return e.v0; },
		[](const CDTConstraintEdge& e) { return e.v1; }
	);

	cdt.eraseOuterTrianglesAndHoles();

	for (const CDT::Triangle& t : cdt.triangles) {
		Triangle tri{};

		tri.v0 = static_cast<int>(t.vertices[0]);
		tri.v1 = static_cast<int>(t.vertices[1]);
		tri.v2 = static_cast<int>(t.vertices[2]);

		unstructuredTriangles.push_back(tri);
	}
}

float Mesh::displayZ(double z) const {
	return static_cast<float>(2.0 * z / g.L - 1.0);
}

float Mesh::displayR(double r) const {
	return static_cast<float>(2.0 * r / g.R - 1.0);
}

inline Vec2 triangleCentroid(Vec2 a, Vec2 b, Vec2 c) {
	return Vec2{
		(a.z + b.z + c.z) / 3.0,
		(a.r + b.r + c.r) / 3.0
	};
}

inline double triangleArea2D(Vec2 a, Vec2 b, Vec2 c) {
	double dz1 = b.z - a.z;
	double dr1 = b.r - a.r;

	double dz2 = c.z - a.z;
	double dr2 = c.r - a.r;

	return 0.5 * std::abs(dz1 * dr2 - dr1 * dz2);
}

inline int cellID(int i, int j, int nz) {
	return i * nz + j;
}

inline double axialAreaFull(double r0, double r1) {
	// Full circular annulus area normal to z direction
	return PI * (r1 * r1 - r0 * r0);
}

inline double radialAreaFull(double r, double dz) {
	// Full cylindrical surface area normal to r direction
	return 2.0 * PI * r * dz;
}

inline double cellVolumeFull(double r0, double r1, double dz) {
	// Full axisymmetric cell volume
	return PI * (r1 * r1 - r0 * r0) * dz;
}

MeshEdge makeAxialEdge(int i, int jFace) {
	MeshEdge edge{};

	edge.i = i;
	edge.j = jFace;
	edge.orient = EdgeOrient::Vertical; // rename to match your code

	return edge;
}

MeshEdge makeRadialEdge(int iFace, int j) {
	MeshEdge edge{};

	edge.i = iFace;
	edge.j = j;
	edge.orient = EdgeOrient::Horizontal; // rename to match your code

	return edge;
}

bool isFluidCell(
	int i,
	int j,
	int nr,
	int nz,
	const std::vector<uint8_t>& activeCell
) {
	if (i < 0 || i >= nr) return false;
	if (j < 0 || j >= nz) return false;

	return activeCell[cellID(i, j, nz)] != 0;
}

std::unordered_map<MeshEdge, int, MeshEdgeHash>
createBoundaryEdgeLookup(const std::vector<BoundarySegmentGroup>& groups) {
	std::unordered_map<MeshEdge, int, MeshEdgeHash> lookup;

	for (const BoundarySegmentGroup& group : groups) {
		for (const MeshEdge& edge : group.edges) {

			auto [it, inserted] = lookup.emplace(edge, group.id);

			if (!inserted) {
				// Same edge was already assigned to another group.
				// You can either overwrite, warn, or keep the first one.
				it->second = group.id;
			}
		}
	}

	return lookup;
}

bool Mesh::isClosedControlPath(const BoundarySegment& seg) const {
	if (seg.controlPoints.size() < 3) {
		return false;
	}

	Vec2 a = seg.controlPoints.front();
	Vec2 b = seg.controlPoints.back();

	double dz = b.z - a.z;
	double dr = b.r - a.r;

	return dz * dz + dr * dr < 1e-24;
}

void Mesh::rebuildBoundaryDiscretization() {
	boundaryVertices.clear();
	boundaryEdges.clear();
	unstructuredPoints.clear();

	std::unordered_map<PointKey, int, PointKeyHash> vertexLookup;

	double scale = std::max(g.L, g.R);
	scale = std::max(scale, 1.0);

	double tol = 1e-10 * scale;

	for (BoundarySegment& seg : boundarySegments) {
		rebuildSegmentDiscretization(seg, vertexLookup, tol);
	}
}

void Mesh::rebuildSegmentDiscretization(
	BoundarySegment& seg,
	std::unordered_map<PointKey, int, PointKeyHash>& vertexLookup,
	double tol
) {
	seg.edgeIDs.clear();

	if (seg.controlPoints.size() < 2) {
		return;
	}

	BoundarySizing sizing = getSizingForSegment(seg);

	double length = pathLength(seg.controlPoints);

	bool closed = isClosedControlPath(seg);


	int nEdges = getNumberOfEdgesForSegment(
		seg,
		sizing,
		length,
		closed
	);



	double bias = sizing.bias;

	if (bias <= 1e-12) {
		bias = 1.0;
	}

	std::vector<int> vertexIDs;
	vertexIDs.reserve(nEdges + 1);

	int nVertices = closed ? nEdges : nEdges + 1;

	for (int k = 0; k < nVertices; k++) {
		double s = (double)(k) / (double)(nEdges);
		double t = biasedT(s, bias);

		Vec2 p = interpolatePath(seg.controlPoints, t);

		PointKey key = makePointKey(p, tol);

		int vertexID = -1;

		auto it = vertexLookup.find(key);

		if (it != vertexLookup.end()) {
			vertexID = it->second;
		}
		else {
			int pointID = (int)(unstructuredPoints.size());
			unstructuredPoints.push_back(p);

			vertexID = (int)(boundaryVertices.size());

			BoundaryVertex bv{};
			bv.id = vertexID;
			bv.pointID = pointID;
			bv.pos = p;
			bv.hasGridVertex = false;

			boundaryVertices.push_back(bv);

			vertexLookup[key] = vertexID;
		}

		vertexIDs.push_back(vertexID);
	}

	int edgeCount = nEdges;

	for (int k = 0; k < edgeCount; k++) {
		int v0 = vertexIDs[k];

		int v1 = closed ?
			vertexIDs[(k + 1) % (int)(vertexIDs.size())] :
			vertexIDs[k + 1];

		if (v0 == v1) {
			continue;
		}

		Vec2 p0 = boundaryVertices[v0].pos;
		Vec2 p1 = boundaryVertices[v1].pos;

		double dz = p1.z - p0.z;
		double dr = p1.r - p0.r;

		if (dz * dz + dr * dr < 1e-24) {
			continue;
		}

		int edgeID = (int)(boundaryEdges.size());

		BoundaryEdge edge{};
		edge.id = edgeID;
		edge.v0 = v0;
		edge.v1 = v1;
		edge.segmentID = seg.id;
		edge.groupID = seg.groupID;
		edge.source = seg.source;
		edge.hasMeshEdge = false;

		boundaryEdges.push_back(edge);
		seg.edgeIDs.push_back(edgeID);
	}
}

int Mesh::getNumberOfEdgesForSegment(
	const BoundarySegment& seg,
	const BoundarySizing& sizing,
	double length,
	bool closed
) const {
	int nEdges = 1;

	if (sizing.enabled) {
		if (sizing.mode == BoundarySizingMode::TargetSpacing) {
			if (sizing.targetSpacing > 1e-30) {
				nEdges = (int)(std::ceil(length / sizing.targetSpacing));
			}
		}
		else if (sizing.mode == BoundarySizingMode::EdgeCount) {
			nEdges = sizing.edgeCount;
		}
	}

	nEdges = std::max(nEdges, 1);

	if (closed) {
		nEdges = std::max(nEdges, 15);
	}

	return nEdges;
}

void Mesh::addInteriorPoints(
	std::vector<Vec2>& points,
	double zMin,
	double zMax,
	double rMin,
	double rMax,
	double spacing
) {
	int row = 0;

	for (double r = rMin + spacing; r < rMax; r += spacing) {
		double zOffset = (row % 2 == 0) ? 0.0 : 0.5 * spacing;

		for (double z = zMin + spacing + zOffset; z < zMax; z += spacing) {

			Vec2 p;
			p.z = z;
			p.r = r;

			points.push_back(p);
		}

		row++;
	}
}

BoundarySizing Mesh::getSizingForSegment(const BoundarySegment& seg) const {
	if (seg.groupID >= 0) {
		for (const BoundarySegmentGroup& group : boundaryGroups) {
			if (group.id == seg.groupID) {
				return group.sizing;
			}
		}
	}

	return seg.sizing;
}

std::unordered_set<int> Mesh::getSegmentIDsInSameLoop(int segmentID) const {
	std::unordered_set<int> ids;

	const BoundarySegment* target = nullptr;

	for (const BoundarySegment& seg : boundarySegments) {
		if (seg.id == segmentID) {
			target = &seg;
			break;
		}
	}

	if (!target) {
		return ids;
	}

	// If this segment is not part of a loop, just return itself.
	if (target->loopID < 0) {
		ids.insert(segmentID);
		return ids;
	}

	for (const BoundarySegment& seg : boundarySegments) {
		if (seg.loopID == target->loopID) {
			ids.insert(seg.id);
		}
	}

	return ids;
}

int getBoundaryGroupID(
	const std::unordered_map<MeshEdge, int, MeshEdgeHash>& lookup,
	const MeshEdge& edge
) {
	auto it = lookup.find(edge);

	if (it == lookup.end()) {
		return -1;
	}

	return it->second;
}

void Mesh::generate() {
	Clock::time_point startTime = startTimer();

	if (meshType == MeshType::Structured) {
		createGrid();
		createGridVertices();
		createGridLineVertices();
		createCylinderVertices();
	}
	else {
		rebuildBoundaryDiscretization();
		addInteriorPoints(unstructuredPoints, 0.0, g.L, 0.0, g.R, 0.0001);

		runConstrainedDelaunay();


		FVMesh fvMesh = createUnstructuredMesh(
			unstructuredPoints,
			unstructuredTriangles,
			boundaryVertices,
			boundaryEdges
		);

		createUnstructuredVertices(
			unstructuredPoints,
			unstructuredTriangles
		);

		createUnstructuredLineVertices(
			unstructuredPoints,
			fvMesh
		);
	}

	console->addCompletionMessage("Completed generating buffers");

	isReady = true;

	float endTime = endTimer(startTime);
	console->addCompletionTime("Mesh", endTime);
}

int Mesh::addUnstructuredBoundaryVertex(Vec2 p) {
	int pointID = static_cast<int>(unstructuredPoints.size());
	unstructuredPoints.push_back(p);

	int vertexID = static_cast<int>(boundaryVertices.size());

	BoundaryVertex bv{};
	bv.id = vertexID;
	bv.pointID = pointID;
	bv.pos = p;
	bv.hasGridVertex = false;

	boundaryVertices.push_back(bv);

	return vertexID;
}


FVMesh Mesh::createUnstructuredMesh(
	const std::vector<Vec2>& points,
	const std::vector<Triangle>& triangles,
	const std::vector<BoundaryVertex>& boundaryVertices,
	const std::vector<BoundaryEdge>& boundaryEdges
) const {
	FVMesh mesh;

	mesh.nr = 0;
	mesh.nz = 0;

	mesh.cells.resize(triangles.size());

	// -------------------------
	// 1. Create cells
	// -------------------------
	for (int c = 0; c < (int)(triangles.size()); c++) {
		const Triangle& tri = triangles[c];

		Vec2 a = points[tri.v0];
		Vec2 b = points[tri.v1];
		Vec2 d = points[tri.v2];

		FVCell& cell = mesh.cells[c];

		cell.center = triangleCentroid(a, b, d);
		cell.area2D = triangleArea2D(a, b, d);
		cell.volume = 2.0 * PI * cell.center.r * cell.area2D;

		cell.active = true;
		cell.solid = false;
		cell.faceIDs.clear();
	}

	// boundary edge -> boundary group
	std::unordered_map<EdgeKey, int, EdgeKeyHash> boundaryLookup;

	for (const BoundaryEdge& edge : boundaryEdges) {
		if (edge.groupID < 0) {
			continue;
		}

		if (!edgeInRange(edge, boundaryVertices.size())) continue;

		const BoundaryVertex& a = boundaryVertices[edge.v0];
		const BoundaryVertex& b = boundaryVertices[edge.v1];

		if (a.pointID < 0 || b.pointID < 0) {
			continue;
		}

		boundaryLookup[EdgeKey(a.pointID, b.pointID)] = edge.groupID;
	}

	// triangle edge -> face ID
	std::unordered_map<EdgeKey, int, EdgeKeyHash> edgeToFace;

	auto addTriangleEdge = [&](int ownerCellID, int v0, int v1) {
		EdgeKey key(v0, v1);

		auto it = edgeToFace.find(key);

		if (it == edgeToFace.end()) {
			FVFace face{};

			face.owner = ownerCellID;
			face.neighbor = -1;
			face.boundaryGroupID = -1;

			face.v0 = v0;
			face.v1 = v1;

			Vec2 p0 = points[v0];
			Vec2 p1 = points[v1];

			face.center = Vec2{
				0.5 * (p0.z + p1.z),
				0.5 * (p0.r + p1.r)
			};

			double dz = p1.z - p0.z;
			double dr = p1.r - p0.r;

			face.length2D = std::sqrt(dz * dz + dr * dr);
			face.area = 2.0 * PI * face.center.r * face.length2D;

			Vec2 normal{ dr, -dz };

			double mag = std::sqrt(normal.z * normal.z + normal.r * normal.r);

			if (mag > 1e-30) {
				normal.z /= mag;
				normal.r /= mag;
			}

			Vec2 toFace{
				face.center.z - mesh.cells[ownerCellID].center.z,
				face.center.r - mesh.cells[ownerCellID].center.r
			};

			double dot = normal.z * toFace.z + normal.r * toFace.r;

			if (dot < 0.0) {
				normal.z *= -1.0;
				normal.r *= -1.0;
			}

			face.normal = normal;

			auto groupIt = boundaryLookup.find(key);
			if (groupIt != boundaryLookup.end()) {
				face.boundaryGroupID = groupIt->second;
			}

			int faceID = (int)(mesh.faces.size());
			mesh.faces.push_back(face);

			edgeToFace[key] = faceID;
			mesh.cells[ownerCellID].faceIDs.push_back(faceID);
		}
		else {
			int faceID = it->second;

			mesh.faces[faceID].neighbor = ownerCellID;
			mesh.faces[faceID].boundaryGroupID = -1;

			mesh.cells[ownerCellID].faceIDs.push_back(faceID);
		}
		};

	// -------------------------
	// 2. Create faces
	// -------------------------
	for (int c = 0; c < (int)(triangles.size()); c++) {
		const Triangle& tri = triangles[c];

		addTriangleEdge(c, tri.v0, tri.v1);
		addTriangleEdge(c, tri.v1, tri.v2);
		addTriangleEdge(c, tri.v2, tri.v0);
	}

	return mesh;
}

std::vector<FVFace> createStructuredFVFaces(
	int nr,
	int nz,
	const std::vector<uint8_t>& activeCell,
	const std::vector<double>& rFace,
	const std::vector<double>& zFace,
	const std::vector<double>& r,
	const std::vector<double>& z,
	const std::vector<BoundarySegmentGroup>& boundaryGroups
) {
	std::vector<FVFace> faces;
	auto boundaryLookup = createBoundaryEdgeLookup(boundaryGroups);

	for (int i = 0; i < nr; i++) {
		for (int jFace = 0; jFace < nz + 1; jFace++) {

			int jLeft = jFace - 1;
			int jRight = jFace;

			bool leftFluid = isFluidCell(i, jLeft, nr, nz, activeCell);
			bool rightFluid = isFluidCell(i, jRight, nr, nz, activeCell);

			if (!leftFluid && !rightFluid) {
				continue; // skip faces between two solid cells
			}

			FVFace face;

			face.center = Vec2(zFace[jFace], r[i]);
			
			double r0 = rFace[i];
			double r1 = rFace[i + 1];

			face.area = PI * (r1 * r1 - r0 * r0);

			MeshEdge edge = makeAxialEdge(i, jFace);

			if (leftFluid && rightFluid) {			// interior fluid
				face.owner = cellID(i, jLeft, nz);
				face.neighbor = cellID(i, jRight, nz);
				face.normal = Vec2(1.0, 0.0); // normal points from left to right

				face.boundaryGroupID = -1; // not a boundary face

			}
			else if (leftFluid && !rightFluid) {
				face.owner = cellID(i, jLeft, nz);	// boundary on right side
				face.neighbor = -1; // boundary face
				face.normal = Vec2(1.0, 0.0); // normal points outward from fluid cell

				face.boundaryGroupID = getBoundaryGroupID(boundaryLookup, edge);

			}
			else if (!leftFluid && rightFluid) { // boundary on left side
				face.owner = cellID(i, jRight, nz);
				face.neighbor = -1; // boundary face
				face.normal = Vec2(-1.0, 0.0); // normal points outward from fluid cell

				face.boundaryGroupID = getBoundaryGroupID(boundaryLookup, edge);

			}

			faces.push_back(face);
		}
	}

	for (int iFace = 0; iFace <= nr; iFace++) {
		for (int j = 0; j < nz; j++) {
			int iLower = iFace - 1;
			int iUpper = iFace;

			bool lowerFluid = isFluidCell(iLower, j, nr, nz, activeCell);
			bool upperFluid = isFluidCell(iUpper, j, nr, nz, activeCell);

			if (!lowerFluid && !upperFluid) {
				continue; // skip faces between two solid cells
			}

			FVFace face;

			face.center = Vec2(z[j], rFace[iFace]);
			face.area = 2.0 * PI * rFace[iFace] * (zFace[j + 1] - zFace[j]);

			MeshEdge edge = makeRadialEdge(iFace, j);

			if (lowerFluid && upperFluid) {
				face.owner = cellID(iLower, j, nz);
				face.neighbor = cellID(iUpper, j, nz);
				face.normal = Vec2(0.0, 1.0);
				face.boundaryGroupID = -1;

			}
			else if (lowerFluid && !upperFluid) {
				face.owner = cellID(iLower, j, nz);
				face.neighbor = -1;
				face.normal = Vec2(0.0, 1.0);

				face.boundaryGroupID = getBoundaryGroupID(boundaryLookup, edge);
			}
			else if (!lowerFluid && upperFluid) {
				face.owner = cellID(iUpper, j, nz);
				face.neighbor = -1;
				face.normal = Vec2(0.0, -1.0);

				face.boundaryGroupID = getBoundaryGroupID(boundaryLookup, edge);
			}

			faces.push_back(face);
		}
	}
	return faces;
}

std::vector<FVCell> createStructuredFVCells(
	int nr,
	int nz,
	const std::vector<uint8_t>& activeCell,
	const std::vector<double>& rFace,
	const std::vector<double>& zFace,
	const std::vector<double>& r,
	const std::vector<double>& z,
	const std::vector<FVFace>& faces) {

	std::vector<FVCell> cells;
	cells.resize(nr * nz);

	for (int i = 0; i < nr; i++) {
		for (int j = 0; j < nz; j++) {

			int n = cellID(i, j, nz);

			FVCell& cell = cells[n];


			cell.center = Vec2(z[j], r[i]);

			double r0 = rFace[i];
			double r1 = rFace[i + 1];
			double dz = zFace[j + 1] - zFace[j];

			cell.volume = PI * (r1 * r1 - r0 * r0) * dz;

			cell.active = activeCell[n] != 0;
			cell.solid = activeCell[n] == 0;

			cell.faceIDs.clear();
		}
	}

	// iterate through each face, which has an owner and neighbor cell indices
	// if the owner or neighbor is a valid cell index, push the face index to the corresponding cell's faceIDs vector
	for (int f = 0; f < (int)faces.size(); f++) {

		const FVFace& face = faces[f];

		if (face.owner >= 0) {
			cells[face.owner].faceIDs.push_back(f);	// push back face indices
		}

		if (face.neighbor >= 0) {
			cells[face.neighbor].faceIDs.push_back(f); // push back face indices
		}
	}

	return cells;
}


FVMesh Mesh::createStructuredMesh(const std::vector<uint8_t>& activeCell) const {

	FVMesh fvMesh;

	std::vector<FVFace> faces = createStructuredFVFaces (
		g.nr,
		g.nz,
		activeCell,
		g.rFace,
		g.zFace,
		g.r,
		g.z,
		boundaryGroups
	);

	std::vector<FVCell> cells = createStructuredFVCells(
		g.nr,
		g.nz,
		activeCell,
		g.rFace,
		g.zFace,
		g.r,
		g.z,
		faces
	);	

	fvMesh.nr = g.nr;
	fvMesh.nz = g.nz;
	fvMesh.faces = std::move(faces);
	fvMesh.cells = std::move(cells);


	return fvMesh;
}

void Mesh::updateAfterLoadingFile() {

	isReady = true;
	//console->addLine("Successfully loaded mesh");	// console does not exist at this point (i think), so uncommenting will crash

}

BoundarySegment* Mesh::getBoundarySegmentByID(int id) {

	for (BoundarySegment& seg : boundarySegments) {

		if (seg.id == id) {
			return &seg;
		}
	}

	return nullptr;
}

int Mesh::getAvailableLoopID() {
	return nextLoopID++;
}

int Mesh::getAvailableBoundaryGroupID() const {
	int id = 0;
	for (const auto& g : boundaryGroups)
		id = std::max(id, g.id + 1);
	return id;
}


void Mesh::highlightSegmentsInGroup(const BoundarySegmentGroup& group) {
	highlightedBoundarySegmentIDs.clear();

	for (int segmentID : group.segmentIDs) {
		highlightedBoundarySegmentIDs.insert(segmentID);
	}
}

std::optional<BoundarySegmentGroup> Mesh::createBoundaryGroupFromSelection() {
	if (selectedBoundaryIDs.empty()) {
		return {};
	}

	BoundarySegmentGroup group{};

	group.id = getAvailableBoundaryGroupID();
	group.name = "Boundary " + std::to_string(group.id);

	group.segmentIDs.assign(
		selectedBoundaryIDs.begin(),
		selectedBoundaryIDs.end()
	);

	std::snprintf(
		group.nameBuffer,
		sizeof(group.nameBuffer),
		"%s",
		group.name.c_str()
	);

	for (int segmentID : group.segmentIDs) {
		BoundarySegment* seg = getBoundarySegmentByID(segmentID);

		if (!seg) {
			continue;
		}

		seg->groupID = group.id;

		for (int edgeID : seg->edgeIDs) {
			if (edgeID >= 0 &&
				edgeID < static_cast<int>(boundaryEdges.size())) {
				boundaryEdges[edgeID].groupID = group.id;
			}
		}
	}

	return group;
}

void Mesh::createGrid() {

	g.dz.clear();
	g.dr.clear();
	g.r.clear();
	g.z.clear();
	g.zFace.clear();
	g.rFace.clear();

	int nr = g.nr;
	int nz = g.nz;

	std::vector<double>& dz = g.dz;
	std::vector<double>& dr = g.dr;

	std::vector<double>& r = g.r;
	std::vector<double>& z = g.z;

	std::vector<double>& rFace = g.rFace;
	std::vector<double>& zFace = g.zFace;

	rFace = linspace(0.0, g.R, nr + 1, g.rBias);
	zFace = linspace(0.0, g.L, nz + 1, g.zBias);

	// radial location
	for (int i = 0; i < nr; i++) {
		double idr = rFace[i + 1] - rFace[i];
		dr.push_back(idr);
		r.push_back(0.5 * (rFace[i + 1] + rFace[i]));
	}

	for (int j = 0; j < nz; j++) {
		double jdz = zFace[j + 1] - zFace[j];
		dz.push_back(jdz);
		z.push_back(0.5 * (zFace[j + 1] + zFace[j]));
	}
}

FVMesh Mesh::createFVMesh(const std::vector<uint8_t>& activeCell) const {
	if (meshType == MeshType::Structured) {
		return createStructuredMesh(activeCell);
	}

	return createUnstructuredMesh(
		unstructuredPoints,
		unstructuredTriangles,
		boundaryVertices,
		boundaryEdges
	);
}

void Mesh::createGridVertices() {

	gridVertices.clear();

	int nr = g.nr;
	int nz = g.nz;

	const std::vector<double>& rFace = g.rFace;
	const std::vector<double>& zFace = g.zFace;

	for (int i = 0; i < nr; i++) {
		for (int j = 0; j < nz; j++) {

			float x0 = static_cast<float>(2.0 * zFace[j] / g.L - 1.0);
			float x1 = static_cast<float>(2.0 * zFace[j + 1] / g.L - 1.0);

			float y0 = static_cast<float>(2.0 * rFace[i] / g.R - 1.0);
			float y1 = static_cast<float>(2.0 * rFace[i + 1] / g.R - 1.0);

			// Triangle 1
			gridVertices.push_back(x0); gridVertices.push_back(y0);
			gridVertices.push_back(x1); gridVertices.push_back(y0);
			gridVertices.push_back(x1); gridVertices.push_back(y1);

			// Triangle 2
			gridVertices.push_back(x0); gridVertices.push_back(y0);
			gridVertices.push_back(x1); gridVertices.push_back(y1);
			gridVertices.push_back(x0); gridVertices.push_back(y1);
		}
	}
}

void Mesh::createUnstructuredVertices(
	const std::vector<Vec2>& points,
	const std::vector<Triangle>& triangles
) {
	vertices.clear();
	indices.clear();

	for (const Vec2& p : points) {
		glm::vec3 coord{
			displayZ(p.z),
			displayR(p.r),
			0.0f
		};

		vertices.push_back({ coord });
	}

	for (const Triangle& tri : triangles) {
		indices.push_back(tri.v0);
		indices.push_back(tri.v1);
		indices.push_back(tri.v2);
	}
}

void Mesh::createUnstructuredLineVertices(
	const std::vector<Vec2>& points,
	const FVMesh& mesh
) {
	gridLineVertices.clear();

	for (const FVFace& face : mesh.faces) {
		if (face.v0 < 0 || face.v1 < 0) {
			continue;
		}

		const Vec2& p0 = points[face.v0];
		const Vec2& p1 = points[face.v1];

		gridLineVertices.push_back(displayZ(p0.z));
		gridLineVertices.push_back(displayR(p0.r));

		gridLineVertices.push_back(displayZ(p1.z));
		gridLineVertices.push_back(displayR(p1.r));
	}
}

void Mesh::createGridLineVertices() {
	gridLineVertices.clear();

	const std::vector<double>& rFace = g.rFace;
	const std::vector<double>& zFace = g.zFace;

	int nr = g.nr;
	int nz = g.nz;

	// Vertical grid lines, constant z
	for (int j = 0; j <= nz; j++) {

		float x = (float)(2.0 * zFace[j] / g.L - 1.0f);

		float y0 = -1.0f;
		float y1 = 1.0f;

		gridLineVertices.push_back(x); gridLineVertices.push_back(y0);
		gridLineVertices.push_back(x); gridLineVertices.push_back(y1);
	}

	// Horizontal grid lines, constant r
	for (int i = 0; i <= nr; i++) {

		float y = (float)(2.0 * rFace[i] / g.R - 1.0f);

		float x0 = -1.0f;
		float x1 = 1.0f;

		gridLineVertices.push_back(x0); gridLineVertices.push_back(y);
		gridLineVertices.push_back(x1); gridLineVertices.push_back(y);
	}
}

void Mesh::createCylinderVertices() {

	vertices.clear();
	indices.clear();

	int nr = g.nr;
	int nz = g.nz;

	const std::vector<double>& rFace = g.rFace;
	const std::vector<double>& zFace = g.zFace;

	vertices.reserve((nr + 1) * (nz + 1));
	indices.reserve(nr * nz * 6);

	// get all vertices and colors for 2D concentration field
	glm::vec3 coord;
	for (int i = 0; i < nr + 1; i++) {
		for (int j = 0; j < nz + 1; j++) {

			float x = (float)zFace[j];
			float y = (float)rFace[i];

			coord = { x, y, 0.0f };
			vertices.push_back({ coord });
		}
	}

	// get all indices for 2D concentration field
	for (int i = 0; i < nr; i++) {
		for (int j = 0; j < nz; j++) {

			int botLeft = i * (nz + 1) + j;
			int botRight = botLeft + 1;
			int topLeft = (i + 1) * (nz + 1) + j;
			int topRight = topLeft + 1;

			indices.push_back(topLeft);
			indices.push_back(botLeft);
			indices.push_back(botRight);

			indices.push_back(botRight);
			indices.push_back(topRight);
			indices.push_back(topLeft);

		}
	}
}
