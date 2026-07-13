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
class Console;
struct GridConfig;
struct SolutionField;
struct AppConfig;	// used only by reference below
struct AppAssets;

// The Inspector renders solved fields over the finite-volume mesh using the
// same 2D ImDrawList + Camera2D approach as the Mesh Inspector and the
// geometry/sketch view. Unstructured cells draw from mesh.unstructuredTriangles;
// structured cells draw from the grid face coordinates.
class Inspector : public BaseSurfaceViewer {
public:

	Inspector(Project& project, SceneView& scene, AppConfig& appConfig);

	// re-frame the camera onto the mesh the next time we render
	void generate();

	// toolbar is drawn first, then the 2D field is rendered using the remaining space
	void render();

	// copy active surface to clipboard
	void copyActiveSurfaceToClipboard();

	Console* console = nullptr;

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
	bool pendingFrame = true;	// re-fit the view to the mesh on the next render
	bool showMesh = false;		// overlay the mesh wireframe

	int selectedCell = -1;		// cell pinned by a left click (-1 = none)

	// tracks results.currentItem so the field tab bar can re-select the matching
	// tab when the field is changed from elsewhere (e.g. the Results panel combo)
	int lastFieldItem = -1;

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

	// true when structured face coordinates are available for drawing cells
	bool hasStructuredGrid() const;

	// structured results keep solid cells in the arrays, but those cells are not solved
	bool isStructuredCellActive(int cellID) const;

	// true when a field value can be shown for this cell
	bool isDrawableCell(int cellID, int fieldSize) const;

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

	// tab strip for switching the displayed field in place; each tab maps to an
	// entry in results.fieldType and drives results.currentItem
	void drawFieldTabs();

	void drawField(ImDrawList* drawList);
	void drawMeshOverlay(ImDrawList* drawList);
	void drawValueProbe(ImDrawList* drawList);

	// build the console line for the picked cell's current field value
	std::string buildCellInfoText(int cellID) const;

	// print the picked cell's current field value to the console
	void logCellInfoToConsole();

	// highlight the pinned cell
	void drawCellInfo(ImDrawList* drawList);

	void drawEmptyMessage(ImDrawList* drawList);

};
