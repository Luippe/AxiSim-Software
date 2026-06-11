#pragma once
#include "buffer_manager.h"
#include "solver_struct.h"
#include "boundary_struct.h"
#include <glm/fwd.hpp>

class Field {
public:

	std::vector<float> vertexValues;	// vetex centered values
	std::vector<float> cellValues;		// cell centered values

	TextureBuffer textureBuffer;

	float vmin = 0.0f;
	float vmax = 0.0f;
	int nr = 0;
	int nz = 0;

	Field();

	void setMinMax(float vmin, float vmax);

	void generate(
		const SolutionField& solution,
		const FVMesh& fvMesh,
		const std::vector<BoundaryGroup>& boundaryGroups,
		const std::vector<BoundaryGroupBC>& boundaryGroupBCs
	);

	// get value at given position using bilinear interpolation
	float getData(const glm::vec2& pos) const;
	float getData(const glm::vec3& pos) const;

private:

	const FVMesh* fvMesh;
	const std::vector<BoundaryGroup>* boundaryGroups;
	const std::vector<BoundaryGroupBC>* boundaryGroupBCs;
	BoundaryVariable boundaryVariable = BoundaryVariable::None;

	std::vector<double> extendedZ;
	std::vector<double> extendedR;
	std::vector<double> extendedData;

	int extNr = 0;
	int extNz = 0;

	double sampleBoundary(
		int i,
		int j,
		const Vec2& targetNormal
	) const;

	void buildExtendedData();

	void buildExtendedCoordinates();

	// create texture buffer for field
	void createBuffer();

	// create values at cell vertices
	void createVertexValues();

	// create cell centered values
	void createCellValues();

	// update vmin and vmax
	void updateMinMax();

	CellStoreType type;
	std::vector<double> unProcessedData;

	std::vector<double> dr; // cell widths
	std::vector<double> dz;

	std::vector<double> rFace;
	std::vector<double> zFace;

	std::vector<double> rCell;
	std::vector<double> zCell;

	std::vector<double> dataR;
	std::vector<double> dataZ;
	double xOffset, yOffset;	// offset of the field from (0,0)
	
};