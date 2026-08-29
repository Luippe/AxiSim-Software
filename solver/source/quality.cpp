#include "quality.h"

#include <limits>
#include <numbers>

#include "aximesh/quality.h"
#include "boundary_struct.h"

namespace {

	// Vec2 is (z, r) and AxiMesh::Point is (x, y). The mapping is arbitrary but must
	// be consistent: triangleQuality is signed through the winding.
	AxiMesh::Point toAxiPoint(const Vec2& v) {
		return AxiMesh::Point{ v.z, v.r };
	}

}

// Measured from the cell's corner ring (points + cellCornerStart/cellCornerIDs)
// rather than its faces, because the corners are the only cell-shape data BOTH mesh
// paths carry: createMultiBlockFVMesh builds faces from toPackedMesh, which has no
// vertex IDs and leaves FVFace::v0/v1 at -1.
void Quality::buildQuality(const FVMesh& fvMesh) {

	const int nCells = fvMesh.numCells();
	const double unmeasured = std::numeric_limits<double>::quiet_NaN();

	aspectRatios.assign(nCells, unmeasured);
	elementQuality.assign(nCells, unmeasured);
	planeAngles.assign(3 * (size_t)nCells, unmeasured);

	const std::vector<int>& cornerStart = fvMesh.cellCornerStart;
	const std::vector<int>& cornerIDs = fvMesh.cellCornerIDs;
	const std::vector<Vec2>& points = fvMesh.points;

	if ((int)cornerStart.size() < nCells + 1) {
		return;		// no outline store: nothing to measure
	}

	const int nPoints = (int)points.size();

	for (int c = 0; c < nCells; c++) {
		const int begin = cornerStart[c];

		// triangle formulas, so a quad or an empty outline stays unmeasured
		if (cornerStart[c + 1] - begin != 3) {
			continue;
		}

		const int i0 = cornerIDs[begin];
		const int i1 = cornerIDs[begin + 1];
		const int i2 = cornerIDs[begin + 2];

		if (i0 < 0 || i1 < 0 || i2 < 0 ||
			i0 >= nPoints || i1 >= nPoints || i2 >= nPoints) {
			continue;
		}

		const AxiMesh::Point a = toAxiPoint(points[i0]);
		const AxiMesh::Point b = toAxiPoint(points[i1]);
		const AxiMesh::Point d = toAxiPoint(points[i2]);

		// a degenerate triangle comes back infinite, which reads as unmeasurable
		// downstream the same way the NaN does
		aspectRatios[c] = AxiMesh::Quality::triangleAspectRatio(a, b, d);
		elementQuality[c] = AxiMesh::Quality::triangleQuality(a, b, d);

		// fills [3c, 3c+2] in radians; the histogram band is degrees
		AxiMesh::Quality::trianglePlaneAngle(planeAngles, a, b, d, c);

		for (int k = 0; k < 3; k++) {
			planeAngles[3 * (size_t)c + k] *= 180.0 / std::numbers::pi;
		}
	}
}

void Quality::reset() {
	// both together: the inspector overlays trust a metric whose length matches the
	// cell count, so a half-cleared Quality is a set of stale numbers waiting to be
	// painted onto whatever mesh happens to have the same cell count next.
	aspectRatios.clear();
	elementQuality.clear();
	planeAngles.clear();
}
