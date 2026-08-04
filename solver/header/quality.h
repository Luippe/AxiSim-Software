#pragma once
#include <vector>

struct FVMesh;

class Quality {
public:

	std::vector<double> aspectRatios;
	std::vector<double> nonOrthogonality;
	std::vector<double> skewness;

	void buildQuality(
		const FVMesh& fvMesh,
		const std::vector<double>& meshPoints,
		const std::vector<int>& cellCornerStart,
		const std::vector<int>& cellCornerIDs
	);
	void reset();

private:

};