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
		std::string candidateName = "Boundary " + std::to_string(id);

		bool nameExists = std::any_of(
			boundaryGroups.begin(),
			boundaryGroups.end(),
			[&](const BoundarySegmentGroup& group) {
				return group.name == candidateName;
			}
		);

		if (!nameExists) {
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
