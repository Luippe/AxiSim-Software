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

	// The FVMesh carries its own cell corners (points + cellCornerStart/cellCornerIDs),
	// so the shape metrics need nothing passed alongside it.
	void buildQuality(const FVMesh& fvMesh);
	void reset();

private:

};
