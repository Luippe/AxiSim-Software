#include "pch.h"
#include "field_manager.h"
#include <algorithm>
#include "printer.h"

Field::Field(int nz, int nr) :
	nzBase(nz),
	nrBase(nr){
}

void Field::generate(SolutionField& solution, BoundaryConditionConfig& bc) {

	this->bc = bc;
	unProcessedData = solution.field;
	nr = solution.nr;
	nz = solution.nz;
	dr = solution.dr;
	dz = solution.dz;
	type = solution.type;

	switch (solution.type) {
	case CellStoreType::CENTER:
		xOffset = 0.5 * dz;
		yOffset = 0.5 * dr;
		break;
	case CellStoreType::AXIAL:
		xOffset = 0.0;
		yOffset = 0.5 * dr;
		break;
	case CellStoreType::RADIAL:
		xOffset = 0.5 * dz;
		yOffset = 0.0;
		break;
	}

	createValues();
	createBuffer();
	updateMinMax();
}

void Field::updateMinMax() {

	vmin = (double)*std::min_element(processedData.begin(), processedData.end());
	vmax = (double)*std::max_element(processedData.begin(), processedData.end());

}

void Field::createValues() {

	processedData.clear();

	for (int i = 0; i < nrBase + 1; i++) {
		for (int j = 0; j < nzBase + 1; j++) {

			float x = j * dz;
			float r = i * dr;

			glm::vec3 pos = { x, r, 0.0f };
			float val = getData(pos);

			processedData.push_back(val);
		}
	}
}

void Field::createBuffer() {
	textureBuffer.createBuffer(GL_R32F, nzBase + 1, nrBase + 1, GL_RED, GL_FLOAT, processedData.data());
}

double Field::sample(int i, int j) {

	// inlet
	if (j < 0) {
		if (type == CellStoreType::CENTER || type == CellStoreType::RADIAL) {
			if (!(bc.inlet.type == BCType::NEUMANN)) {
				return bc.inlet.val;
			}
			else {
				j = 0;
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

float Field::getData(glm::vec3& pos) {

	double f11, f12, f21, f22;

	double r = sqrt(pos.y * pos.y + pos.z * pos.z);
	double z = pos.x;

	int i1 = (r - yOffset) / dr;
	int j1 = (z - xOffset) / dz;
	int i2 = i1 + 1;
	int j2 = j1 + 1;

	double r1 = std::clamp(i1 * dr + yOffset, 0.0, nr * dr);
	double z1 = std::clamp(j1 * dz + xOffset, 0.0, nz * dz);
	double r2 = std::clamp(i2 * dr + yOffset, 0.0, nr * dr);
	double z2 = std::clamp(j2 * dz + xOffset, 0.0, nz * dz);
	
	int n11 = i1 * nz + j1;
	int n12 = i1 * nz + j2;
	int n21 = i2 * nz + j1;
	int n22 = i2 * nz + j2;

	f11 = sample(i1, j1);
	f12 = sample(i1, j2);
	f21 = sample(i2, j1);
	f22 = sample(i2, j2);

	float A = (float)(1.0 / ((z2 - z1) * (r2 - r1)));
	glm::vec2 B((z2 - z), (z - z1));
	glm::mat2 C(f11, f12, f21, f22);
	glm::vec2 D((r2 - r), (r - r1));

	return A * glm::dot(B, (C * D));
}