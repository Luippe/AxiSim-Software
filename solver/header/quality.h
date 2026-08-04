#pragma once
#include <vector>

struct FVMesh;

class Quality {
public:

	std::vector<double> aspectRatios;
	std::vector<double> nonOrthogonality;
	std::vector<double> skewness;

	// The FVMesh carries its own cell corners (points + cellCornerStart/cellCornerIDs),
	// so the shape metrics need nothing passed alongside it.
	void buildQuality(const FVMesh& fvMesh);
	void reset();

private:

};