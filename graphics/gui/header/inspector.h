#pragma once
#include "pch.h"

#include <array>
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

	void render();

	// Drawn by GUI into the app-wide toolbar strip above the dockspace, not by
	// render(), so the band can span the whole window instead of this panel.
	void drawToolBar();

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
	bool mirrorResult = false;	// reflect the axisymmetric result across r = 0

	int selectedCell = -1;		// cell pinned by a left click (-1 = none)
	bool selectedMirrored = false; // selectedCell was picked on the reflected half

	// tracks results.currentItem so the field tab bar can re-select the matching
	// tab when the field is changed from elsewhere (e.g. the Results panel combo)
	int lastFieldItem = -1;

	// scratch buffers reused across frames for smooth (vertex) shading
	std::vector<float> vertexValues;
	std::vector<int> vertexCounts;

	// Real multiblock cell quads (world r-z corners), in block/cellGlobal order --
	// the same cells the Mesh Inspector draws. For a multiblock mesh the results view
	// colors these directly by the exact per-cell solver value, instead of the
	// resampled uniform raster. Rebuilt from the mesh in generate().
	std::vector<std::array<Vec2, 4>> blockQuads;

	// Shared-vertex topology for interpolated (smooth) shading of the block quads:
	// the 4 unique vertex ids of each quad (corners shared between adjacent cells --
	// and across block seams -- collapse to one id), plus the total vertex count.
	// Built alongside blockQuads; lets smooth shading average cell values onto shared
	// corners exactly like the raster path, with no seam discontinuities.
	std::vector<std::array<int, 4>> blockQuadVertexIds;
	int blockVertexCount = 0;

	// ----------helpers-----------

	// true when the current mesh is multiblock and its cell quads are cached, so the
	// results view should draw the true conformal blocks rather than the raster grid
	bool hasMultiBlockCells() const;

	// (re)build blockQuads from the current multiblock mesh (no-op otherwise)
	void rebuildMultiBlockCells();

	// resolve the raw per-cell solution for the currently selected field
	const SolutionField* getCurrentSolution() const;

	// compute the value range of a solution field
	// how many cells drawField will iterate, so the range pass can match it
	int drawableCellCount() const;

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

	// center + zoom the camera on a z/r bounding box (the shared tail of frameToMesh's
	// multiblock / structured / unstructured branches)
	void frameToBounds(double zMin, double zMax, double rMin, double rMax);

	// pick the triangle/cell under a world-space point (-1 if none)
	int pickCell(const Vec2& world) const;

	// Map original axisymmetric geometry to the requested display half. The solver
	// stores only r >= 0; the mirrored half is a view-only reflection across r = 0.
	ImVec2 resultWorldToScreen(Vec2 world, bool mirrored) const;

	// Radial vector components and radial derivatives are odd across the symmetry
	// axis. Scalar/axial fields are even. These helpers keep the reflected colors,
	// probes, and pinned values physically consistent with the displayed field.
	bool currentFieldIsOddAcrossAxis() const;
	double displayedFieldValue(double value, bool mirrored) const;

	// ----------input-----------
	void handleMouse(ImGuiIO& io);

	// pin/unpin a cell on left click (separate from hover probe)
	void handleSelection(ImGuiIO& io);

	// ----------draw calls-----------

	// tab strip for switching the displayed field in place; each tab maps to an
	// entry in results.fieldType and drives results.currentItem
	void drawFieldTabs();

	void drawField(ImDrawList* drawList, bool mirrored = false);
	void drawMeshOverlay(ImDrawList* drawList, bool mirrored = false);
	void drawValueProbe(ImDrawList* drawList);

	// build the console line for the picked cell's current field value
	std::string buildCellInfoText(int cellID) const;

	// print the picked cell's current field value to the console
	void logCellInfoToConsole();

	// highlight the pinned cell
	void drawCellInfo(ImDrawList* drawList);

	void drawEmptyMessage(ImDrawList* drawList);

};
