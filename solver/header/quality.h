#pragma once
#include <vector>

struct FVMesh;

class Quality {
public:

	// Both per FV cell, both measured by AxiMesh::Quality's triangle formulas -- the
	// same two functions the mesher optimizes against, so the inspector and the
	// mesher cannot disagree about what a good cell is.
	//
	// A cell that is not a triangle has no value and is left NaN, which is how the
	// inspector overlays tell "unmeasurable" from "bad" and skip it. A structured
	// (multiblock) mesh is quads, so it comes back all-NaN.
	std::vector<double> aspectRatios;	// radius ratio R/(2r): 1 = equilateral, up = worse
	std::vector<double> elementQuality;	// mean ratio: 1 = equilateral, 0 = degenerate

	// THREE per cell, in degrees over [0, 180] -- the interior angle at each corner in
	// corner order, so cell c owns [3c, 3c+2]. Not one value per cell like the two
	// above, so it feeds the histogram only: an overlay would need a reduction first,
	// and the useful one is the MAXIMUM angle, which is exactly what the two ratios
	// above cannot see. Non-triangles are NaN across all three entries, so 3*cellID
	// stays a valid index for every cell.
	std::vector<double> planeAngles;

	// The FVMesh carries its own cell corners (points + cellCornerStart/cellCornerIDs),
	// so the shape metrics need nothing passed alongside it.
	void buildQuality(const FVMesh& fvMesh);
	void reset();

private:

};
