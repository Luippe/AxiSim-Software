#include "mesh.h"
#include "shader.h"
#include "console.h"

#include "time_manager.h"
#include "solver_struct.h"
#include "printer.h"
#include <glm/trigonometric.hpp>
#include <algorithm>
#include "math_func.h"



Mesh::Mesh(Config& config) : g(config.g) {
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
	ntheta = 360.0f / (float)nseg;

	createGrid();
	createGridVertices();
	createGridLineVertices();
	createCylinderVertices();

	console->addCompletionMessage("Completed generating buffers");

	isReady = true;

	float endTime = endTimer(startTime);
	console->addCompletionTime("Mesh", endTime);
}


std::vector<FVFace> createFVFaces(
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

std::vector<FVCell> createFVCells(
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

FVMesh Mesh::createStructuredMesh(const std::vector<uint8_t>& activeCell) {

	FVMesh mesh;

	std::vector<FVFace> faces = createFVFaces (
		g.nr,
		g.nz,
		activeCell,
		g.rFace,
		g.zFace,
		g.r,
		g.z,
		boundaryGroups
	);

	std::vector<FVCell> cells = createFVCells(
		g.nr,
		g.nz,
		activeCell,
		g.rFace,
		g.zFace,
		g.r,
		g.z,
		faces
	);	

	mesh.nr = g.nr;
	mesh.nz = g.nz;
	mesh.faces = std::move(faces);
	mesh.cells = std::move(cells);

	return mesh;
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

BoundarySegmentGroup* Mesh::getBoundaryGroupByID(int id) {
	for (BoundarySegmentGroup& group : boundaryGroups) {
		if (group.id == id) {
			return &group;
		}
	}
	return nullptr;
}

BoundarySegmentGroup* Mesh::getBoundaryGroupByName(const std::string& name) {
	for (BoundarySegmentGroup& group : boundaryGroups) {
		if (group.name == name) {
			return &group;
		}
	}
	return nullptr;
}

int Mesh::getAvailableBoundaryGroupID() const {

	int id = nextGroupID;

	while (true) {

		bool idExists = std::any_of(
            boundaryGroups.begin(),
            boundaryGroups.end(),
            [&](const BoundarySegmentGroup& group) {
                return group.id == id;
            }
        );

		if (!idExists) {
			return id;
		}

		id++;
	}
}


std::vector<MeshEdge> Mesh::edgesFromBoundarySegment(
	const BoundarySegment& seg
) const {
	std::vector<MeshEdge> edges;

	if (seg.a.i == seg.b.i) {
		int i = seg.a.i;

		int j0 = std::min(seg.a.j, seg.b.j);
		int j1 = std::max(seg.a.j, seg.b.j);

		for (int j = j0; j < j1; j++) {
			edges.push_back({ EdgeOrient::Horizontal, i, j });
		}
	}
	else if (seg.a.j == seg.b.j) {
		int j = seg.a.j;

		int i0 = std::min(seg.a.i, seg.b.i);
		int i1 = std::max(seg.a.i, seg.b.i);

		for (int i = i0; i < i1; i++) {
			edges.push_back({ EdgeOrient::Vertical, i, j });
		}
	}

	return edges;
}
void Mesh::highlightSegmentsInGroup(const BoundarySegmentGroup& group) {
	highlightedBoundarySegmentIDs.clear();

	std::unordered_set<MeshEdge, MeshEdgeHash> groupEdges(
		group.edges.begin(),
		group.edges.end()
	);

	for (const BoundarySegment& seg : boundarySegments) {
		std::vector<MeshEdge> segmentEdges = edgesFromBoundarySegment(seg);

		for (const MeshEdge& edge : segmentEdges) {
			if (groupEdges.find(edge) != groupEdges.end()) {
				highlightedBoundarySegmentIDs.insert(seg.id);
				break;
			}
		}
	}
}

std::optional<BoundarySegmentGroup> Mesh::createBoundaryGroupFromSelection() {
	if (selectedBoundaryIDs.empty()) {
		return {};
	}

	BoundarySegmentGroup group;

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
