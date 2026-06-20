#include "inspector.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

#include "scene_view.h"
#include "project.h"
#include "results.h"
#include "mesh.h"
#include "colormap.h"
#include "colorbar.h"

#include "flag_manager.h"
#include "printer.h"

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

	for (double v : sol.field) {
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

void Inspector::frameToMesh() {

	const std::vector<Vec2>& pts = mesh.unstructuredPoints;

	if (pts.empty() || imageSize.x <= 1.0f || imageSize.y <= 1.0f) {
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

	camera.center = Vec2{ 0.5 * (zMin + zMax), 0.5 * (rMin + rMax) };

	double w = zMax - zMin;
	double h = rMax - rMin;

	double uppZ = (w > 1.0e-12) ? w / (double)imageSize.x : camera.unitsPerPixel;
	double uppR = (h > 1.0e-12) ? h / (double)imageSize.y : camera.unitsPerPixel;

	double upp = std::max(uppZ, uppR);

	if (upp <= 1.0e-30) {
		upp = 0.001;
	}

	// add a small margin so the mesh does not touch the canvas edges
	camera.unitsPerPixel = upp * 1.15;
}

static double pickSign(const Vec2& p, const Vec2& a, const Vec2& b) {
	return (p.z - b.z) * (a.r - b.r) - (a.z - b.z) * (p.r - b.r);
}

int Inspector::pickCell(const Vec2& world) const {

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
void Inspector::handleMouse() {

	ImGuiIO& io = ImGui::GetIO();

	if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f) {
		camera.calculateZoom(io.MouseWheel, ImGui::GetMousePos());
	}

	if (ImGui::IsItemActive() &&
		(ImGui::IsMouseDragging(ImGuiMouseButton_Left) ||
		 ImGui::IsMouseDragging(ImGuiMouseButton_Middle))) {
		camera.calculatePan(io.MouseDelta.x, io.MouseDelta.y);
	}
}

// ======================================================================
// -----------------------DRAW CALLS-------------------------------------
// ======================================================================
void Inspector::drawToolBar() {
	float toolbarHeight = 40.0f;

	ImGui::BeginChild("##toolbar", ImVec2(0.0f, toolbarHeight), false);

	if (addImageButton("Reset", "Reset View", assets.houseIcon, buttonSize)) {
		camera.home();
		pendingFrame = true;
	}
	ImGui::SameLine();

	if (addImageButton("Copy", "Copy to clipboard", assets.copyIcon, buttonSize) || consoleCopy) {
		pendingCopyWidth = std::max(imageWidth, 1);
		pendingCopyHeight = std::max(imageHeight, 1);
		pendingCopy = true;
		consoleCopy = false;
	}
	ImGui::SameLine();

	ImGui::Checkbox("Mesh", &showMesh);

	ImGui::EndChild();
}

void Inspector::drawField(ImDrawList* drawList) {

	const SolutionField* sol = getCurrentSolution();
	if (!sol) {
		return;
	}

	const std::vector<Vec2>& pts = mesh.unstructuredPoints;
	const std::vector<Triangle>& tris = mesh.unstructuredTriangles;
	const std::vector<double>& field = sol->field;

	if (pts.empty() || tris.empty() || field.empty()) {
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

	// for smooth shading, average the surrounding cell values onto each vertex
	if (smooth) {
		vertexValues.assign(pts.size(), 0.0f);
		vertexCounts.assign(pts.size(), 0);

		int nCells = std::min((int)tris.size(), (int)field.size());

		for (int c = 0; c < nCells; c++) {
			const Triangle& t = tris[c];
			float v = (float)field[c];

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

	drawList->PushClipRect(imageMin, imageMax, true);

	const ImVec2 uv = drawList->_Data->TexUvWhitePixel;

	for (int c = 0; c < (int)tris.size(); c++) {
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
			double v = (c < (int)field.size()) ? field[c] : 0.0;
			ImU32 col = valueToColor(v, vmin, vmax);
			drawList->AddTriangleFilled(a, b, d, col);
		}
	}

	drawList->PopClipRect();
}

void Inspector::drawMeshOverlay(ImDrawList* drawList) {

	if (!showMesh) {
		return;
	}

	const std::vector<Vec2>& pts = mesh.unstructuredPoints;
	const std::vector<Triangle>& tris = mesh.unstructuredTriangles;

	if (pts.empty() || tris.empty()) {
		return;
	}

	drawList->PushClipRect(imageMin, imageMax, true);

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

	drawList->PopClipRect();
}

void Inspector::drawAxes(ImDrawList* drawList) {

	ImVec2 origin = camera.worldToScreen(Vec2{ 0.0, 0.0 });

	drawList->PushClipRect(imageMin, imageMax, true);

	if (origin.y >= imageMin.y && origin.y <= imageMax.y) {
		drawList->AddLine(
			ImVec2(imageMin.x, origin.y),
			ImVec2(imageMax.x, origin.y),
			IM_COL32(210, 55, 55, 120),
			1.0f
		);
	}

	if (origin.x >= imageMin.x && origin.x <= imageMax.x) {
		drawList->AddLine(
			ImVec2(origin.x, imageMin.y),
			ImVec2(origin.x, imageMax.y),
			IM_COL32(55, 190, 95, 120),
			1.0f
		);
	}

	drawList->PopClipRect();
}

void Inspector::drawValueProbe(ImDrawList* drawList) {

	if (!ImGui::IsItemHovered()) {
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

	Vec2 world = camera.screenToWorld(ImGui::GetMousePos());

	int c = pickCell(world);
	if (c < 0 || c >= (int)sol->field.size()) {
		return;
	}

	ImVec2 m = ImGui::GetMousePos();
	drawList->AddCircleFilled(m, 3.0f, IM_COL32(255, 255, 255, 220), 12);
	drawList->AddCircle(m, 4.0f, IM_COL32(20, 20, 20, 220), 12, 1.5f);

	ImGui::SetTooltip(
		"z: %.4g\nr: %.4g\nvalue: %.5g",
		world.z,
		world.r,
		sol->field[c]
	);
}

void Inspector::drawEmptyMessage(ImDrawList* drawList) {

	if (!mesh.unstructuredTriangles.empty() && getCurrentSolution()) {
		return;
	}

	const char* msg = "No results to display";
	ImVec2 ts = ImGui::CalcTextSize(msg);

	ImVec2 center(
		0.5f * (imageMin.x + imageMax.x),
		0.5f * (imageMin.y + imageMax.y)
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

	ImGui::InvisibleButton("##ExportCanvas", exportSize);

	// temporarily retarget the camera/canvas to the off-screen surface
	Camera2D savedCamera = camera;
	ImVec2 savedMin = imageMin;
	ImVec2 savedMax = imageMax;
	ImVec2 savedSize = imageSize;

	imageMin = ImGui::GetItemRectMin();
	imageMax = ImGui::GetItemRectMax();
	imageSize = exportSize;

	camera.setDimensions((int)exportSize.x, (int)exportSize.y, imageMin);

	ImDrawList* drawList = ImGui::GetWindowDrawList();

	drawList->AddRectFilled(imageMin, imageMax, IM_COL32(19, 27, 37, 255));
	drawField(drawList);
	drawMeshOverlay(drawList);
	drawAxes(drawList);

	camera = savedCamera;
	imageMin = savedMin;
	imageMax = savedMax;
	imageSize = savedSize;

	ImGui::End();
	ImGui::PopStyleVar();

	offScreenFBO.endOffScreenImGuiRender(oldFBO, oldViewport, oldDisplaySize, oldFramebufferSize);
}

// ======================================================================
// -----------------------MAIN RENDER LOOP-------------------------------
// ======================================================================
void Inspector::render() {

	ImGui::Begin("Inspector");

	ImDrawList* drawList = ImGui::GetWindowDrawList();

	drawToolBar();

	ImVec2 pos = ImGui::GetCursorScreenPos();
	ImVec2 size = ImGui::GetContentRegionAvail();

	Rect surfaceRect = makePaddedRect(
		pos,
		size,
		0.0f,
		colorbar.width,
		0.0f,
		0.0f
	);

	resizeImage(surfaceRect.size());

	ImGui::SetCursorScreenPos(surfaceRect.min);
	ImGui::InvisibleButton(
		"##InspectorCanvas",
		surfaceRect.size(),
		ImGuiButtonFlags_MouseButtonLeft |
		ImGuiButtonFlags_MouseButtonRight |
		ImGuiButtonFlags_MouseButtonMiddle
	);

	imageMin = ImGui::GetItemRectMin();
	imageMax = ImGui::GetItemRectMax();
	imageSize = {
		imageMax.x - imageMin.x,
		imageMax.y - imageMin.y
	};

	camera.setDimensions(
		(int)imageSize.x,
		(int)imageSize.y,
		imageMin
	);

	if (pendingFrame && imageSize.x > 1.0f && imageSize.y > 1.0f) {
		frameToMesh();
		pendingFrame = false;
	}

	handleMouse();

	drawCanvas(drawList, surfaceRect, 5.0f);
	drawField(drawList);
	drawMeshOverlay(drawList);
	drawAxes(drawList);
	drawValueProbe(drawList);
	drawEmptyMessage(drawList);

	ImGui::SameLine();
	colorbar.render();

	ImGui::End();
}
