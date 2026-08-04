#include "quality.h"

#include <algorithm>

#include "boundary_struct.h"

void calculateSkewness(
	const FVMesh& fvMesh,
	const std::vector<double>& meshPoints,
	const std::vector<int>& cellCornerStart,
	const std::vector<int>& cellCornerIDs,
	std::vector<double>& skewness
) {
	int nCells = fvMesh.numCells();

	skewness.assign(nCells, 0.0);

	for (int c = 0; c < nCells; c++) {

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

			double fDot = fz * face.normal.z + fr * face.normal.r;
			double fMag = std::sqrt(fz * fz + fr * fr);

			orth = std::min(orth, fDot / fMag);

			// centroid to centroid
			if (face.isBoundary()) continue;

			const FVCell& nbCell = fvMesh.cells[face.neighbor];

			double dz = nbCell.center.z - cell.center.z;
			double dr = nbCell.center.r - cell.center.r;

			double dDot = dz * face.normal.z + dr * face.normal.r;
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

void Quality::buildQuality(
	const FVMesh& fvMesh, 
	const std::vector<double>& meshPoints,
	const std::vector<int>& cellCornerStart,
	const std::vector<int>& cellCornerIDs
) {

	calculateAspectRatio(fvMesh, aspectRatios);
	calculateOrthogonality(fvMesh, nonOrthogonality);
	calculateSkewness(fvMesh, meshPoints, cellCornerStart, cellCornerIDs, skewness);

}

void Quality::reset() {
	aspectRatios.clear();

}