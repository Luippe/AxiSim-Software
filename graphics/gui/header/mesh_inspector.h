#pragma once
#include "pch.h"

#include <glm/fwd.hpp>
#include <unordered_set>
#include <unordered_map>
#include <optional>
#include <array>

#include "base_surface_viewer.h"

#include "buffer_manager.h"
#include "core_struct.h"
#include "graphics_struct.h"
#include "solver_struct.h"
#include "boundary_struct.h"

class Mesh;
struct GridConfig;

class MeshInspector : public BaseSurfaceViewer {
public:

	MeshInspector(Mesh& mesh, AppConfig& appConfig);
	void updateGridBuffer();

	VertexBuffer vertexBuffer;
	GLsizeiptr gridLineBufferBytes = 0;

	// create dockspace to have multiple tabs
	DockingSpace meshDockSpace{ "Mesh Inspector" };

	void generate();

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
	Mesh& mesh;
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

	//-------------pending variables------------
	PendingCircle pendingCircle;
	PendingRect pendingRect;

	//-------------status bar--------------
	float statusBarHeight = 100.0f;

	// -------------drawing variables--------------
	ImColor drawingColor = IM_COL32(203, 209, 224, 255);

	int cellIndex(int i, int j) const;
	bool isInsideCellGrid(int i, int j) const;
	bool isSolidCell(int i, int j, const std::unordered_set<int>& obstacleIndices) const;
	void rebuildSelectableOuterEdges(const std::unordered_set<int>& obstacleIndices);
	bool isDomainBoundaryEdge(const MeshEdge& e) const;
	bool domainEdgeTouchesSolid(const MeshEdge& e, const std::unordered_set<int>& obstacleIndices) const;

	// set group total length
	void setGroupTotalLength(BoundarySegmentGroup& group);

	// finds what orientation the group includes in its edges vector
	void setGroupOrientation(BoundarySegmentGroup& group);

	// render the preview onto fbo
	void renderPreview();

	// build all boundary segments
	void buildSegments();

	// handle mouse events
	void handleMouse();
	void handleOpenPopup();
	void handleItemButtonSelect();
	void handleItemButtonRemove();
	void handleDrawCircle();
	void handleDrawRectangle();

	void handleCursor(ImGuiIO& io);
	std::optional<int> findHoveredBoundarySegment();

	std::array<MeshEdge, 4> getCellEdges(int i, int j) const;

	// draw toolbar at the top of the mesh analyzer, which can be used for variety of functions
	void drawToolBar();

	// draw popup menu when right clicked
	void drawPopup();
	
	// draw a status bar that shows important detail of the mesh
	void drawStatusBar();

	// draw text at clicked position
	void drawTextAtSurfacePoint(ImDrawList* drawList);

	// draw pending objects such as circles and rectangles while they
	void drawPendingObjects(ImDrawList* drawList);

	void drawBoundarySegments(ImDrawList* drawList);

	void fillBoundaryGroupEdges(BoundarySegmentGroup& group);
	bool obstacleCellTouchesBoundaryGroup(int cellIndex) const;
	bool tryAddObstacleCell(int cellIndex);
	void tryAddObstacleCellsInRect(
		const ImVec2& start,
		const ImVec2& end
	);


	// build domain segments
	std::unordered_set<MeshEdge, MeshEdgeHash> buildDomainBoundaryEdges() const;

	std::unordered_set<MeshEdge, MeshEdgeHash> buildCombinedBoundaryEdges(
		const std::unordered_set<MeshEdge, MeshEdgeHash>& selectableOuterEdges,
		const std::unordered_set<int>& obstacleIndices
	);

	void setBaseNrNz();

	// remove obstacles
	void syncAfterObstacleEdit();
	bool boundaryGroupStillValid(
		const BoundarySegmentGroup& group,
		const std::unordered_set<MeshEdge, MeshEdgeHash>& validEdges
	) const;
	bool removeInvalidBoundaryGroups();
	bool removeObstacleCell(int cellIndex);
	void removeObstacleCellsInRect(
		const ImVec2& start,
		const ImVec2& end
	);
	void clearObstacles();

};