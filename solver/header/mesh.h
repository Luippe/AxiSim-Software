#pragma once

#include <optional>

#include "setting.cuh"
#include "buffer_manager.h"
#include "boundary_struct.h"
#include "graphics_struct.h"

class Shader;
class Console;
class Config;



class Mesh {
public:


	int nseg = 64;	// number of vertices on the circle
	float ntheta; // angle of each triangle in circle
	bool showMesh = true;
	bool showFill = true;
	bool isReady = false;

	// grid vertices and indices
	std::vector<Vertex> vertices;
	std::vector<float> gridLineVertices;
	std::vector<float> gridVertices;
	std::vector<unsigned int> indices;

	// boundary varaibles
	std::unordered_set<int> selectedBoundaryIDs;
	std::unordered_set<MeshEdge, MeshEdgeHash> selectableOuterEdges;
	std::vector<BoundarySegment> boundarySegments;
	std::vector<BoundarySegmentGroup> boundaryGroups;
	std::unordered_set<int> highlightedBoundarySegmentIDs;

	int nextGroupID = 0;




	Mesh(Config& config);

	GridConfig& g;

	Console* console = nullptr;

	// render the mesh, wireframe, and outline
	void render();

	void generate();


	void updateAfterLoadingFile();

	void createGrid();
	void createGridLineVertices();
	void createGridVertices();
	FVMesh createStructuredMesh(const std::vector<uint8_t>& activeCell);

	// search for segment with specific ID
	BoundarySegment* getBoundarySegmentByID(int id);

	BoundarySegmentGroup* getBoundaryGroupByID(int id);

	BoundarySegmentGroup* getBoundaryGroupByName(const std::string& name);

	// get the next avaiable group id that does not conflict with existing group ids
	int getAvailableBoundaryGroupID() const;

	// highlight all boundary segment in a group
	void highlightSegmentsInGroup(const BoundarySegmentGroup& group);

	// create boundary group using the selected boundary segments
	std::optional<BoundarySegmentGroup> createBoundaryGroupFromSelection();


private:



	void createCylinderVertices();

	std::vector<MeshEdge> edgesFromBoundarySegment(
		const BoundarySegment& seg
	) const;

};
