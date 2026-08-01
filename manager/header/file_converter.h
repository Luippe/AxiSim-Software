#pragma once

#include <filesystem>

#include "boundary_struct.h"
#include "file_struct.h"

struct Block;
struct MultiBlockMesh;


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
std::vector<Vec3> verticesFromBlock(const Block& block, double wedgeAngleDeg = 5.0);

// create hex from a block in multiblock mesh
Hex hexFromBlock(const Block& block);

// Sort every EXTERNAL block edge into blockMeshDict patches, keyed by patch name.
//
// Sources, in the order they land in the map:
//   - "back" / "front", type wedge, one face per block. Mandatory for a wedge
//     mesh; blockMesh will not accept the case without both.
//   - one patch per boundary group actually used by an external edge, named from
//     BoundarySegmentGroup::name (sanitized to an OpenFOAM `word`) and typed from
//     its BoundaryType.
//
// Interface edges are skipped: the point merge makes them interior faces. Faces
// lying on the axis are skipped too -- they collapse to zero area. `groups` is
// only read for names and types, so Mesh::boundaryGroups can be passed straight in.
std::unordered_map<std::string, BoundaryFOAM> boundaryFromMultiblock(
	const MultiBlockMesh& mesh, const std::vector<BoundarySegmentGroup>& groups);

// blockMeshDict spelling of a patch type, for the dict writer.
const char* foamPatchTypeName(BoundaryFOAMType type);

// populate BlockMeshDict
BlockMeshDict blockMeshDictFromMultiblock(const MultiBlockMesh& mesh, const std::vector<BoundarySegmentGroup>& groups);

// Write `dict` out as an OpenFOAM blockMeshDict, ready for `blockMesh` to read.
// Returns false (and says why on stderr) if the file cannot be opened or written;
// a partial file is left on disk either way, so a caller that needs atomicity has
// to write to a scratch name and rename.
//
// Global vertex labels are formed HERE. hexFromBlock leaves Hex::indices hex-local
// (0..7) while boundaryFromMultiblock has already resolved its faces against block
// base b*8, so the block entries -- and only those -- get the base added on the way
// out. This is the one place the two numbering spaces meet.
bool writeBlockMeshDict(const std::filesystem::path& path, const BlockMeshDict& dict);

// Write a stub system/controlDict at `path`. Nothing in it is derived from the
// project -- it is the steady-state defaults, with the solver name and the run
// length left for the user.
//
// Needed by blockMesh, not only by a solver: blockMesh constructs a Time object
// before reading the mesh dict, and Time reads system/controlDict as MUST_READ, so
// a case without one fails on a missing file rather than on anything about the mesh.
bool writeControlDict(const std::filesystem::path& path);

// Initial + boundary conditions for the 0/ directory, one FoamField per file.
//
// Built from the DICT rather than the multi-block mesh, because the patch names in
// 0/ have to be the ones blockMeshDict already committed to -- foamPatchName
// sanitizes and de-collides them, and that is not reversible. BoundaryFOAM::groupID
// is the way back to each patch's conditions.
//
// U and p are always written. T and C are written only when some group carries a
// condition for them, which is how the Solver tab's field checkboxes reach here.
//
// Conditions AxiSim has and OpenFOAM does not -- pulsatile inlets, Michaelis-Menten
// and Hill wall fluxes -- are written as their nearest steady equivalent with a //
// note on the entry saying what was lost. Nothing is dropped silently.
std::vector<FoamField> initialFieldsFromDict(
	const BlockMeshDict& dict, const std::vector<BoundarySegmentGroup>& groups);

// Write one file per field into `dir`, which must already exist and should be the
// case's 0/. Returns false (and says why on stderr) on the first field that cannot
// be written, leaving the fields before it on disk.
bool writeInitialFields(const std::filesystem::path& dir, const std::vector<FoamField>& fields);