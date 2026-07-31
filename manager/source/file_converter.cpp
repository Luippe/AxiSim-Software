#include "file_converter.h"

#include <cmath>

#include "multiblock.h"



std::vector<double> verticesFromBlock(const Block& block, double wedgeAngleDeg) {

	// The four (z, r) corners, ordered min -> +z -> +z+r -> +r. makeRectBlock lays
	// nodes out with I running +r and J running +z, so node(0,0) is the min corner
	// and this traversal is counter-clockwise in the r-z plane -- which is what
	// makes the hex's first face wind correctly. A block whose corners were handed
	// to makeQuadBlock in another order comes out inside-out here, and blockMesh
	// rejects it for negative volume.
	const MBNode corners[4] = {
		block.node(0, 0),
		block.node(0, block.nz),
		block.node(block.nr, block.nz),
		block.node(block.nr, 0)
	};

	// A single-layer wedge is symmetric about the r-z plane, so each side carries
	// half of the quoted included angle.
	const double halfAngle = 0.5 * wedgeAngleDeg * (MB_PI / 180.0);
	const double cosHalf = std::cos(halfAngle);
	const double sinHalf = std::sin(halfAngle);

	std::vector<double> vertices;
	vertices.reserve(24);

	// Back plane first, then front, so the 8 labels land in hex order.
	for (int side = 0; side < 2; side++) {
		const double sign = (side == 0) ? -1.0 : 1.0;
		for (const MBNode& corner : corners) {
			vertices.push_back(corner.z);                     // blockMesh x = axial
			vertices.push_back(corner.r * cosHalf);           // y = radius on the wedge face
			vertices.push_back(sign * corner.r * sinHalf);    // z = wedge offset
		}
	}

	return vertices;
}

Hex hexFromBlock(const Block& block) {

	Hex hex;

	double r = block.node(0, 0).r;
	if (r != 0.0) {
		hex.indices = { 0,1,2,3,4,5,6,7 };
	}
	else {
		hex.indices = { 0,1,2,3,0,1,6,7 };
	}

	hex.size = { block.nz, block.nr, 1 };
	hex.grading = { 1,1,1 };

	return hex;
}

void blockMeshDictFromMultiblock(const MultiBlockMesh& mesh) {

	for (int i = 0; i < mesh.blocks.size(); i++) {

	}

}