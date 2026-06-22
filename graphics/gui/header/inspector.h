#pragma once
#include "pch.h"

#include <string>
#include <vector>

#include "colorbar.h"
#include "camera.h"

#include "graphics_struct.h"
#include "base_surface_viewer.h"

class Mesh;
class Results;
class Project;
class SceneView;
struct GridConfig;
struct SolutionField;

// The Inspector renders the solved field over the (unstructured) finite-volume
// mesh using the same 2D ImDrawList + Camera2D approach as the Mesh Inspector
// and the geometry/sketch view. Each FV cell maps 1:1 to a triangle in
// mesh.unstructuredTriangles and is colored by the current solution field
// through the active colormap.
class Inspector : public BaseSurfaceViewer {
public:

	Inspector(Project& project, SceneView& scene, AppConfig& appConfig);

	// re-frame the camera onto the mesh the next time we render
	void generate();

	// toolbar is drawn first, then the 2D field is rendered using the remaining space
	void render();

	// copy active surface to clipboard
	void copyActiveSurfaceToClipboard();

	Colorbar colorbar;

private:

	// ----------dependencies-----------
	Project& project;
	SceneView& scene;
	Mesh& mesh;
	Results& results;
	GridConfig& g;

	// ----------resources-----------
	AppAssets& assets;

	// ----------2D view-----------
	Camera2D camera;

	bool pendingFrame = true;	// re-fit the view to the mesh on the next render
	bool showMesh = false;		// overlay the mesh wireframe

	int selectedCell = -1;		// cell pinned by a left click (-1 = none)

	// scratch buffers reused across frames for smooth (vertex) shading
	std::vector<float> vertexValues;
	std::vector<int> vertexCounts;

	// ----------helpers-----------

	// resolve the raw per-cell solution for the currently selected field
	const SolutionField* getCurrentSolution() const;

	// compute the value range of a solution field
	bool computeFieldRange(const SolutionField& sol, float& vmin, float& vmax) const;

	// map a scalar value to a color using the active colormap LUT
	ImU32 valueToColor(double value, double vmin, double vmax) const;

	// fit the camera so the whole mesh is visible
	void frameToMesh();

	// pick the triangle/cell under a world-space point (-1 if none)
	int pickCell(const Vec2& world) const;

	// ----------input-----------
	void handleMouse(ImGuiIO& io);

	// pin/unpin a cell on left click (separate from hover probe)
	void handleSelection(ImGuiIO& io);

	// ----------draw calls-----------
	void drawToolBar();
	void drawField(ImDrawList* drawList);
	void drawMeshOverlay(ImDrawList* drawList);
	void drawAxes(ImDrawList* drawList);
	void drawValueProbe(ImDrawList* drawList);

	// highlight the pinned cell and draw a panel with its full data
	void drawCellInfo(ImDrawList* drawList);

	void drawEmptyMessage(ImDrawList* drawList);

};
