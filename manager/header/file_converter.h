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
// These are POSITIONS only, block-local and un-deduplicated: a corner two blocks
// share comes back from both of them. weldVertices is what turns the whole set
// into dict labels. wedgeAngleDeg is the FULL included angle, as the dict quotes it.
//
// A block on the axis has r = 0 corners whose back and front points coincide. They
// are still emitted, to keep the stride uniform; the weld is what collapses them
// to one label, and with them the cell to a prism.
std::vector<Vec3> verticesFromBlock(const Block& block, double wedgeAngleDeg = 5.0);

// One global vertex label per distinct corner of the whole mesh.
//
// `points` is the dict's `vertices` list; `labels[b][k]` is the label of block b's
// hex-local corner k, as verticesFromBlock orders them. Blocks that meet share the
// labels along the seam, and a block on the axis repeats one.
struct VertexWeld {
	std::vector<Vec3> points;
	std::vector<std::array<int, 8>> labels;
};

// Weld the per-block corners of `mesh` into that one global labelling.
//
// This is what CONNECTS the blocks, and it is not optional. blockMesh does not
// discover adjacency from coordinates: even under `mergeType points` its geometric
// pass only refines point matching across faces that are ALREADY internal in the
// block topology (blockMeshMergeGeometrical.C gates the cross-block merge on
// isInternalFace). Give every block its own 8 labels and blockMesh reports
// "Number of internal faces : 0", drops both sides of every seam onto its default
// patch -- type `empty`, which then costs the case its axial and radial solution
// directions -- and writes one disconnected region per block. checkMesh calls that
// mesh OK, and the run dies much later, inside GAMG, on a 0/0 in an all-zero
// pressure field.
//
// Corners are matched by POSITION within a tolerance, not by exact equality: two
// blocks reach a shared band corner through different arithmetic and land about an
// ulp apart. The tolerance is relative to the mesh extent and sits orders of
// magnitude below one cell, so it can only ever fuse corners meant to be the same
// point.
VertexWeld weldVertices(const MultiBlockMesh& mesh, double wedgeAngleDeg = 5.0);

// create hex from a block in multiblock mesh. `labels` is the block's row of
// VertexWeld::labels, and lands in Hex::indices unchanged.
Hex hexFromBlock(const Block& block, const std::array<int, 8>& labels);

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
//
// The group of an edge is found GEOMETRICALLY, from the sketch boundary nearest its
// midpoint -- not from Block::edgeGroup, which no trellis-decomposed mesh ever fills
// in (the only setEdgeGroup calls in the tree are in buildFiveBlockExample). This is
// deliberately the same nearest-edge rule createMultiBlockFVMesh applies to face
// centres, because the export and the solver have to partition the boundary
// identically: if they disagree about which face is the inlet, the two codes are not
// solving the same problem and comparing their fields means nothing.
//
// An edge matching no group within tolerance still gets a patch -- named
// "unassigned", warned about on stderr -- because dropping its faces would hand
// blockMesh a mesh with a hole and no clue where.
//
// `weld` supplies the vertex labels the faces are written against, so it has to be
// the same one the dict's `vertices` list came from.
std::unordered_map<std::string, BoundaryFOAM> boundaryFromMultiblock(
	const MultiBlockMesh& mesh, const std::vector<BoundarySegmentGroup>& groups,
	const std::vector<BoundaryEdge>& boundaryEdges,
	const std::vector<BoundaryVertex>& boundaryVertices, const VertexWeld& weld);

// blockMeshDict spelling of a patch type, for the dict writer.
const char* foamPatchTypeName(BoundaryFOAMType type);

// populate BlockMeshDict. The two boundary vectors are forwarded to
// boundaryFromMultiblock, which needs the sketch geometry to classify block edges.
BlockMeshDict blockMeshDictFromMultiblock(
	const MultiBlockMesh& mesh, const std::vector<BoundarySegmentGroup>& groups,
	const std::vector<BoundaryEdge>& boundaryEdges,
	const std::vector<BoundaryVertex>& boundaryVertices);

// Write `dict` out as an OpenFOAM blockMeshDict, ready for `blockMesh` to read.
// Returns false (and says why on stderr) if the file cannot be opened or written;
// a partial file is left on disk either way, so a caller that needs atomicity has
// to write to a scratch name and rename.
//
// Labels are written verbatim -- weldVertices already made them global -- so the
// only numbering work left here is checking that every one of them indexes
// dict.vertices, which is done up front rather than left to blockMesh.
bool writeBlockMeshDict(const std::filesystem::path& path, const BlockMeshDict& dict);

// The four dictionaries that turn a meshed folder into a runnable case. Each
// returns false and says why on stderr if its file cannot be written.
//
// system/controlDict picks pimpleFoam or simpleFoam off setup.transient, carries
// the run length, and appends a scalarTransport function object per solved scalar
// -- stock OpenFOAM flow solvers transport no scalars, so that is what lets one
// run produce U, T and C together.
//
// "Run length" is endTime/deltaT from the Solver tab for a transient run, but for
// a steady one it is setup.steadyIterations, which is AxiSim's outer-iteration cap
// only when that exceeds the 2000 floor. See FoamCaseSetup for why it never goes
// below.
//
// It is also needed by blockMesh, not only by a solver: blockMesh constructs a
// Time object before reading the mesh dict, and Time reads controlDict as
// MUST_READ, so a case without one fails on a missing file rather than on the mesh.
bool writeControlDict(const std::filesystem::path& path, const FoamCaseSetup& setup);

// system/fvSchemes. ddt follows transient + secondOrderTime, grad follows
// leastSquaresGradient, div follows the Solver tab's convection scheme. `default
// none` for divSchemes means every div the solver forms has to be named, so the
// scalar entries are written only for the scalars actually being solved.
//
// setup.addConvection is the exception that cannot be honoured: a stock flow
// solver always assembles div(phi,U). With it off the entry is still written, but
// annotated in the dict and warned about on stderr, since the exported case then
// solves a different equation set than AxiSim did.
bool writeFvSchemes(const std::filesystem::path& path, const FoamCaseSetup& setup);

// system/fvSolution: linear solvers, the SIMPLE/PIMPLE block, and the project's
// relaxation factors. nNonOrthogonalCorrectors follows setup.nonOrthCorrector, and
// T and Conc get separate relaxation entries because AxiSim relaxes temperature
// with the momentum factor and leaves concentration unrelaxed.
//
// The linear solvers themselves (GAMG for p, smoothSolver for the rest) are
// OpenFOAM's own and deliberately NOT AxiSim's: an inner solve is iterated to a
// tolerance either way, so the choice moves the cost and not the converged answer.
//
// residualControl is deliberately NOT the Solver tab's convergence tolerance. The
// two are not comparable -- AxiSim's is its own scaled residual, OpenFOAM's is the
// initial residual of an outer iteration under its own normalisation -- and
// carrying AxiSim's 1e-3 across stops SIMPLE ~9% short of the developed profile.
bool writeFvSolution(const std::filesystem::path& path, const FoamCaseSetup& setup);

// constant/transportProperties. mu goes out as kinematic nu, since the
// incompressible solvers divide rho out of the equation set entirely.
bool writeTransportProperties(const std::filesystem::path& path, const FoamCaseSetup& setup);

// constant/turbulenceProperties, always `laminar`. Not optional: the flow solvers
// construct a turbulence model unconditionally and abort without this file.
bool writeTurbulenceProperties(const std::filesystem::path& path);

// Initial + boundary conditions for the 0/ directory, one FoamField per field.
//
// Built from the DICT rather than the multi-block mesh, because the patch names in
// 0/ have to be the ones blockMeshDict already committed to -- foamPatchName
// sanitizes and de-collides them, and that is not reversible. BoundaryFOAM::groupID
// is the way back to each patch's conditions.
//
// U and p are always written; T and C follow setup's two field flags, the same two
// that decide the scalarTransport entries in controlDict.
//
// Conditions AxiSim has and OpenFOAM does not -- pulsatile inlets, Michaelis-Menten
// and Hill wall fluxes -- are written as their nearest steady equivalent with a //
// note on the entry saying what was lost. Nothing is dropped silently.
std::vector<FoamField> initialFieldsFromDict(
	const BlockMeshDict& dict, const std::vector<BoundarySegmentGroup>& groups,
	const FoamCaseSetup& setup);

// Write one file per field into `dir`, which must already exist and should be the
// case's 0/. Returns false (and says why on stderr) on the first field that cannot
// be written, leaving the fields before it on disk.
bool writeInitialFields(const std::filesystem::path& dir, const std::vector<FoamField>& fields);