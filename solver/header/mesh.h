#pragma once

#include <optional>
#include <array>

#include "multiblock.h"

#include "setting.cuh"
#include "buffer_manager.h"
#include "boundary_struct.h"
#include "graphics_struct.h"
#include "sketch_struct.h"


class Console;
class Config;

class Mesh {
public:

	const char* sizingType[3] = { "Edge Count", "Target Spacing", "None"};
	const char* meshType[2] = { "Structured", "Unstructured" };

	BoundarySizing getSizingForSegment(const BoundarySegment& seg) const;
	void rebuildBoundaryDiscretization();

	bool isClosedControlPath(const BoundarySegment& seg) const;

	void rebuildSegmentDiscretization(
		BoundarySegment& seg,
		std::unordered_map<PointKey, int, PointKeyHash>& vertexLookup,
		double tol
	);

	void clearUnstructuredGeometry();

	void initializeUnstructuredDomain(
		int nzPoints,
		int nrPoints
	);

	bool hasDomainBoundarySegments() const;

	int addBoundarySegmentFromVertices(
		const std::vector<int>& vertexIDs,
		BoundarySource source
	);

	int getNumberOfEdgesForSegment(
		const BoundarySegment& seg,
		const BoundarySizing& sizing,
		double length,
		bool closed
	) const;

	void createDefaultUnstructuredDomainBoundarySegments(
		int nzPoints,
		int nrPoints
	);

	int createObstacleBoundaryGroup(const std::string& name);
	void addCircularObstacle(
		Vec2 center,
		double radius,
		int nObstaclePoints
	);
	int addUnstructuredBoundaryVertex(Vec2 p);
	int getAvailableLoopID();

	void runGmshTriangulation();
	void applyRegionOfInfluenceFields(double defaultMeshSize);

	int nseg = 64;	// number of vertices on the circle
	bool showFill = true;
	bool isReady = false;

	// current grid type
	MeshType currentMeshType = MeshType::Unstructured;

	// multiblock variables
	MultiBlockMesh multiBlock;
	bool isMultiBlock = false;   // routes the inspector to the segment renderer

	// Structured trellis resolution: axial cells per z-band and radial cells per
	// r-band. Bands are the gaps between the distinct z/r coordinates of the sketch
	// geometry; every block seam stays conformal because neighbouring blocks share a
	// band's cell count. Indexed left->right (z) and bottom->top (r).
	std::vector<int> zBandCells;
	std::vector<int> rBandCells;


	// grid vertices and indices
	std::vector<Vertex> vertices;
	std::vector<float> gridLineVertices;
	std::vector<float> gridVertices;
	std::vector<unsigned int> indices;

	// boundary varaibles
	std::unordered_set<int> selectedBoundaryIDs;
	std::unordered_set<MeshEdge, MeshEdgeHash> selectableOuterEdges;
	std::vector<BoundarySegmentGroup> boundaryGroups;
	std::vector<BoundaryEdge> boundaryEdges;
	std::unordered_set<int> highlightedBoundarySegmentIDs;

	std::vector<Vec2> unstructuredPoints;
	std::vector<Triangle> unstructuredTriangles;
	std::vector<BoundaryVertex> boundaryVertices;
	std::vector<BoundarySegment> boundarySegments;
	std::vector<MeshRegionOfInfluence> regionsOfInfluence;

	int nextGroupID = 0;
	int nextRegionOfInfluenceID = 0;

	Mesh(Config& config);

	GridConfig& g;

	Console* console = nullptr;

	// render the mesh, wireframe, and outline
	void render();

	void generate();

	bool convertSketchToUnstructuredMesh(const SketchModel& sketch);

	void createMultiBlockLineVertices(const MultiBlockMesh& mb);

	// Build a solver-ready host FVMesh from the multiblock (cells + faces via
	// toPackedMesh). External faces are classified into boundary groups by geometric
	// match against the sketch-derived boundaryEdges -- the same scheme the
	// unstructured createFVMesh uses -- since toPackedMesh leaves them at the block
	// edgeGroup (-1 for trellis blocks). Consumed through the face-based solver path.
	FVMesh createMultiBlockFVMesh() const;

	// Build an inspectable host FVMesh from the multiblock (via createMultiBlockFVMesh)
	// plus the 4 corner vertices of each cell (same global order), so the Mesh
	// Inspector can pick/highlight/report multiblock cells.
	void buildMultiBlockInspectMesh(FVMesh& out,
		std::vector<std::array<Vec2, 4>>& quads) const;

	// Map each raster cell (g.nr x g.nz, row-major i*nz+j) to the global index of the
	// multiblock cell covering its center, or -1 if none (obstacles / outside domain).
	// The results pipeline is raster-based (it revolves g.zFace/g.rFace and samples a
	// raster texture), so a multiblock solution -- which is stored per multiblock cell,
	// not on the raster -- must be resampled onto the raster grid before display. Built
	// once from the axis-aligned block rectangles and reused across all fields.
	std::vector<int> buildMultiBlockRasterMap() const;

	// Returns false (with a user-facing reason) if the sketch has geometry the
	// rectilinear structured/multiblock code can't represent: circles, arcs, skewed
	// (non-axis-aligned) outline edges, open line loops, or degenerate rectangles.
	bool sketchSupportsStructured(const SketchModel& sketch, std::string& reason) const;

	// Build the multi-block structured grid by trellis decomposition: every distinct
	// z/r coordinate of the geometry becomes a grid line, and each interior cell
	// (center inside the domain) becomes a conformal block. Sets multiBlock +
	// isMultiBlock; falls back to a single domain block when the sketch is empty.
	void buildStructuredMultiBlock(const SketchModel& sketch);

	// multiBlock / isMultiBlock are derived from the sketch and not serialized, so
	// reconstruct them after a project load (mirroring the Generate path) -- else a
	// loaded structured project reverts to the raster grid in both the inspector and
	// the solver until re-Generate. Clears the flag for non-structured meshes so a
	// reused Mesh object doesn't carry a stale multiblock from a prior project.
	void rebuildMultiBlockAfterLoad(const SketchModel& sketch);

	// Distinct sorted z/r coordinates of the sketch geometry = the trellis lines.
	void computeTrellisLines(const SketchModel& sketch,
		std::vector<double>& zLines, std::vector<double>& rLines) const;

	// Resize the per-band cell-count vectors to match the current trellis, keeping
	// existing entries; new bands default to a sensible cell count.
	void ensureBandSizes(size_t nZBands, size_t nRBands);

	// rasterize the sketched geometry onto a structured grid: cells whose center
	// falls outside the domain (or inside an obstacle) become solid. The grid
	// extents come from the sketch, not from preset g.L / g.R defaults.
	bool convertSketchToStructuredMesh(const SketchModel& sketch);

	void updateAfterLoadingFile();

	// Return the mesh to the same blank state as a freshly constructed Mesh, so a
	// new project doesn't inherit the previous project's geometry, boundary groups,
	// or multiblock. g (== config.g) is reset separately by Project::createNew.
	void reset();

	float displayZ(double z) const;
	float displayR(double r) const;

	void createGrid();
	void createGridLineVertices();
	void createGridVertices();
	void createUnstructuredVertices(
		const std::vector<Vec2>& points,
		const std::vector<Triangle>& triangles
	);
	void createUnstructuredLineVertices(
		const std::vector<Vec2>& points,
		const FVMesh& mesh
	);

	FVMesh createFVMesh(const std::vector<uint8_t>& activeCell) const;

	FVMesh createUnstructuredMesh(
		const std::vector<Vec2>& points,
		const std::vector<Triangle>& triangles,
		const std::vector<BoundaryVertex>& boundaryVertices,
		const std::vector<BoundaryEdge>& boundaryEdges
	) const;

	FVMesh createStructuredMesh(const std::vector<uint8_t>& activeCell) const;

	// search for segment with specific ID
	BoundarySegment* getBoundarySegmentByID(int id);

	// get the next avaiable group id that does not conflict with existing group ids
	int getAvailableBoundaryGroupID() const;

	std::unordered_set<int> getSegmentIDsInSameLoop(int segmentID) const;

	// highlight all boundary segment in a group
	void highlightSegmentsInGroup(const BoundarySegmentGroup& group);

	// create boundary group using the selected boundary segments
	std::optional<BoundarySegmentGroup> createBoundaryGroupFromSelection();

private:

	int nextLoopID = 0;

	void createCylinderVertices();

	// From the rasterized activeCell grid, collect the interior grid faces that
	// separate a fluid cell from a solid cell into selectableOuterEdges. These are
	// the obstacle/wall boundaries the Mesh Inspector displays and lets the user
	// pick; without them any wall not lying on the outer grid border is invisible.
	void rebuildSelectableObstacleEdges();

	// even-odd ray cast: is a world point inside the discretized boundary loops?
	bool pointInsideDomain(const Vec2& p) const;

};
