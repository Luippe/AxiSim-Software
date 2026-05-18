#pragma once
#include "buffer_manager.h"
#include "solver_struct.h"
#include <glm/fwd.hpp>

class Field {
public:

	std::vector<float> vertexValues;	// vetex centered values
	std::vector<float> cellValues;		// cell centered values

	TextureBuffer textureBuffer;

	float vmin = 0.0f;
	float vmax = 0.0f;
	int nr, nz;
	int nzBase = 0;
	int nrBase = 0;

	Field(int nz, int nr);

	void setMinMax(float vmin, float vmax);

	void generate(SolutionField& solution, BoundaryConditionConfig& bc);

	// get value at given position using bilinear interpolation
	float getData(const glm::vec2& pos);
	float getData(const glm::vec3& pos);

	// sample value at given i and j
	double sample(int i, int j);

private:

	// create texture buffer for field
	void createBuffer();

	// create values at cell vertices
	void createVertexValues();

	// create cell centered values
	void createCellValues();

	// update vmin and vmax
	void updateMinMax();

	BoundaryConditionConfig bc;
	CellStoreType type;
	std::vector<double> unProcessedData;

	double dr, dz;
	double xOffset, yOffset;	// offset of the field from (0,0)
	
};