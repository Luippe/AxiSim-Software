#pragma once
#include "pch.h"

#include <glm/fwd.hpp>
#include <unordered_set>
#include <unordered_map>
#include <optional>
#include <array>

#include "base_surface_viewer.h"

#include "buffer_manager.h"
#include "camera.h"
#include "core_struct.h"
#include "graphics_struct.h"
#include "solver_struct.h"
#include "boundary_struct.h"

class Mesh;
class Geometry;
class Project;
struct GridConfig;

enum class MeshSnapType {
	None,
	Vertex,
	Line,
	Circle
};

struct MeshSnapResult {
	MeshSnapType type = MeshSnapType::None;
	Vec2 world{};
	ImVec2 screen{};
	float distancePx = 0.0f;
	int entityID = -1;
};

class MeshInspector : public BaseSurfaceViewer {
public:

	MeshInspector(Project& project, AppConfig& appConfig);

	VertexBuffer vertexBuffer;
	GLsizeiptr gridLineBufferBytes = 0;

	// create dockspace to have multiple tabs
	DockingSpace meshDockSpace{ "Mesh Inspector" };

	// toolbar is drawn first, then the 2D field is rendered using the remaining space
	void render();

	// copy surface to clipboard
	void copyActiveSurfaceToClipboard();

	// create buffer using mesh.gridVertices
	void createGridBuffer();

	// remove boundary group
	bool deleteBoundaryGroupByID(int groupID);

private:

	// ----------dependencies-----------
	Project& project;
	Mesh& mesh;
	Geometry& geometry;
	GridConfig& g;

	// ----------mesh analyzer region-----------
	int nrBase = 0;
	int nzBase = 0;

	// ----------resources-----------
	AppAssets& assets;

	//-------------boundary lines--------------
	float pickRadiusPx = 12.0f;
	std::optional<int> hoveredId;

	bool isPopupOpened = false;
	bool hoveringOverSegment = false;
	bool hoveringOverSelectedSegment = false;
	std::optional<BoundarySegmentGroup> pendingBoundaryGroup;
	std::string obstacleError;
	std::vector<int> namedIDs;
	bool toggleSnapping = false;
	Vec2 roiStartWorld{};
	Vec2 roiCurrentWorld{};

	//-------------pending variables------------
	PendingCircle pendingCircle;
	PendingRect pendingRect;

	//-------------status bar--------------
	float statusBarHeight = 100.0f;

	// -------------drawing variables--------------
	ImColor drawingColor = IM_COL32(203, 209, 224, 255);
	const ImU32 sketchBgColor = IM_COL32(102, 102, 102, 255);
	const ImU32 outlineColor = IM_COL32(150, 150, 150, 255);

	// -------------cell inspection--------------
	bool toggleInspectCell = false;	// toolbar mode: pick cells to read mesh data
	int selectedCell = -1;			// FV cell pinned by a left click (-1 = none)
	FVMesh inspectFVMesh;			// snapshot rebuilt when inspect mode turns on
	bool inspectMeshDirty = true;	// rebuild the snapshot on the next render

	int cellIndex(int i, int j) const;
	bool isInsideCellGrid(int i, int j) const;
	bool isSolidCell(int i, int j, const std::unordered_set<int>& obstacleIndices) const;
	bool isDomainBoundaryEdge(const MeshEdge& e) const;
	bool domainEdgeTouchesSolid(const MeshEdge& e, const std::unordered_set<int>& obstacleIndices) const;

	// set group total length
	void setGroupTotalLength(BoundarySegmentGroup& group);

	// finds what orientation the group includes in its edges vector
	void setGroupOrientation(BoundarySegmentGroup& group);

	// build all boundary segments
	void buildSegments();

	// handle mouse events
	void handleMouse();
	void handleOpenPopup();
	void handleDrawRegionOfInfluence();

	void handleCursor(ImGuiIO& io);
	std::optional<MeshSnapResult> findSnap(ImVec2 mouse);
	Vec2 getSnappedWorld(ImVec2 mouse);
	std::optional<int> findHoveredBoundarySegment();

	// draw toolbar at the top of the mesh analyzer, which can be used for variety of functions
	void drawToolBar();

	// draw popup menu when right clicked
	void drawPopup();

	// draw text at clicked position
	void drawTextAtSurfacePoint(ImDrawList* drawList);

	// draw pending objects such as circles and rectangles while they
	void drawPendingObjects(ImDrawList* drawList);
	void drawSnapping(ImDrawList* drawList);

	void drawMeshLines(ImDrawList* drawList);
	void drawHighlightedCells2D(ImDrawList* drawList);
	void drawBoundarySegments(ImDrawList* drawList);
	void drawRegionsOfInfluence(ImDrawList* drawList);

	// -------------cell inspection--------------
	// rebuild the FV mesh snapshot used for picking/reading cell data
	void buildInspectMesh();

	// pick the cell under a world-space point (-1 if none)
	int pickCell(const Vec2& world) const;

	// pin/unpin a cell on left click (only while inspect mode is on)
	void handleCellSelection(ImGuiIO& io);

	// max non-orthogonality over the cell's interior faces, in degrees
	// (-1 if the cell has no interior faces); also reports the average
	double cellNonOrthogonality(int cellID, double& avgDeg, int& interiorFaces) const;

	// highlight the pinned cell and draw a panel with its mesh data
	void drawCellInfo(ImDrawList* drawList);

	void fillBoundaryGroupEdges(BoundarySegmentGroup& group);

	// build domain segments
	std::unordered_set<MeshEdge, MeshEdgeHash> buildDomainBoundaryEdges() const;

	std::unordered_set<MeshEdge, MeshEdgeHash> buildCombinedBoundaryEdges(
		const std::unordered_set<MeshEdge, MeshEdgeHash>& selectableOuterEdges,
		const std::unordered_set<int>& obstacleIndices
	);

	void setBaseNrNz();

};
