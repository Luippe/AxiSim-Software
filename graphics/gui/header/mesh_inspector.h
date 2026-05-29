#pragma once
#include "pch.h"


#include <glm/fwd.hpp>
#include <unordered_set>
#include <unordered_map>
#include <optional>

#include "base_surface_viewer.h"

#include "buffer_manager.h"
#include "graphics_struct.h"
#include "solver_struct.h"

class Mesh;
struct GridConfig;

enum class EdgeOrient : uint8_t {
	Vertical, 
	Horizontal
};

struct MeshEdge {
	EdgeOrient orient;

	int i;
	int j;
};

inline uint64_t edgeKey(const MeshEdge& e) {
	return (uint64_t(e.orient == EdgeOrient::Horizontal) << 63)
		| (uint64_t(uint32_t(e.i)) << 32)
		| uint32_t(e.j);
}

inline bool operator==(const MeshEdge& a, const MeshEdge& b) {
	return a.orient == b.orient &&
		a.i == b.i &&
		a.j == b.j;
}

struct MeshEdgeHash {
	std::size_t operator()(const MeshEdge& e) const {
		std::size_t h1 = std::hash<int>{}(static_cast<int>(e.orient));
		std::size_t h2 = std::hash<int>{}(e.i);
		std::size_t h3 = std::hash<int>{}(e.j);

		return h1 ^ (h2 << 1) ^ (h3 << 2);
	}
};

struct GridVertex {
	int i;
	int j;
};

inline bool operator==(const GridVertex& a, const GridVertex& b) {
	return a.i == b.i && a.j == b.j;
}

struct GridVertexHash {
	std::size_t operator()(const GridVertex& v) const {
		std::size_t h1 = std::hash<int>{}(v.i);
		std::size_t h2 = std::hash<int>{}(v.j);
		return h1 ^ (h2 << 1);
	}
};

struct BoundarySegment {
	GridVertex a;
	GridVertex b;
};

struct GridVertexEdge {
	GridVertex a;
	GridVertex b;
};

inline bool operator==(const GridVertexEdge& e1, const GridVertexEdge& e2) {
	return e1.a == e2.a && e1.b == e2.b;
}

struct VertexEdgeHash {
	std::size_t operator()(const GridVertexEdge& e) const {
		GridVertexHash vh;

		std::size_t h1 = vh(e.a);
		std::size_t h2 = vh(e.b);

		return h1 ^ (h2 << 1);
	}
};



class MeshInspector : public BaseSurfaceViewer {
public:

	MeshInspector(Mesh& mesh, AppAssets& assets);

	VertexBuffer vertexBuffer;

	// create dockspace to have multiple tabs
	DockingSpace meshDockSpace{ "Mesh Inspector" };


	void generate();

	// toolbar is drawn first, then the 2D field is rendered using the remaining space
	void render();

	// copy surface to clipboard
	void copyActiveSurfaceToClipboard();

	// create buffer using mesh.gridVertices
	void createGridBuffer();


private:

	// ----------dependencies-----------
	Mesh& mesh;
	GridConfig& g;

	// ----------mesh analyzer region-----------
	int nrBase = 0;
	int nzBase = 0;

	// ----------resources-----------
	AppAssets& assets;

	//-------------boundary lines--------------
	bool toggleConnecting = false;

	std::unordered_set<MeshEdge, MeshEdgeHash> selectableOuterEdges;
	std::vector<GridVertex> gridVertices;

	int cellIndex(int i, int j) const;
	bool isInsideCellGrid(int i, int j) const;
	bool isSolidCell(int i, int j, const std::unordered_set<int>& obstacleIndices) const;
	void rebuildSelectableOuterEdges(const std::unordered_set<int>& obstacleIndices);

	// render the preview onto fbo
	void renderPreview();

	// handle mouse events
	void handleMouse();

	// draw toolbar at the top of the mesh analyzer, which can be used for variety of functions
	void drawToolBar();

	// draw popup menu when right clicked
	void drawPopup();

	// draw horizontally and vertically merged edges
	
	// draw text at clicked position
	void drawTextAtSurfacePoint();

	void drawBoundarySegments(
		const std::vector<BoundarySegment>& segments,
		const std::vector<double>& rFace,
		const std::vector<double>& zFace
	);

	MeshEdge nearestEdgeFromGridPoint(const ImVec2& p);

	void setBaseNrNz();

};