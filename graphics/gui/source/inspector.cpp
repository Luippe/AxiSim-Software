#include "inspector.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <unordered_map>

#include "scene_view.h"
#include "project.h"
#include "results.h"
#include "mesh.h"
#include "colormap.h"
#include "colorbar.h"
#include "console.h"
#include "app_struct.h"

#include "flag_manager.h"
#include "printer.h"
#include "unit_manager.h"
#include "math_func.h"

using namespace UITabBarFlags;

Inspector::Inspector(Project& project, SceneView& scene, AppConfig& appConfig) :
		scene(scene),
		project(project),
		mesh(project.mesh),
		results(project.results),
		g(mesh.g),
		assets(appConfig.assets),
		colorbar(scene.colormap, project.results),
		BaseSurfaceViewer("graphics/shaders/inspector.vert", "graphics/shaders/inspector.frag") {

	frameBuffer.create2DBuffer(100, 100, GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE);
}

// ======================================================================
// -----------------------HELPER FUNCTIONS-------------------------------
// ======================================================================
void Inspector::generate() {
	// fit the view to the freshly generated mesh on the next render
	pendingFrame = true;

	// a freshly generated mesh re-indexes the cells, so drop any pinned cell
	selectedCell = -1;

	// cache the true multiblock cell quads so the field draws on the real blocks
	rebuildMultiBlockCells();
}

void Inspector::rebuildMultiBlockCells() {
	blockQuads.clear();
	blockQuadVertexIds.clear();
	blockVertexCount = 0;

	if (!mesh.isMultiBlock) {
		return;
	}

	// buildMultiBlockInspectMesh fills quads in block/cellGlobal order -- the same
	// order solutions[].field is stored in -- so quad c and field[c] are the same cell.
	FVMesh scratch;
	mesh.buildMultiBlockInspectMesh(scratch, blockQuads);

	if (blockQuads.empty()) {
		return;
	}

	// Assign each quad corner a shared vertex id for smooth shading. Block seam nodes
	// from neighbouring blocks are computed independently (possible float drift), so
	// dedup by a geometry-scale-quantized (z,r) key rather than exact equality.
	struct VKey {
		long long z, r;
		bool operator==(const VKey& o) const { return z == o.z && r == o.r; }
	};
	struct VKeyHash {
		size_t operator()(const VKey& k) const {
			return std::hash<long long>()(k.z * 73856093LL ^ (k.r * 19349663LL));
		}
	};

	const double scale = std::max(g.L, g.R);
	const double tol = (scale > 0.0 ? scale : 1.0) * 1.0e-6;
	const double inv = 1.0 / tol;

	auto keyOf = [&](const Vec2& p) {
		return VKey{
			(long long)std::llround(p.z * inv),
			(long long)std::llround(p.r * inv)
		};
	};

	std::unordered_map<VKey, int, VKeyHash> vertexLookup;
	vertexLookup.reserve(blockQuads.size() * 4);

	blockQuadVertexIds.resize(blockQuads.size());

	for (size_t c = 0; c < blockQuads.size(); c++) {
		for (int k = 0; k < 4; k++) {
			VKey key = keyOf(blockQuads[c][k]);
			auto it = vertexLookup.find(key);
			int id;
			if (it == vertexLookup.end()) {
				id = (int)vertexLookup.size();
				vertexLookup.emplace(key, id);
			}
			else {
				id = it->second;
			}
			blockQuadVertexIds[c][k] = id;
		}
	}

	blockVertexCount = (int)vertexLookup.size();
}

bool Inspector::hasMultiBlockCells() const {
	return mesh.isMultiBlock && !blockQuads.empty();
}

const SolutionField* Inspector::getCurrentSolution() const {

	if (results.fieldType.empty()) {
		return nullptr;
	}

	int idx = std::clamp(results.currentItem, 0, (int)results.fieldType.size() - 1);
	const std::string& name = results.fieldType[idx];

	auto it = results.solutions.find(name);
	if (it == results.solutions.end()) {
		return nullptr;
	}

	return &it->second;
}

bool Inspector::computeFieldRange(const SolutionField& sol, float& vmin, float& vmax) const {

	double lo = std::numeric_limits<double>::max();
	double hi = std::numeric_limits<double>::lowest();
	bool found = false;

	for (int c = 0; c < (int)sol.field.size(); c++) {
		if (!isDrawableCell(c, (int)sol.field.size())) {
			continue;
		}

		double v = sol.field[c];
		if (!std::isfinite(v)) {
			continue;
		}

		lo = std::min(lo, v);
		hi = std::max(hi, v);
		found = true;
	}

	if (!found) {
		return false;
	}

	// avoid a zero-width range so the colormap still resolves
	if (hi - lo < 1.0e-30) {
		hi = lo + 1.0e-30;
	}

	vmin = (float)lo;
	vmax = (float)hi;

	return true;
}

ImU32 Inspector::valueToColor(double value, double vmin, double vmax) const {

	const unsigned char(*lut)[3] = scene.colormap.currentLUT;

	if (!lut) {
		return IM_COL32(180, 180, 180, 255);
	}

	double t = (vmax > vmin) ? (value - vmin) / (vmax - vmin) : 0.5;
	t = std::clamp(t, 0.0, 1.0);

	int idx = (int)std::lround(t * 255.0);
	idx = std::clamp(idx, 0, 255);

	return IM_COL32(lut[idx][0], lut[idx][1], lut[idx][2], 255);
}

bool Inspector::hasStructuredGrid() const {

	if (mesh.currentMeshType != MeshType::Structured) {
		return false;
	}

	return g.nr > 0 &&
		g.nz > 0 &&
		(int)g.rFace.size() >= g.nr + 1 &&
		(int)g.zFace.size() >= g.nz + 1;
}

bool Inspector::isStructuredCellActive(int cellID) const {

	if (cellID < 0) {
		return false;
	}

	// Structured results are presented on the raster grid, so cellID is a raster
	// index (i*nz+j) and g.activeCell is its fluid/solid mask. The solver's fvMesh is
	// in multiblock cell order (fvMesh.nr/nz == 0), so it must NOT be indexed by a
	// raster id here -- doing so mismatched every cell for a multiblock mesh.
	if (cellID < (int)g.activeCell.size()) {
		return g.activeCell[cellID] != 0;
	}

	return true;
}

bool Inspector::isDrawableCell(int cellID, int fieldSize) const {

	if (cellID < 0 || cellID >= fieldSize) {
		return false;
	}

	// Multiblock: field is block/cellGlobal ordered and every block cell is fluid
	// (obstacles are absent blocks), so a valid quad index is drawable. Must be tested
	// before the structured branch, which would (wrongly) index the raster mask.
	if (hasMultiBlockCells()) {
		return cellID < (int)blockQuads.size();
	}

	if (mesh.currentMeshType == MeshType::Structured) {
		return isStructuredCellActive(cellID);
	}

	const FVMesh& fv = project.solver.fvMesh;
	if (cellID < (int)fv.cells.size()) {
		return fv.cells[cellID].active && !fv.cells[cellID].solid;
	}

	return true;
}

void Inspector::frameToBounds(double zMin, double zMax, double rMin, double rMax) {

	camera.center = Vec2{ 0.5 * (zMin + zMax), 0.5 * (rMin + rMax) };

	double w = zMax - zMin;
	double h = rMax - rMin;

	double uppZ = (w > 1.0e-12) ? w / (double)canvasRect.size.x : camera.unitsPerPixel;
	double uppR = (h > 1.0e-12) ? h / (double)canvasRect.size.y : camera.unitsPerPixel;

	double upp = std::max(uppZ, uppR);
	if (upp <= 1.0e-30) {
		upp = 0.001;
	}

	// add a small margin so the mesh does not touch the canvas edges
	camera.unitsPerPixel = upp * 1.15;
}

void Inspector::frameToMesh() {

	if (canvasRect.size.x <= 1.0f || canvasRect.size.y <= 1.0f) {
		return;
	}

	// Multiblock: frame to the real block extent (tight even for L-shapes / notches),
	// not the [0,L]x[0,R] raster bounding box.
	if (hasMultiBlockCells()) {
		double zMin = std::numeric_limits<double>::max();
		double zMax = std::numeric_limits<double>::lowest();
		double rMin = std::numeric_limits<double>::max();
		double rMax = std::numeric_limits<double>::lowest();

		for (const std::array<Vec2, 4>& q : blockQuads) {
			for (int k = 0; k < 4; k++) {
				zMin = std::min(zMin, q[k].z);
				zMax = std::max(zMax, q[k].z);
				rMin = std::min(rMin, q[k].r);
				rMax = std::max(rMax, q[k].r);
			}
		}

		frameToBounds(zMin, zMax, rMin, rMax);
		return;
	}

	if (hasStructuredGrid()) {
		double zMin = std::numeric_limits<double>::max();
		double zMax = std::numeric_limits<double>::lowest();
		double rMin = std::numeric_limits<double>::max();
		double rMax = std::numeric_limits<double>::lowest();
		bool found = false;

		for (int i = 0; i < g.nr; i++) {
			for (int j = 0; j < g.nz; j++) {
				int n = i * g.nz + j;
				if (!isStructuredCellActive(n)) {
					continue;
				}

				zMin = std::min(zMin, g.zFace[j]);
				zMax = std::max(zMax, g.zFace[j + 1]);
				rMin = std::min(rMin, g.rFace[i]);
				rMax = std::max(rMax, g.rFace[i + 1]);
				found = true;
			}
		}

		if (!found) {
			zMin = g.zFace.front();
			zMax = g.zFace.back();
			rMin = g.rFace.front();
			rMax = g.rFace.back();
		}

		frameToBounds(zMin, zMax, rMin, rMax);
		return;
	}

	const std::vector<Vec2>& pts = mesh.unstructuredPoints;

	if (pts.empty()) {
		return;
	}

	double zMin = std::numeric_limits<double>::max();
	double zMax = std::numeric_limits<double>::lowest();
	double rMin = std::numeric_limits<double>::max();
	double rMax = std::numeric_limits<double>::lowest();

	for (const Vec2& p : pts) {
		zMin = std::min(zMin, p.z);
		zMax = std::max(zMax, p.z);
		rMin = std::min(rMin, p.r);
		rMax = std::max(rMax, p.r);
	}

	frameToBounds(zMin, zMax, rMin, rMax);
}

static int inspectorCellIndexAt(const std::vector<double>& faces, double x) {
	if (faces.size() < 2) {
		return -1;
	}

	if (x < faces.front() || x > faces.back()) {
		return -1;
	}

	auto it = std::upper_bound(faces.begin(), faces.end(), x);
	int index = static_cast<int>(it - faces.begin()) - 1;

	return std::clamp(index, 0, static_cast<int>(faces.size()) - 2);
}

int Inspector::pickCell(const Vec2& world) const {

	// Multiblock: point-in-quad against the real block cells (block/cellGlobal order),
	// so a picked index maps straight into solutions[].field.
	if (hasMultiBlockCells()) {
		for (int c = 0; c < (int)blockQuads.size(); c++) {
			if (pointInQuad(world, blockQuads[c])) {
				return c;
			}
		}
		return -1;
	}

	if (hasStructuredGrid()) {
		int j = inspectorCellIndexAt(g.zFace, world.z);
		int i = inspectorCellIndexAt(g.rFace, world.r);

		if (i < 0 || j < 0) {
			return -1;
		}

		int n = i * g.nz + j;
		return isStructuredCellActive(n) ? n : -1;
	}

	const std::vector<Vec2>& pts = mesh.unstructuredPoints;
	const std::vector<Triangle>& tris = mesh.unstructuredTriangles;

	for (int c = 0; c < (int)tris.size(); c++) {
		const Triangle& t = tris[c];

		if (t.v0 < 0 || t.v1 < 0 || t.v2 < 0) continue;
		if (t.v0 >= (int)pts.size() ||
			t.v1 >= (int)pts.size() ||
			t.v2 >= (int)pts.size()) {
			continue;
		}

		double d1 = pickSign(world, pts[t.v0], pts[t.v1]);
		double d2 = pickSign(world, pts[t.v1], pts[t.v2]);
		double d3 = pickSign(world, pts[t.v2], pts[t.v0]);

		bool hasNeg = (d1 < 0.0) || (d2 < 0.0) || (d3 < 0.0);
		bool hasPos = (d1 > 0.0) || (d2 > 0.0) || (d3 > 0.0);

		if (!(hasNeg && hasPos)) {
			return c;
		}
	}

	return -1;
}

// ======================================================================
// -----------------------MOUSE HANDLES----------------------------------
// ======================================================================
void Inspector::handleMouse(ImGuiIO& io) {

	// if mouse is not near the image, then dont handle any mouse events
	if (!isMouseNearImage(io)) return;

	// handle zooming and panning
	if (io.MouseWheel != 0.0f) {
		camera.calculateZoom(io.MouseWheel, currentMousePos);
	}

	if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
		camera.calculatePan(io.MouseDelta.x, io.MouseDelta.y);
	}
}

void Inspector::handleSelection(ImGuiIO& io) {


	if (!isMouseNearImage(io)) return;

	// only treat a left release as a click (pin a cell) when the mouse hasn't
	// been dragged - dragging is a pan and must not move the selection.
	if (!ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
		return;
	}

	ImVec2 drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
	if (drag.x * drag.x + drag.y * drag.y > 9.0f) {
		return;
	}

	Vec2 world = camera.screenToWorld(ImGui::GetMousePos());
	selectedCell = pickCell(world); // -1 if the click missed the mesh (deselect)

	if (selectedCell >= 0) {
		logCellInfoToConsole();
	}
}

// ======================================================================
// -----------------------DRAW CALLS-------------------------------------
// ======================================================================
void Inspector::drawToolBar() {
	// icon-only, CFD-style toolbar: view | display, screenshot on the far right.
	// names are hidden on the buttons and shown via tooltip.
	const ImVec2 iconSize(toolbarIconSize, toolbarIconSize);

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 2.0f));

	const float rowH = iconSize.y + ImGui::GetStyle().FramePadding.y * 2.0f;
	const float toolbarHeight = rowH + ImGui::GetStyle().WindowPadding.y * 2.0f;

	ImGui::BeginChild("##toolbar", ImVec2(0.0f, toolbarHeight), false,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	// --- view ---
	if (addImageButton("Reset", "", "Reset view", assets.icon("house"), iconSize)) {
		camera.initPosition();
		pendingFrame = true;
	}

	addToolbarSeparator(rowH - 8.0f);

	// --- display ---
	addImageButtonToggle("ToggleMesh", "", "Toggle mesh overlay", assets.icon("grid"), iconSize, showMesh);

	// --- screenshot (pushed to the far right) ---
	const float copyWidth = iconSize.x + ImGui::GetStyle().FramePadding.x * 2.0f;
	ImGui::SameLine(ImGui::GetContentRegionMax().x - copyWidth);
	if (addImageButton("Copy", "", "Copy to clipboard", assets.icon("clipboard"), iconSize) || consoleCopy) {
		pendingCopyWidth = frameBuffer.width;
		pendingCopyHeight = frameBuffer.height;
		pendingCopy = true;
		consoleCopy = false;
	}

	ImGui::EndChild();
	ImGui::PopStyleVar(2);
}

void Inspector::drawFieldTabs() {

	std::vector<std::string>& shown = results.shownFields;
	if (shown.empty()) {
		return;		// nothing selected to show; the canvas draws its empty message
	}

	// if the field was changed elsewhere (e.g. dropping a field into the Graphics
	// picker, or the Results combo) since last frame, force the matching tab active
	// so the strip stays in sync. Only on the change frame - forcing it every frame
	// would override the user's own tab clicks.
	bool syncSelection = (results.currentItem != lastFieldItem);

	int tabToClose = -1;

	if (ImGui::BeginTabBar("##FieldTabs", InspectorTabBarFlags)) {

		for (int i = 0; i < (int)shown.size(); i++) {

			// map this shown field back to its index in fieldType (currentItem space)
			int fieldIdx = results.indexOfField(shown[i]);
			if (fieldIdx < 0) {
				continue;	// field no longer exists (results regenerated in another mode)
			}

			ImGuiTabItemFlags itemFlags = ImGuiTabItemFlags_None;
			if (syncSelection && fieldIdx == results.currentItem) {
				itemFlags |= ImGuiTabItemFlags_SetSelected;
			}

			// BeginTabItem returns true only for the active tab, so this branch is
			// where a click on a tab switches the displayed field. The close button
			// (&open) removes the field from the shown set.
			bool open = true;
			if (ImGui::BeginTabItem(shown[i].c_str(), &open, itemFlags)) {
				if (results.currentItem != fieldIdx) {
					results.currentItem = fieldIdx;
					results.updateCurrentField();
				}
				ImGui::EndTabItem();
			}

			if (!open) {
				tabToClose = i;
			}
		}

		ImGui::EndTabBar();
	}

	if (tabToClose >= 0) {
		// copy the name first: removeShownField erases from the same vector
		std::string name = shown[tabToClose];
		results.removeShownField(name);
	}

	lastFieldItem = results.currentItem;
}

void Inspector::drawField(ImDrawList* drawList) {

	const SolutionField* sol = getCurrentSolution();
	if (!sol) {
		return;
	}

	const std::vector<double>& field = sol->field;

	if (field.empty()) {
		return;
	}

	float vmin = 0.0f;
	float vmax = 0.0f;
	if (!computeFieldRange(*sol, vmin, vmax)) {
		return;
	}

	// keep the colorbar in sync with what is on screen
	if (results.currentField) {
		results.currentField->vmin = vmin;
		results.currentField->vmax = vmax;
	}

	bool smooth = (results.currentShadingType == ShadingType::Interp);

	const ImVec2 uv = drawList->_Data->TexUvWhitePixel;

	// Multiblock: color each real block cell by its exact solver value (block order).
	// Flat = one color per cell; Interp = average cell values onto shared corners
	// (blockQuadVertexIds) and gouraud-fill each quad, matching the raster path.
	if (hasMultiBlockCells()) {

		// hasMultiBlockCells() guarantees blockQuadVertexIds is sized to blockQuads and
		// blockVertexCount >= 1, so the shading mode alone decides smooth vs flat.
		bool mbSmooth = smooth;

		if (mbSmooth) {
			vertexValues.assign(blockVertexCount, 0.0f);
			vertexCounts.assign(blockVertexCount, 0);

			for (int c = 0; c < (int)blockQuads.size(); c++) {
				if (!isDrawableCell(c, (int)field.size())) {
					continue;
				}

				double raw = field[c];
				if (!std::isfinite(raw)) {
					continue;
				}

				float v = (float)raw;
				for (int k = 0; k < 4; k++) {
					int id = blockQuadVertexIds[c][k];
					if (id < 0 || id >= blockVertexCount) continue;
					vertexValues[id] += v;
					vertexCounts[id] += 1;
				}
			}

			for (size_t i = 0; i < vertexValues.size(); i++) {
				if (vertexCounts[i] > 0) {
					vertexValues[i] /= (float)vertexCounts[i];
				}
			}
		}

		for (int c = 0; c < (int)blockQuads.size(); c++) {
			if (!isDrawableCell(c, (int)field.size())) {
				continue;
			}

			double v = field[c];
			if (!std::isfinite(v)) {
				continue;
			}

			const std::array<Vec2, 4>& q = blockQuads[c];
			ImVec2 p0 = camera.worldToScreen(q[0]);
			ImVec2 p1 = camera.worldToScreen(q[1]);
			ImVec2 p2 = camera.worldToScreen(q[2]);
			ImVec2 p3 = camera.worldToScreen(q[3]);

			if (mbSmooth) {
				float fallback = (float)v;
				auto cornerValue = [&](int k) {
					int id = blockQuadVertexIds[c][k];
					if (id >= 0 && id < blockVertexCount && vertexCounts[id] > 0) {
						return vertexValues[id];
					}
					return fallback;
				};

				ImU32 c0 = valueToColor(cornerValue(0), vmin, vmax);
				ImU32 c1 = valueToColor(cornerValue(1), vmin, vmax);
				ImU32 c2 = valueToColor(cornerValue(2), vmin, vmax);
				ImU32 c3 = valueToColor(cornerValue(3), vmin, vmax);

				// two triangles (p0,p1,p2) + (p0,p2,p3) over the convex quad
				drawList->PrimReserve(6, 6);
				drawList->PrimVtx(p0, uv, c0);
				drawList->PrimVtx(p1, uv, c1);
				drawList->PrimVtx(p2, uv, c2);
				drawList->PrimVtx(p0, uv, c0);
				drawList->PrimVtx(p2, uv, c2);
				drawList->PrimVtx(p3, uv, c3);
			}
			else {
				ImU32 col = valueToColor(v, vmin, vmax);
				drawList->AddQuadFilled(p0, p1, p2, p3, col);
			}
		}

		return;
	}

	if (hasStructuredGrid()) {
		const int nr = g.nr;
		const int nz = g.nz;

		auto vertexIndex = [nz](int iFace, int jFace) {
			return iFace * (nz + 1) + jFace;
		};

		if (smooth) {
			int nVertices = (nr + 1) * (nz + 1);
			vertexValues.assign(nVertices, 0.0f);
			vertexCounts.assign(nVertices, 0);

			for (int i = 0; i < nr; i++) {
				for (int j = 0; j < nz; j++) {
					int c = i * nz + j;
					if (!isDrawableCell(c, (int)field.size())) {
						continue;
					}

					double raw = field[c];
					if (!std::isfinite(raw)) {
						continue;
					}

					float v = (float)raw;
					int ids[4] = {
						vertexIndex(i, j),
						vertexIndex(i, j + 1),
						vertexIndex(i + 1, j + 1),
						vertexIndex(i + 1, j)
					};

					for (int id : ids) {
						if (id < 0 || id >= nVertices) continue;
						vertexValues[id] += v;
						vertexCounts[id] += 1;
					}
				}
			}

			for (size_t i = 0; i < vertexValues.size(); i++) {
				if (vertexCounts[i] > 0) {
					vertexValues[i] /= (float)vertexCounts[i];
				}
			}
		}

		for (int i = 0; i < nr; i++) {
			for (int j = 0; j < nz; j++) {
				int c = i * nz + j;
				if (!isDrawableCell(c, (int)field.size())) {
					continue;
				}

				double v = field[c];
				if (!std::isfinite(v)) {
					continue;
				}

				ImVec2 p00 = camera.worldToScreen(Vec2{ g.zFace[j], g.rFace[i] });
				ImVec2 p10 = camera.worldToScreen(Vec2{ g.zFace[j + 1], g.rFace[i] });
				ImVec2 p11 = camera.worldToScreen(Vec2{ g.zFace[j + 1], g.rFace[i + 1] });
				ImVec2 p01 = camera.worldToScreen(Vec2{ g.zFace[j], g.rFace[i + 1] });

				if (smooth) {
					float fallback = (float)v;
					auto cornerValue = [&](int id) {
						if (id >= 0 &&
							id < (int)vertexValues.size() &&
							vertexCounts[id] > 0) {
							return vertexValues[id];
						}
						return fallback;
					};

					int i00 = vertexIndex(i, j);
					int i10 = vertexIndex(i, j + 1);
					int i11 = vertexIndex(i + 1, j + 1);
					int i01 = vertexIndex(i + 1, j);

					ImU32 c00 = valueToColor(cornerValue(i00), vmin, vmax);
					ImU32 c10 = valueToColor(cornerValue(i10), vmin, vmax);
					ImU32 c11 = valueToColor(cornerValue(i11), vmin, vmax);
					ImU32 c01 = valueToColor(cornerValue(i01), vmin, vmax);

					drawList->PrimReserve(6, 6);
					drawList->PrimVtx(p00, uv, c00);
					drawList->PrimVtx(p10, uv, c10);
					drawList->PrimVtx(p11, uv, c11);
					drawList->PrimVtx(p00, uv, c00);
					drawList->PrimVtx(p11, uv, c11);
					drawList->PrimVtx(p01, uv, c01);
				}
				else {
					ImU32 col = valueToColor(v, vmin, vmax);
					drawList->AddQuadFilled(p00, p10, p11, p01, col);
				}
			}
		}

		return;
	}

	const std::vector<Vec2>& pts = mesh.unstructuredPoints;
	const std::vector<Triangle>& tris = mesh.unstructuredTriangles;

	if (pts.empty() || tris.empty()) {
		return;
	}

	// for smooth shading, average the surrounding cell values onto each vertex
	if (smooth) {
		vertexValues.assign(pts.size(), 0.0f);
		vertexCounts.assign(pts.size(), 0);

		int nCells = std::min((int)tris.size(), (int)field.size());

		for (int c = 0; c < nCells; c++) {
			if (!isDrawableCell(c, (int)field.size())) {
				continue;
			}

			const Triangle& t = tris[c];
			double raw = field[c];
			if (!std::isfinite(raw)) {
				continue;
			}

			float v = (float)raw;

			int ids[3] = { t.v0, t.v1, t.v2 };
			for (int k = 0; k < 3; k++) {
				int vid = ids[k];
				if (vid < 0 || vid >= (int)pts.size()) continue;
				vertexValues[vid] += v;
				vertexCounts[vid] += 1;
			}
		}

		for (size_t i = 0; i < vertexValues.size(); i++) {
			if (vertexCounts[i] > 0) {
				vertexValues[i] /= (float)vertexCounts[i];
			}
		}
	}



	for (int c = 0; c < (int)tris.size(); c++) {
		if (!isDrawableCell(c, (int)field.size())) {
			continue;
		}
		if (!std::isfinite(field[c])) {
			continue;
		}

		const Triangle& t = tris[c];

		if (t.v0 < 0 || t.v1 < 0 || t.v2 < 0) continue;
		if (t.v0 >= (int)pts.size() ||
			t.v1 >= (int)pts.size() ||
			t.v2 >= (int)pts.size()) {
			continue;
		}

		ImVec2 a = camera.worldToScreen(pts[t.v0]);
		ImVec2 b = camera.worldToScreen(pts[t.v1]);
		ImVec2 d = camera.worldToScreen(pts[t.v2]);

		if (smooth) {
			ImU32 ca = valueToColor(vertexValues[t.v0], vmin, vmax);
			ImU32 cb = valueToColor(vertexValues[t.v1], vmin, vmax);
			ImU32 cd = valueToColor(vertexValues[t.v2], vmin, vmax);

			drawList->PrimReserve(3, 3);
			drawList->PrimVtx(a, uv, ca);
			drawList->PrimVtx(b, uv, cb);
			drawList->PrimVtx(d, uv, cd);
		}
		else {
			double v = field[c];
			ImU32 col = valueToColor(v, vmin, vmax);
			drawList->AddTriangleFilled(a, b, d, col);
		}
	}

}

void Inspector::drawMeshOverlay(ImDrawList* drawList) {

	if (!showMesh) {
		return;
	}

	// Multiblock: outline the real block cells, matching the Mesh Inspector.
	if (hasMultiBlockCells()) {
		const ImU32 lineColor = IM_COL32(25, 35, 45, 140);

		for (const std::array<Vec2, 4>& q : blockQuads) {
			ImVec2 p0 = camera.worldToScreen(q[0]);
			ImVec2 p1 = camera.worldToScreen(q[1]);
			ImVec2 p2 = camera.worldToScreen(q[2]);
			ImVec2 p3 = camera.worldToScreen(q[3]);
			drawList->AddQuad(p0, p1, p2, p3, lineColor, 1.0f);
		}

		return;
	}

	if (hasStructuredGrid()) {
		const ImU32 lineColor = IM_COL32(25, 35, 45, 140);
		const int nr = g.nr;
		const int nz = g.nz;

		auto activeAt = [&](int i, int j) {
			if (i < 0 || i >= nr || j < 0 || j >= nz) {
				return false;
			}
			return isStructuredCellActive(i * nz + j);
		};

		for (int i = 0; i < nr; i++) {
			for (int jFace = 0; jFace <= nz; jFace++) {
				if (!activeAt(i, jFace - 1) && !activeAt(i, jFace)) {
					continue;
				}

				ImVec2 a = camera.worldToScreen(Vec2{ g.zFace[jFace], g.rFace[i] });
				ImVec2 b = camera.worldToScreen(Vec2{ g.zFace[jFace], g.rFace[i + 1] });
				drawList->AddLine(a, b, lineColor, 1.0f);
			}
		}

		for (int iFace = 0; iFace <= nr; iFace++) {
			for (int j = 0; j < nz; j++) {
				if (!activeAt(iFace - 1, j) && !activeAt(iFace, j)) {
					continue;
				}

				ImVec2 a = camera.worldToScreen(Vec2{ g.zFace[j], g.rFace[iFace] });
				ImVec2 b = camera.worldToScreen(Vec2{ g.zFace[j + 1], g.rFace[iFace] });
				drawList->AddLine(a, b, lineColor, 1.0f);
			}
		}

		return;
	}

	const std::vector<Vec2>& pts = mesh.unstructuredPoints;
	const std::vector<Triangle>& tris = mesh.unstructuredTriangles;

	if (pts.empty() || tris.empty()) {
		return;
	}

	const ImU32 lineColor = IM_COL32(25, 35, 45, 140);

	for (const Triangle& t : tris) {
		if (t.v0 < 0 || t.v1 < 0 || t.v2 < 0) continue;
		if (t.v0 >= (int)pts.size() ||
			t.v1 >= (int)pts.size() ||
			t.v2 >= (int)pts.size()) {
			continue;
		}

		ImVec2 a = camera.worldToScreen(pts[t.v0]);
		ImVec2 b = camera.worldToScreen(pts[t.v1]);
		ImVec2 d = camera.worldToScreen(pts[t.v2]);

		drawList->AddTriangle(a, b, d, lineColor, 1.0f);
	}
}

void Inspector::drawValueProbe(ImDrawList* drawList) {

	ImGuiIO& io = ImGui::GetIO();

	if (!isMouseNearImage(io)) {
		return;
	}

	if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) ||
		ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
		return;
	}

	const SolutionField* sol = getCurrentSolution();
	if (!sol) {
		return;
	}

	Vec2 world = camera.screenToWorld(currentMousePos);

	int c = pickCell(world);
	if (c < 0 || c >= (int)sol->field.size()) {
		return;
	}
	if (!isDrawableCell(c, (int)sol->field.size()) ||
		!std::isfinite(sol->field[c])) {
		return;
	}

	ImVec2 m = currentMousePos;
	drawList->AddCircleFilled(m, 3.0f, IM_COL32(255, 255, 255, 220), 12);
	drawList->AddCircle(m, 4.0f, IM_COL32(20, 20, 20, 220), 12, 1.5f);

	ImGui::SetTooltip(
		"z: %.4g\nr: %.4g\nvalue: %.5g",
		world.z,
		world.r,
		sol->field[c]
	);
}

static const char* shortFieldName(const std::string& name) {
	if (name == "Axial Velocity")  return "U  (axial)";
	if (name == "Radial Velocity") return "V  (radial)";
	if (name == "Pressure")        return "P";
	if (name == "Temperature")     return "T";
	if (name == "Concentration")   return "C";
	return name.c_str();
}

std::string Inspector::buildCellInfoText(int cellID) const {
	if (cellID < 0 || results.fieldType.empty()) {
		return {};
	}

	int idx = std::clamp(results.currentItem, 0, (int)results.fieldType.size() - 1);
	const std::string& name = results.fieldType[idx];

	auto it = results.solutions.find(name);
	if (it == results.solutions.end() || cellID >= (int)it->second.field.size()) {
		return {};
	}

	char line[160];
	std::snprintf(line, sizeof(line), "Cell #%d %s: %.6g",
		cellID, shortFieldName(name), it->second.field[cellID]);
	return line;
}

void Inspector::logCellInfoToConsole() {
	if (!console) {
		return;
	}

	std::string info = buildCellInfoText(selectedCell);
	if (info.empty()) {
		return;
	}

	console->addLine(info);
}

void Inspector::drawCellInfo(ImDrawList* drawList) {

	ImVec2 canvasMin = canvasRect.min;
	ImVec2 canvasMax = canvasRect.max;


	if (selectedCell < 0) {
		return;
	}

	drawList->PushClipRect(canvasMin, canvasMax, true);

	// Multiblock: highlight the picked block cell quad (block/cellGlobal order).
	if (hasMultiBlockCells()) {
		if (selectedCell >= (int)blockQuads.size()) {
			selectedCell = -1;
			drawList->PopClipRect();
			return;
		}

		const std::array<Vec2, 4>& q = blockQuads[selectedCell];
		ImVec2 pts[4];
		for (int k = 0; k < 4; k++) {
			pts[k] = camera.worldToScreen(q[k]);
		}

		drawList->AddConvexPolyFilled(pts, 4, IM_COL32(255, 235, 60, 70));
		drawList->AddPolyline(pts, 4, IM_COL32(255, 235, 60, 255), ImDrawFlags_Closed, 2.0f);

		drawList->PopClipRect();
		return;
	}

	if (hasStructuredGrid()) {
		int nCells = g.nr * g.nz;
		if (selectedCell >= nCells || !isStructuredCellActive(selectedCell)) {
			selectedCell = -1;
			drawList->PopClipRect();
			return;
		}

		int i = selectedCell / g.nz;
		int j = selectedCell % g.nz;

		ImVec2 p00 = camera.worldToScreen(Vec2{ g.zFace[j], g.rFace[i] });
		ImVec2 p11 = camera.worldToScreen(Vec2{ g.zFace[j + 1], g.rFace[i + 1] });

		ImVec2 rmin(std::min(p00.x, p11.x), std::min(p00.y, p11.y));
		ImVec2 rmax(std::max(p00.x, p11.x), std::max(p00.y, p11.y));

		drawList->AddRectFilled(rmin, rmax, IM_COL32(255, 235, 60, 70));
		drawList->AddRect(rmin, rmax, IM_COL32(255, 235, 60, 255), 0.0f, 0, 2.0f);
	}
	else {
		const std::vector<Vec2>& pts = mesh.unstructuredPoints;
		const std::vector<Triangle>& tris = mesh.unstructuredTriangles;

		if (selectedCell >= (int)tris.size()) {
			selectedCell = -1;
			drawList->PopClipRect();
			return;
		}

		const Triangle& t = tris[selectedCell];
		if (t.v0 < 0 || t.v1 < 0 || t.v2 < 0 ||
			t.v0 >= (int)pts.size() ||
			t.v1 >= (int)pts.size() ||
			t.v2 >= (int)pts.size()) {
			drawList->PopClipRect();
			return;
		}

		ImVec2 a = camera.worldToScreen(pts[t.v0]);
		ImVec2 b = camera.worldToScreen(pts[t.v1]);
		ImVec2 d = camera.worldToScreen(pts[t.v2]);

		drawList->AddTriangleFilled(a, b, d, IM_COL32(255, 235, 60, 70));
		drawList->AddTriangle(a, b, d, IM_COL32(255, 235, 60, 255), 2.0f);
	}

	drawList->PopClipRect();
}

void Inspector::drawEmptyMessage(ImDrawList* drawList) {

	const SolutionField* sol = getCurrentSolution();
	if (sol && !sol->field.empty()) {
		if (hasStructuredGrid() || !mesh.unstructuredTriangles.empty()) {
			return;
		}
	}

	const char* msg = "No results to display";
	ImVec2 ts = ImGui::CalcTextSize(msg);

	ImVec2 center(
		0.5f * (canvasRect.min.x + canvasRect.max.x),
		0.5f * (canvasRect.min.y + canvasRect.max.y)
	);

	drawList->AddText(
		ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f),
		IM_COL32(200, 200, 200, 255),
		msg
	);
}

void Inspector::copyActiveSurfaceToClipboard() {

	GLint oldFBO, oldViewport[4];
	ImVec2 oldDisplaySize, oldFramebufferSize;
	offScreenFBO.create2DBuffer(pendingCopyWidth, pendingCopyHeight, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
	offScreenFBO.beginOffScreenImGuiRender(oldFBO, oldViewport, oldDisplaySize, oldFramebufferSize);

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::Begin("##ExportWindow", nullptr, UIFlags::TemporaryWindowFlags);

	ImVec2 exportSize((float)pendingCopyWidth, (float)pendingCopyHeight);
	ImGui::Image((ImTextureID)(intptr_t)frameBuffer.getTextureID(), exportSize, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
	canvasRect = makePaddedRect(ImGui::GetItemRectMin(), exportSize);

	camera.setDimensions(
		canvasRect.size.x,
		canvasRect.size.y,
		canvasRect.min
	);

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawCanvas(drawList, canvasRect, 5.0f);

	drawList->PushClipRect(canvasRect.min, canvasRect.max, true);
	drawField(drawList);
	drawMeshOverlay(drawList);
	drawAxes(drawList);
	drawCellInfo(drawList);
	drawValueProbe(drawList);
	drawEmptyMessage(drawList);

	// bake the colorbar into the exported image at the same relative position it
	// occupies in the live panel
	colorbar.draw(drawList, canvasRect.min, canvasRect.max);
	drawList->PopClipRect();

	ImGui::End();
	ImGui::PopStyleVar();

	offScreenFBO.endOffScreenImGuiRender(oldFBO, oldViewport, oldDisplaySize, oldFramebufferSize);
}

// ======================================================================
// -----------------------MAIN RENDER LOOP-------------------------------
// ======================================================================
void Inspector::render() {

	updateLengthScale(
		project.lengthScale.value,
		Units::lengthUnits[project.lengthScale.index].name
	);

	ImGui::SetNextWindowClass(&windowClass);
	ImGui::Begin("Inspector");

	ImDrawList* drawList = ImGui::GetWindowDrawList();


	drawToolBar();

	// field selector tabs - drawn before the canvas is measured so it sizes to the
	// space left below the strip
	drawFieldTabs();

	ImVec2 pos = ImGui::GetCursorScreenPos();
	ImVec2 size = ImGui::GetContentRegionAvail();

	// the colorbar now floats inside the canvas, so the canvas fills the whole panel
	canvasRect = makePaddedRect(pos, size);

	resizeImage();

	camera.setDimensions(
		(int)canvasRect.size.x,
		(int)canvasRect.size.y,
		canvasRect.min
	);

	// recenter/re-zoom to the loaded project's units if a reset was requested
	applyPendingResetView();

	if (pendingFrame && canvasRect.size.x > 1.0f && canvasRect.size.y > 1.0f) {
		frameToMesh();
		pendingFrame = false;
	}

	// update current global mouse pos
	updateCurrentMousePos();

	ImGuiIO io = ImGui::GetIO();

	// let the floating colorbar consume the pointer first, so dragging it doesn't
	// also pan/select the field underneath it
	bool overColorbar = colorbar.interact(canvasRect.min, canvasRect.max);

	if (!overColorbar) {
		handleMouse(io);
		handleSelection(io);
	}

	drawCanvas(drawList, canvasRect, 5.0f);

	drawList->PushClipRect(canvasRect.min, canvasRect.max, true);
	drawField(drawList);
	drawMeshOverlay(drawList);
	drawAxes(drawList);
	drawCellInfo(drawList);
	if (!overColorbar) {
		drawValueProbe(drawList);
	}
	drawEmptyMessage(drawList);

	// draw the colorbar on top of the field, inside the canvas, so it belongs to
	// the same image (and is included when copying to the clipboard)
	colorbar.draw(drawList, canvasRect.min, canvasRect.max);
	drawList->PopClipRect();

	ImGui::End();
}
