#pragma once
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
	std::unordered_set<int> selectedBoundaryIds;
	std::unordered_set<MeshEdge, MeshEdgeHash> selectableOuterEdges;
	std::vector<BoundarySegment> boundarySegments;
	std::vector<BoundarySegmentGroup> boundaryGroups;

	int nextGroupID = -1;
	int namingBoundaryGroupID = -1;

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

	// search for segment with specific ID
	BoundarySegment* getBoundarySegmentByID(int id);

	BoundarySegmentGroup* getBoundaryGroupByID(int id);

	// create boundary group using the selected boundary segments
	void createBoundaryGroupFromSelection();
private:

	void createCylinderVertices();



};
