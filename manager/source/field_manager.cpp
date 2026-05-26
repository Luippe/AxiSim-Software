#include "pch.h"
#include "field_manager.h"
#include <algorithm>
#include "printer.h"

Field::Field(int nz, int nr) :
	nzBase(nz),
	nrBase(nr){
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

void Field::generate(SolutionField& solution, BoundaryConditionConfig& bc) {

	this->bc = bc;
	unProcessedData = solution.field;

	nr = solution.nr;
	nz = solution.nz;

	dr = solution.dr;
	dz = solution.dz;

	type = solution.type;

	rFace = buildFaces(dr);
	zFace = buildFaces(dz);

	rCell = buildCenters(rFace);
	zCell = buildCenters(zFace);

	switch (type) {
	case CellStoreType::CENTER:
		dataR = rCell;
		dataZ = zCell;
		break;

	case CellStoreType::AXIAL:
		dataR = rCell;
		dataZ = zFace;
		break;

	case CellStoreType::RADIAL:
		dataR = rFace;
		dataZ = zCell;
		break;
	}

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

	for (int i = 0; i < nrBase + 1; i++) {
		for (int j = 0; j < nzBase + 1; j++) {

			float x = static_cast<float>(zFace[j]);
			float r = static_cast<float>(rFace[i]);

			vertexValues.push_back(getData(glm::vec2(x, r)));
		}
	}
}

void Field::createCellValues() {

	cellValues.clear();

	for (int i = 0; i < nrBase; i++) {
		for (int j = 0; j < nzBase; j++) {

			float x = static_cast<float>(zCell[j]);
			float r = static_cast<float>(rCell[i]);

			cellValues.push_back(getData(glm::vec2(x, r)));
		}
	}
}

int findIndex(const std::vector<double>& coords, double x) {
	auto it = std::upper_bound(coords.begin(), coords.end(), x);

	int idx = static_cast<int>(it - coords.begin()) - 1;

	idx = std::clamp(idx, 0, static_cast<int>(coords.size()) - 2);

	return idx;
}

void Field::createBuffer() {
	textureBuffer.createBuffer(GL_R32F, nzBase + 1, nrBase + 1, GL_RED, GL_FLOAT, vertexValues.data());
}

double Field::sample(int i, int j) {

	// inlet
	if (j < 0) {
		if (type == CellStoreType::CENTER || type == CellStoreType::RADIAL) {
			if (bc.inlet.type == BCType::DIRICHLET) {
				return bc.inlet.val;
			}
		}
		else if (type == CellStoreType::AXIAL) {
			j = 0;
		}
	}

	// outlet
	if (j >= nz) {
		if (type == CellStoreType::CENTER || type == CellStoreType::RADIAL) {
			if (bc.outlet.type == BCType::DIRICHLET) {
				return bc.outlet.val;
			}
			else if (bc.outlet.type == BCType::NEUMANN) {
				j = nz - 1;
			}
		}
		else if (type == CellStoreType::AXIAL) {
			j = nz - 1;
		}
	}

	// centerline
	if (i < 0) {
		if (type == CellStoreType::CENTER || type == CellStoreType::AXIAL) {
			if (bc.centerline.type == BCType::DIRICHLET) {
				return bc.centerline.val;
			}
			else if (bc.centerline.type == BCType::NEUMANN) {
				i = 0;
			}
		}
		else if (type == CellStoreType::RADIAL) {
			i = 0;
		}
	}

	// outer
	if (i >= nr) {
		if (type == CellStoreType::CENTER || type == CellStoreType::AXIAL) {
			if (bc.outer.type == BCType::DIRICHLET) {
				return bc.outer.val;
			}
			else if (bc.outer.type == BCType::NEUMANN) {
				i = nr - 1;
			}
		}
		else if (type == CellStoreType::RADIAL) {
			i = nr - 1;
		}
	}
	
	return unProcessedData[i * nz + j];
}

float Field::getData(const glm::vec2& pos) {

	double z = pos.x;
	double r = pos.y;

	int j1 = findIndex(dataZ, z);
	int i1 = findIndex(dataR, r);

	int j2 = j1 + 1;
	int i2 = i1 + 1;

	double z1 = dataZ[j1];
	double z2 = dataZ[j2];

	double r1 = dataR[i1];
	double r2 = dataR[i2];

	double f11 = sample(i1, j1);
	double f12 = sample(i1, j2);
	double f21 = sample(i2, j1);
	double f22 = sample(i2, j2);

	double tz = (z - z1) / (z2 - z1);
	double tr = (r - r1) / (r2 - r1);

	tz = std::clamp(tz, 0.0, 1.0);
	tr = std::clamp(tr, 0.0, 1.0);

	double value =
		(1.0 - tz) * (1.0 - tr) * f11 +
		tz * (1.0 - tr) * f12 +
		(1.0 - tz) * tr * f21 +
		tz * tr * f22;

	return (float)(value);
}

float Field::getData(const glm::vec3& pos) {

	double r = sqrt(pos.y * pos.y + pos.z * pos.z);
	double z = pos.x;

	return getData(glm::vec2(z, r));
}