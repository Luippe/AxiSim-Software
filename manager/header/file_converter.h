#pragma once
#include <vector>
#include <array>
#include "boundary_struct.h"

struct Block;

struct Hex {


	std::array<int, 8> indices;
	std::array<int, 3> size;
	std::array<double, 3> grading;

};

// wedge vertices for a block in multiblock mesh, flattened as 8 (x, y, z) triples
// in the order a blockMeshDict hex entry wants them:
//
//   0..3  back plane  (-angle/2):  node(0,0)  node(0,nz)  node(nr,nz)  node(nr,0)
//   4..7  front plane (+angle/2):  same four corners, same order
//
// Vertex k of block b is therefore global label b*8 + k, so hexFromBlock is base
// + 0..7 with no cross-block dedup pass -- that is what `mergeType points;` in the
// dict handles. wedgeAngleDeg is the FULL included angle, as the dict quotes it.
//
// A block on the axis has r = 0 corners whose back and front points coincide. They
// are still emitted, to keep the stride uniform, but the hex must reference the
// BACK label twice for them: 8 distinct labels would leave the cell a zero-
// thickness edge instead of collapsing it to a prism.
std::vector<double> verticesFromBlock(const Block& block, double wedgeAngleDeg = 5.0);

// create hex from a block in multiblock mesh
Hex hexFromBlock(const Block& block);