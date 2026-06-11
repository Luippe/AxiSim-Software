#include "pch.h"
#include "field_manager.h"
#include <algorithm>

#include "printer.h"
#include "boundary_func.h"

using namespace BoundaryDefaults;
using namespace BoundaryGet;

Field::Field() {
}

inline int extID(int iExt, int jExt, int extNz) {
	return iExt * extNz + jExt;
}

std::vector<double> buildFaces(const std::vector<double>& widths) {
	std::vector<double> faces(widths.size() + 1, 0.0);

	for (int i = 0; i < widths.size(); i++) {
		faces[i + 1] = faces[i] + widths[i];
	}

	return faces;
}

std::vector<double> buildCenters(const std::vector<double>& faces) {
	std::vector<double> centers(faces.size() - 1);

	for (int i = 0; i < centers.size(); i++) {
		centers[i] = 0.5 * (faces[i] + faces[i + 1]);
	}

	return centers;
}

void Field::generate(
	const SolutionField& solution,
	const FVMesh& fvMesh,
	const std::vector<BoundarySegmentGroup>& boundaryGroups
) {

	this->fvMesh = &fvMesh;
	this->boundaryGroups = &boundaryGroups;

	unProcessedData = solution.field;

	boundaryVariable = solution.boundaryVariable;

	nr = fvMesh.nr;
	nz = fvMesh.nz;

	dr = solution.dr;
	dz = solution.dz;

	rFace = buildFaces(dr);
	zFace = buildFaces(dz);

	rCell = buildCenters(rFace);
	zCell = buildCenters(zFace);

	dataR = rCell;
	dataZ = zCell;
	
	buildExtendedCoordinates();
	buildExtendedData();

	createVertexValues();
	createCellValues();
	createBuffer();
	updateMinMax();

}

void Field::updateMinMax() {

	vmin = *std::min_element(vertexValues.begin(), vertexValues.end());
	vmax = *std::max_element(vertexValues.begin(), vertexValues.end());

}

void Field::setMinMax(float vmin, float vmax) {
	this->vmin = vmin;
	this->vmax = vmax;
}

void Field::createVertexValues() {

	vertexValues.clear();

	for (int i = 0; i < nr + 1; i++) {
		for (int j = 0; j < nz + 1; j++) {

			float x = (float)(zFace[j]);
			float r = (float)(rFace[i]);

			vertexValues.push_back(getData(glm::vec2(x, r)));
		}
	}
}

void Field::createCellValues() {

	cellValues.clear();

	for (int i = 0; i < nr; i++) {
		for (int j = 0; j < nz; j++) {

			float x = (float)(zCell[j]);
			float r = (float)(rCell[i]);

			cellValues.push_back(getData(glm::vec2(x, r)));
		}
	}
}

int findIndex(const std::vector<double>& coords, double x) {
	auto it = std::upper_bound(coords.begin(), coords.end(), x);

	int idx = (int)(it - coords.begin()) - 1;

	idx = std::clamp(idx, 0, (int)(coords.size()) - 2);

	return idx;
}

void Field::createBuffer() {
	textureBuffer.createBuffer(GL_R32F, nz + 1, nr + 1, GL_RED, GL_FLOAT, vertexValues.data());
}

int findLowerIndex(const std::vector<double>& x, double value) {
	if (x.size() < 2) return 0;

	if (value <= x.front()) {
		return 0;
	}

	if (value >= x.back()) {
		return (int)(x.size()) - 2;
	}

	auto it = std::upper_bound(x.begin(), x.end(), value);

	int upper = (int)(it - x.begin());
	int lower = upper - 1;

	return std::clamp(lower, 0, (int)(x.size()) - 2);
}

void Field::buildExtendedCoordinates() {

	extendedZ.clear();
	extendedR.clear();

	extendedZ.reserve(nz + 2);
	extendedR.reserve(nr + 2);

	// z direction: inlet face, cell centers, outlet face
	extendedZ.push_back(zFace.front());

	for (double zc : dataZ) {
		extendedZ.push_back(zc);
	}

	extendedZ.push_back(zFace.back());

	// r direction: centerline face, cell centers, outer face
	extendedR.push_back(rFace.front());

	for (double rc : dataR) {
		extendedR.push_back(rc);
	}

	extendedR.push_back(rFace.back());

	extNz = (int)(extendedZ.size());
	extNr = (int)(extendedR.size());
}

double Field::sampleBoundary(
	int i,
	int j,
	const Vec2& targetNormal
) const {

	if (!fvMesh || !boundaryGroups) {
		return unProcessedData[i * nz + j];
	}

	int c = i * nz + j;

	const FVCell& cell = fvMesh->cells[c];

	for (int faceID : cell.faceIDs) {
		const FVFace& face = fvMesh->faces[faceID];

		if (!face.isBoundary()) {
			continue;
		}

		Vec2 normal = face.normal;

		if (face.neighbor == c) {
			normal.z = -normal.z;
			normal.r = -normal.r;
		}

		double dot =
			normal.z * targetNormal.z +
			normal.r * targetNormal.r;

		if (dot < 0.9) {
			continue;
		}

		const BoundarySegmentGroup* group = getBoundaryGroupByID(*boundaryGroups, face.boundaryGroupID);

		if (!group) {
			return unProcessedData[c];
		}

		auto it = group->bcs.find(boundaryVariable);

		if (it == group->bcs.end()) {
			return unProcessedData[c];
		}

		const BoundaryCondition& bc = it->second;

		// if the variable is not in the type of boundary, then get the nearest value instead
		if (!isVariableInBoundaryType(boundaryVariable, group->type)) {
			return unProcessedData[c];
		}

		if (bc.type == DIRICHLET) {
			return bc.value;
		}

		if (bc.type == NEUMANN || bc.type == FULLY_DEVELOPED) {
			double phiP = unProcessedData[c];

			double dz = face.center.z - cell.center.z;
			double dr = face.center.r - cell.center.r;

			double d = std::abs(dz * normal.z + dr * normal.r);

			return phiP + bc.value * d;
		}

		return unProcessedData[c];
	}

	return unProcessedData[c];
}

void Field::buildExtendedData() {

	extNr = nr + 2;
	extNz = nz + 2;

	extendedData.assign(extNr * extNz, 0.0);

	// Interior real cells
	for (int i = 0; i < nr; i++) {
		for (int j = 0; j < nz; j++) {

			int iExt = i + 1;
			int jExt = j + 1;

			extendedData[extID(iExt, jExt, extNz)] =
				unProcessedData[i * nz + j];
		}
	}

	// Fill boundary layer
	for (int i = 0; i < nr; i++) {
		int iExt = i + 1;

		// inlet / west
		extendedData[extID(iExt, 0, extNz)] =
			sampleBoundary(i, 0, Vec2(-1.0, 0.0));

		// outlet / east
		extendedData[extID(iExt, nz + 1, extNz)] =
			sampleBoundary(i, nz - 1, Vec2(1.0, 0.0));
	}

	for (int j = 0; j < nz; j++) {
		int jExt = j + 1;

		// centerline / south
		extendedData[extID(0, jExt, extNz)] =
			sampleBoundary(0, j, Vec2(0.0, -1.0));

		// outer / north
		extendedData[extID(nr + 1, jExt, extNz)] =
			sampleBoundary(nr - 1, j, Vec2(0.0, 1.0));
	}

	// Corners: average neighboring boundary values
	extendedData[extID(0, 0, extNz)] =
		0.5 * (
			extendedData[extID(1, 0, extNz)] +
			extendedData[extID(0, 1, extNz)]
			);

	extendedData[extID(0, nz + 1, extNz)] =
		0.5 * (
			extendedData[extID(1, nz + 1, extNz)] +
			extendedData[extID(0, nz, extNz)]
			);

	extendedData[extID(nr + 1, 0, extNz)] =
		0.5 * (
			extendedData[extID(nr, 0, extNz)] +
			extendedData[extID(nr + 1, 1, extNz)]
			);

	extendedData[extID(nr + 1, nz + 1, extNz)] =
		0.5 * (
			extendedData[extID(nr, nz + 1, extNz)] +
			extendedData[extID(nr + 1, nz, extNz)]
			);
}

float Field::getData(const glm::vec2& pos) const {

	double z = pos.x;
	double r = pos.y;

	if (extendedZ.size() < 2 || extendedR.size() < 2) {
		
		return 0.0f;
	}

	int j1 = findLowerIndex(extendedZ, z);
	int i1 = findLowerIndex(extendedR, r);

	int j2 = j1 + 1;
	int i2 = i1 + 1;

	double z1 = extendedZ[j1];
	double z2 = extendedZ[j2];

	double r1 = extendedR[i1];
	double r2 = extendedR[i2];

	double f11 = extendedData[extID(i1, j1, extNz)];
	double f12 = extendedData[extID(i1, j2, extNz)];
	double f21 = extendedData[extID(i2, j1, extNz)];
	double f22 = extendedData[extID(i2, j2, extNz)];

	double dz = z2 - z1;
	double dr = r2 - r1;

	if (std::abs(dz) < 1.0e-30 || std::abs(dr) < 1.0e-30) {
		return (float)(f11);
	}

	double tz = std::clamp((z - z1) / dz, 0.0, 1.0);
	double tr = std::clamp((r - r1) / dr, 0.0, 1.0);

	double value =
		(1.0 - tz) * (1.0 - tr) * f11 +
		tz * (1.0 - tr) * f12 +
		(1.0 - tz) * tr * f21 +
		tz * tr * f22;

	return (float)(value);
}

float Field::getData(const glm::vec3& pos) const {

	double r = sqrt(pos.y * pos.y + pos.z * pos.z);
	double z = pos.x;

	return getData(glm::vec2(z, r));
}