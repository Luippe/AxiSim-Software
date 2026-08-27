#pragma once

#include "aximesh/aximesh.h"

#include <vector>

namespace AxiMesh::Quality {

	// Both measures are normalized so an equilateral triangle scores exactly 1, and
	// both are scale invariant -- they read the same on normalized or world points.

	// Mean ratio, 4*sqrt(3)*A / (l0^2 + l1^2 + l2^2). 1 = equilateral, 0 = degenerate.
	// Signed through orient, so an inverted triangle comes back negative -- that is
	// the inversion test Smoothing relies on, do not take the absolute value.
	double triangleQuality(const Point& a, const Point& b, const Point& c);

	// Radius ratio R/(2r), written out as l0*l1*l2*(l0+l1+l2) / (4*orient^2).
	// 1 = equilateral, unbounded above, infinity for a zero-area triangle. Unlike
	// lmax/lmin it sees a cap: edges 1,1,1.999 score ~500 here and only 2 there.
	// (Verdict 4.10 "radius ratio", after Pebay & Baker 2003 -- what that manual
	// calls "aspect ratio" is lmax/(2*sqrt(3)*r), a different number.)
	double triangleAspectRatio(const Point& a, const Point& b, const Point& c);

	// Fills mesh.elementQuality and mesh.aspectRatio, one entry per triangle.
	void buildQuality(Mesh& mesh);

}
