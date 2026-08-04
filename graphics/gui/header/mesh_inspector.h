#pragma once
#include "pch.h"

#include <glm/fwd.hpp>
#include <unordered_set>
#include <unordered_map>
#include <optional>
#include <array>
#include <vector>

#include "base_surface_viewer.h"

#include "buffer_manager.h"
#include "camera.h"
#include "core_struct.h"
#include "graphics_struct.h"	// SurfacePoint / Vertex used below
#include "solver_struct.h"
#include "boundary_struct.h"
#include "app_struct.h"


class Mesh;
class Geometry;
class Project;
class Console;
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

	void render();

	// Drawn by GUI into the app-wide toolbar strip above the dockspace, not by
	// render(), so the band can span the whole window instead of this panel.
	void drawToolBar();

	// copy surface to clipboard
	void copyActiveSurfaceToClipboard();

	Console* console = nullptr;

	// create buffer using mesh.gridVertices
	void createGridBuffer();

	// mark the inspect-cell snapshot stale (call after the mesh is regenerated)
	void markMeshChanged() { inspectMeshDirty = true; selectedCell = -1; }

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

	bool hoveringOverSelectedSegment = false;
	std::optional<BoundarySegmentGroup> pendingBoundaryGroup;
	bool toggleSnapping = false;

	// rubber-band selection: drag a box across the canvas to select every boundary
	// segment it touches (Ctrl adds to the current selection). The box starts at
	// initLeftMouse and follows currentMousePos while active.
	bool isBoxSelecting = false;

	// whether the left button went DOWN over the canvas. A gesture belongs to
	// whatever it started on, so a press that began elsewhere must not turn into a
	// box selection the moment it is dragged in here.
	bool leftPressedOnCanvas = false;

	Vec2 roiStartWorld{};
	Vec2 roiCurrentWorld{};

	//-------------pending variables------------
	PendingCircle pendingCircle;
	PendingRect pendingRect;

	// -------------drawing variables--------------
	ImColor drawingColor = IM_COL32(203, 209, 224, 255);

	// -------------cell inspection--------------
	bool toggleInspectCell = false;	// toolbar mode: pick cells to read mesh data
	bool toggleMesh = true;
	bool toggleAspectRatio = false;	// overlay: shade every cell by its aspect ratio
	bool toggleOrthogonality = false;	// overlay: shade every cell by its non-orthogonality
	bool toggleSkewness = false;	// overlay: shade every cell by its skewness
	int selectedCell = -1;			// FV cell pinned by a left click (-1 = none)
	bool inspectMeshDirty = true;	// rebuild the snapshot on the next render

	// The three quality overlays are one view of the mesh, not three: each toolbar
	// toggle clears the other two, so only one shading -- and one legend, they share
	// the same corner -- is ever up. This is what the rest of the panel asks.
	bool qualityOverlayActive() const {
		return toggleAspectRatio || toggleOrthogonality || toggleSkewness;
	}

	// The FV mesh the inspector reads. Mesh owns and caches it (built at generate
	// time), so this is the same instance the solver runs on -- the inspector used
	// to keep its own copy built by a separate createFVMesh call. Defined in the
	// .cpp: Mesh is only forward-declared here.
	const FVMesh& inspectMesh() const;

	// Cell outlines come off the FVMesh itself (points + cellCornerStart /
	// cellCornerIDs), filled by the builders that fill its cells -- so they are
	// index-aligned with inspectMesh().cells on every mesh path, and cannot be a
	// refresh behind it. The inspector used to keep its own multiblock-only copy of
	// the corner quads, and picking, highlighting and the overlay each had to
	// dispatch three ways to find an outline.
	static constexpr int maxCellCorners = 8;

	// World-space corners of one FV cell, in CCW ring order. Returns the count
	// written, or 0 when the cell has no usable outline -- including a polygon with
	// more than maxCellCorners corners, which is dropped rather than truncated into
	// a wrong shape.
	int cellCorners(int cellID, Vec2* out, int maxOut) const;

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
	void syncStructuredBoundaryGroups();

	// handle mouse events
	void handleMouse();
	void handleOpenPopup();
	void handleDrawRegionOfInfluence();

	// select every boundary segment the rubber-band box touches. additive keeps the
	// current selection (Ctrl-drag) instead of replacing it.
	void applyBoxSelection(bool additive);

	// draw the rubber-band selection rectangle while a box drag is in progress
	void drawBoxSelection(ImDrawList* drawList);

	void handleCursor(ImGuiIO& io);
	std::optional<MeshSnapResult> findSnap(ImVec2 mouse);
	Vec2 getSnappedWorld(ImVec2 mouse);
	std::optional<int> findHoveredBoundarySegment();

	// draw popup menu when right clicked
	void drawPopup();

	// draw text at clicked position
	void drawTextAtSurfacePoint(ImDrawList* drawList);

	// draw pending objects such as circles and rectangles while they
	void drawPendingObjects(ImDrawList* drawList);
	void drawSnapping(ImDrawList* drawList);

	void drawMeshLines(ImDrawList* drawList);
	void drawHighlightedCells2D(ImDrawList* drawList);
	void drawUnstructuredSolidBodies(ImDrawList* drawList);
	void drawBoundarySegments(ImDrawList* drawList);
	void drawRegionsOfInfluence(ImDrawList* drawList);
	void drawAspectRatio(ImDrawList* drawList);
	void drawOrthogonality(ImDrawList* drawList);
	void drawSkewness(ImDrawList* drawList);

	// Shared body of the three overlays above: shade every cell by its own value on
	// a fixed [lo, hi] ramp, then raise the legend if anything was painted. Only the
	// numbers and the label differ between the metrics, so they all come through
	// here. `toMetric` converts a stored value into the quantity the legend is
	// labelled in (null when it is already in it).
	void drawQualityOverlay(
		ImDrawList* drawList,
		const std::vector<double>& values,
		double lo,
		double hi,
		const char* label,
		double (*toMetric)(double) = nullptr
	);

	// colorbar for the quality overlays, labelled with the range and the metric it shades
	void drawQualityLegend(ImDrawList* drawList, double lo, double hi, const char* label);


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

	// build the text report for a picked cell
	std::string buildCellInfoText(int cellID) const;

	// print the picked cell's mesh data to the console
	void logCellInfoToConsole();

	// highlight the pinned cell
	void drawCellInfo(ImDrawList* drawList);

	void fillBoundaryGroupEdges(BoundarySegmentGroup& group);

	// build domain segments
	std::unordered_set<MeshEdge, MeshEdgeHash> buildDomainBoundaryEdges() const;

	std::unordered_set<MeshEdge, MeshEdgeHash> buildCombinedBoundaryEdges(
		const std::unordered_set<MeshEdge, MeshEdgeHash>& selectableOuterEdges
	);

	void setBaseNrNz();

};
