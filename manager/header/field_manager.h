#pragma once
#include "buffer_manager.h"
#include "solver_struct.h"
#include <glm/fwd.hpp>

class Field {
public:

	Field(Config& config);

	void generate(SolutionField& solution, BoundaryConditionConfig& bc);

	// get value at given position using bilinear interpolation
	float getData(glm::vec3& pos);

	std::vector<float> processedData;

	TextureBuffer textureBuffer;

	float vmin = 0.0f;
	float vmax = 0.0f;

private:

	// create texture buffer for field
	void createBuffer();

	// fill in field with values
	void createValues();

	// update vmin and vmax
	void updateMinMax();

	// sample value at given i and j
	double sample(int i, int j);

	BoundaryConditionConfig bc;
	Config& config;
	CellStoreType type;
	std::vector<double> unProcessedData;
	int nr, nz;
	double dr, dz;
	double xOffset, yOffset;	// offset of the field from (0,0)
	
};