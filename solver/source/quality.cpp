#include "quality.h"

#include <algorithm>

#include "boundary_struct.h"

void calculateSkewness(
	const FVMesh& fvMesh,
	std::vector<double>& skewness
) {

	constexpr double radToDeg = 57.29577951308232;

	int nCells = fvMesh.numCells();

	skewness.assign(nCells, 0.0);
	const std::vector<int>& cellCornerStart = fvMesh.cellCornerStart;
	const std::vector<int>& cellCornerIDs = fvMesh.cellCornerIDs;
	const std::vector<Vec2>& points = fvMesh.points;

	for (int c = 0; c < nCells; c++) {
		double skew = 0.0;
		const int begin = cellCornerStart[c];

		const int n = cellCornerStart[c + 1] - begin; // number of vertices this cell

		if (n < 3) continue;          // no outline: stays at the sentinel

		double thetaMin = 180.0;
		double thetaMax = 0.0;

		for (int k = 0; k < n; ++k) {
			const Vec2& prev = points[cellCornerIDs[begin + (k + n - 1) % n]];
			const Vec2& here = points[cellCornerIDs[begin + k]];
			const Vec2& next = points[cellCornerIDs[begin + (k + 1) % n]];

			double az = prev.z - here.z, ar = prev.r - here.r;
			double bz = next.z - here.z, br = next.r - here.r;

			double aMag = std::sqrt(az * az + ar * ar);
			double bMag = std::sqrt(bz * bz + br * br);

			if (aMag < 1e-30 || bMag < 1e-30) continue;

			double cosT = std::clamp((az * bz + ar * br) / (aMag * bMag), -1.0, 1.0);
			double theta = std::acos(cosT) * radToDeg;

			thetaMin = std::min(thetaMin, theta);
			thetaMax = std::max(thetaMax, theta);
		}

		double thetaE = 180.0 - 360.0 / (double)n;

		skewness[c] = std::max(
			(thetaMax - thetaE) / (180 - thetaE),
			(thetaE - thetaMin) / thetaE
		);
	}
}

void calculateOrthogonality(
	const FVMesh& fvMesh,
	std::vector<double>& nonOrthgonality
) {
	int nCells = fvMesh.numCells();

	nonOrthgonality.assign(nCells, 0.0);

	for (int c = 0; c < nCells; c++) {
		const FVCell& cell = fvMesh.cells[c];

		double orth = 1.0;

		for (int faceID : cell.faceIDs) {
			const FVFace& face = fvMesh.faces[faceID];

			// centroid to face
			double fz = face.center.z - cell.center.z;
			double fr = face.center.r - cell.center.r;

			double fDot = std::abs(fz * face.normal.z + fr * face.normal.r);
			double fMag = std::sqrt(fz * fz + fr * fr);

			orth = std::min(orth, fDot / fMag);

			// centroid to centroid
			if (face.isBoundary()) continue;

			const FVCell& N = fvMesh.cells[face.neighbor];
			const FVCell& P = fvMesh.cells[face.owner];

			double dz = N.center.z - P.center.z;
			double dr = N.center.r - P.center.r;

			double dDot = std::abs(dz * face.normal.z + dr * face.normal.r);
			double dMag = std::sqrt(dz * dz + dr * dr);

			orth = std::min(orth, dDot / dMag);

		}

		nonOrthgonality[c] = orth;

	}
}

// Per-cell aspect ratio, written into aspectRatios (resized to the cell count).
//
// Measured from the perpendicular distances between a cell's centroid and its own
// face planes, which is the only cell-shape data BOTH mesh paths carry. Face
// vertices are not: createUnstructuredMesh fills FVFace::v0/v1 with point indices,
// but createMultiBlockFVMesh builds its faces from toPackedMesh, which has no
// vertex IDs at all and leaves them at -1 -- so any node-based formula reads
// points[-1] on every structured mesh.
//
//     d_f = |(faceCenter - cellCenter) . n_f|   (n_f is unit in all three builders)
//     AR  = max(d_f) / min(d_f)
//
// That is the usual meaning of aspect ratio for both cell shapes. For an
// axis-aligned quad it is dz/dr, since the two half-widths share the factor of 2;
// for a triangle it is Lmax/Lmin, since the centroid sits at 2A/(3L) from each
// edge, so d is proportional to 1/L. An ideal cell -- square or equilateral --
// scores exactly 1, which the old centroid-to-node terms did not give (they made a
// perfect square 1.41 and an equilateral triangle 2, being circumradius/inradius).
//
// A cell with no faces, or whose centroid lies on one of its own face planes, has
// no measurable ratio and is reported as 0 -- below the 1.0 floor of a real value,
// so the caller can tell "degenerate" from "excellent".
void calculateAspectRatio(
	const FVMesh& fvMesh,
	std::vector<double>& aspectRatios
) {

	int nCells = fvMesh.numCells();

	aspectRatios.assign(nCells, 0.0);

	for (int c = 0; c < nCells; ++c) {
		const FVCell& cell = fvMesh.cells[c];

		double dMin = std::numeric_limits<double>::max();
		double dMax = 0.0;

		for (int faceID : cell.faceIDs) {
			const FVFace& face = fvMesh.faces[faceID];

			// centroid -> face centroid, projected on the face normal
			double fz = face.center.z - cell.center.z;
			double fr = face.center.r - cell.center.r;
			double d = std::abs(fz * face.normal.z + fr * face.normal.r);

			dMin = std::min(dMin, d);
			dMax = std::max(dMax, d);
		}

		aspectRatios[c] = (dMin > 1e-30) ? dMax / dMin : 0.0;
	}
}

void Quality::buildQuality(const FVMesh& fvMesh) {

	calculateAspectRatio(fvMesh, aspectRatios);
	calculateOrthogonality(fvMesh, nonOrthogonality);
	calculateSkewness(fvMesh, skewness);

}

void Quality::reset() {
	// all three together: the inspector overlays trust a metric whose length matches
	// the cell count, so a half-cleared Quality is a set of stale numbers waiting to
	// be painted onto whatever mesh happens to have the same cell count next.
	aspectRatios.clear();
	nonOrthogonality.clear();
	skewness.clear();
}
