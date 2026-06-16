#pragma once

#include <optional>

#include "setting.cuh"
#include "buffer_manager.h"
#include "boundary_struct.h"
#include "graphics_struct.h"

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

	int nseg = 64;	// number of vertices on the circle
	bool meshMode = false;
	bool showFill = true;
	bool isReady = false;

	// current grid type
	MeshType currentMeshType = MeshType::Unstructured;

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

	int nextGroupID = 0;

	Mesh(Config& config);

	GridConfig& g;

	Console* console = nullptr;

	// render the mesh, wireframe, and outline
	void render();

	void generate();

	void updateAfterLoadingFile();

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

};
