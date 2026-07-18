#include "scene_view.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <unordered_map>

#include "project.h"
#include "gui.h"

#include "results.h"
#include "mesh.h"

#include "console.h"

#include "flag_manager.h"
#include "memory_manager.h"
#include "unit_manager.h"
#include "time_manager.h"
#include "math_func.h"
#include "printer.h"


SceneView::SceneView(Project& project, GUI& gui) :
	console(gui.console),
	shaderResults("graphics/shaders/results.vert", "graphics/shaders/results.frag"),
	shaderLine("graphics/shaders/line.vert", "graphics/shaders/line.frag"),
	shaderResultsUnstructured("graphics/shaders/results_us.vert", "graphics/shaders/results_us.frag"),
	results(project.results),
	project(project),
	picker(project, *this)
{
	windowClass.DockNodeFlagsOverrideSet = UIDockFlags::NoDockWindowFlags;

	frameBuffer.createBuffer(500, 500, samples);
	createBuffer();
};


bool SceneView::compareFloat(float value, FilterValues& filterValues) {

	constexpr float eps = 1e-6f;

	switch (results.currentCompareType) {
	case CompareType::None:
		return true;

	case CompareType::LessThan:
		return value < filterValues.valueAt;

	case CompareType::EqualTo:
		return std::abs(value - filterValues.valueAt) < eps;

	case CompareType::GreaterThan:
		return value > filterValues.valueAt;

	case CompareType::Between:
		return value > filterValues.valueLower && value < filterValues.valueUpper;

	case CompareType::Exclude:
		return value < filterValues.valueLower || value > filterValues.valueUpper;

	}

	return false;
}


void SceneView::handleMouse() {

	// check if the image is hovered or the window is focused
	hovered = ImGui::IsItemHovered();
	focused = ImGui::IsWindowFocused();

	if (!(hovered && focused)) return;

	ImGuiIO& io = ImGui::GetIO();

	// ------------ Mouse Clicking -----------------
	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {

		initX = io.MousePos.x;
		initY = io.MousePos.y;

		dragging = true;
		leftMouseDown = true;

	}

	if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {

		dragging = false;
		leftMouseDown = false;

		ImVec2 drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);

		bool wasClick = abs(drag.x) < 3.0f && abs(drag.y) < 3.0f;	// check if the mouse movement is small enough to be considered a click

		if (wasClick) {
			check();
			picker.pick();
		}

		ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
	}

	// ------------ Camera Panning -----------------
	if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
		camera.calculatePan(io.MouseDelta.x, -io.MouseDelta.y);
	}

	// ------------ Camera Rotation-----------------
	if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
		rotating = true;
	}

	if (ImGui::IsMouseReleased(ImGuiMouseButton_Middle)) {
		rotating = false;
		ImGui::ResetMouseDragDelta(ImGuiMouseButton_Middle);
	}

	if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {

		glm::vec2 currentMouse(io.MousePos.x, io.MousePos.y);
		glm::vec2 previousMouse = currentMouse - glm::vec2(io.MouseDelta.x, io.MouseDelta.y);

		camera.calculateRotation(previousMouse, currentMouse);
	}

	// ------------ Camera Zooming -----------------
	if (io.MouseWheel != 0.0f) {
		camera.calculateZoom(io.MouseWheel);
	}
}

void SceneView::uploadUniforms() {

	shaderResults.use();
	shaderResults.SetFloat("vmin", results.currentField->vmin);
	shaderResults.SetFloat("vmax", results.currentField->vmax);
	shaderResults.SetFloat("R", results.g.R);
	shaderResults.SetFloat("L", results.g.L);
	shaderResults.SetInt("fieldTex", 0);
	shaderResults.SetInt("uColormap", 1);

}

std::vector<CylinderInstance> SceneView::createRowMergedCylinderInstances(
	std::vector<float>& field,
	FilterValues& filterValues
) {

	std::vector<CylinderInstance> instances;

	for (int i = 0; i < results.nr; i++) {
		int j = 0;

		while (j < results.nz) {
			int n = i * results.nz + j;

			if (!compareFloat(field[n], filterValues)) {
				j++;
				continue;
			}

			// Start selected run
			int j0 = j;

			while (j < results.nz && compareFloat(field[i * results.nz + j], filterValues)) {
				j++;
			}

			// j1 is one past the last selected cell
			int j1 = j;

			CylinderInstance inst{};

			// Axial bounds from face locations
			inst.x0 = (float)(results.g.zFace[j0]);
			inst.x1 = (float)(results.g.zFace[j1]);

			// Radial bounds from face locations
			inst.innerR = (float)(results.g.rFace[i]);
			inst.outerR = (float)(results.g.rFace[i + 1]);

			instances.push_back(inst);
		}
	}

	return instances;
}

void SceneView::updateSelectedInstances() {	// might be heavy on the cpu, optimize if AxiSim starts lagging

	selectedInstances = createRowMergedCylinderInstances(results.currentField->cellValues, results.filterValues);
	cvInstanceBuffer.bufferSubData(selectedInstances.size() * sizeof(CylinderInstance), selectedInstances.data());

}

void SceneView::createBuffer() {

	cvBuffer.createBuffer(results.verticesCV.size() * sizeof(CylinderTemplateVertex), results.verticesCV.data());

	cvBuffer.bind(); // bind the VAO you will draw with

	cvElementBuffer.createBuffer(results.indicesCV.size() * sizeof(unsigned int), results.indicesCV.data());

	// Per-vertex attributes: locations 0, 1, 2
	cvBuffer.enableAttribute(0, 3, GL_FLOAT, sizeof(CylinderTemplateVertex), (void*)offsetof(CylinderTemplateVertex, dir));
	cvBuffer.enableAttribute(1, 1, GL_FLOAT, sizeof(CylinderTemplateVertex), (void*)offsetof(CylinderTemplateVertex, xCoord));
	cvBuffer.enableAttribute(2, 1, GL_FLOAT, sizeof(CylinderTemplateVertex), (void*)offsetof(CylinderTemplateVertex, radialCoord));

	// ---------------- Instance VBO ----------------
	cvInstanceBuffer.createBuffer(results.nr * results.nz * sizeof(CylinderInstance), nullptr);

	// IMPORTANT:
	cvBuffer.bind(); // bind the VAO you draw with

	glBindBuffer(GL_ARRAY_BUFFER, cvInstanceBuffer.getVBO());
	cvBuffer.enableAttribute(3, 4, GL_FLOAT, sizeof(CylinderInstance), (void*)0);
	glVertexAttribDivisor(3, 1);

	cvBuffer.unbind();

	// a freshly generated result needs its revolved surface rebuilt
	usDirty = true;

}

void SceneView::markUnstructuredDirty() {
	usDirty = true;
}

void SceneView::draw3DPreview() {

	// draw results
	if (!results.isReady) return;

	if (project.mesh.currentMeshType == MeshType::Unstructured) {

		// revolve the unstructured 2D field into a 3D surface
		if (unstructuredNeedsRebuild()) {
			buildUnstructuredSurface();
		}

		drawUnstructured3D();
	}
	else {

		updateSelectedInstances();

		shaderResults.use();
		uploadUniforms();

		cvInstanceBuffer.bufferSubData(
			selectedInstances.size() * sizeof(CylinderInstance),
			selectedInstances.data()
		);

		glActiveTexture(GL_TEXTURE0);
		results.currentField->textureBuffer.bind();

		glActiveTexture(GL_TEXTURE1);
		colormap.bind();

		cvBuffer.bind();
		glDrawElementsInstanced(
			GL_TRIANGLES,
			(GLsizei)(results.indicesCV.size()),
			GL_UNSIGNED_INT,
			0,
			(GLsizei)(selectedInstances.size())
		);

		cvBuffer.unbind();

		glActiveTexture(GL_TEXTURE1);
		colormap.unbind();

		glActiveTexture(GL_TEXTURE0);
		results.currentField->textureBuffer.unbind();
	}

	// draw coordinate axes
	renderer.renderAxis(shaderLine);

	// draw bounding box
	bound.renderBB(shaderLine);

}

// ======================================================================
// -----------------------UNSTRUCTURED RESULTS---------------------------
// ======================================================================
const std::vector<double>* SceneView::currentUnstructuredField() const {

	if (results.fieldType.empty()) {
		return nullptr;
	}

	int idx = std::clamp(results.currentItem, 0, (int)results.fieldType.size() - 1);
	const std::string& name = results.fieldType[idx];

	auto it = results.solutions.find(name);
	if (it == results.solutions.end()) {
		return nullptr;
	}

	return &it->second.field;
}

bool SceneView::unstructuredNeedsRebuild() {

	if (usDirty) {
		return true;
	}

	if (results.fieldType.empty()) {
		return false;
	}

	int idx = std::clamp(results.currentItem, 0, (int)results.fieldType.size() - 1);

	if (results.fieldType[idx] != usFieldName) return true;
	if ((int)results.currentShadingType != usShading) return true;
	if ((int)results.currentCompareType != usCompare) return true;
	if (results.filterValues.valueAt != usValueAt) return true;
	if (results.filterValues.valueLower != usValueLower) return true;
	if (results.filterValues.valueUpper != usValueUpper) return true;

	return false;
}

void SceneView::buildUnstructuredSurface() {

	usVertexData.clear();
	usVertexCount = 0;

	// record the signature this build corresponds to
	if (!results.fieldType.empty()) {
		int idx = std::clamp(results.currentItem, 0, (int)results.fieldType.size() - 1);
		usFieldName = results.fieldType[idx];
	}
	usShading = (int)results.currentShadingType;
	usCompare = (int)results.currentCompareType;
	usValueAt = results.filterValues.valueAt;
	usValueLower = results.filterValues.valueLower;
	usValueUpper = results.filterValues.valueUpper;
	usDirty = false;

	const std::vector<Vec2>& pts = project.mesh.unstructuredPoints;
	const std::vector<Triangle>& tris = project.mesh.unstructuredTriangles;
	const std::vector<double>* fieldPtr = currentUnstructuredField();

	if (!fieldPtr || pts.empty() || tris.empty()) {
		return;
	}

	const std::vector<double>& field = *fieldPtr;
	if (field.empty()) {
		return;
	}

	// value range
	double lo = DBL_MAX;
	double hi = -DBL_MAX;
	bool found = false;
	for (double v : field) {
		if (!std::isfinite(v)) continue;
		lo = std::min(lo, v);
		hi = std::max(hi, v);
		found = true;
	}
	if (!found) {
		return;
	}
	if (hi - lo < 1.0e-30) {
		hi = lo + 1.0e-30;
	}
	if (results.currentField) {
		results.currentField->vmin = (float)lo;
		results.currentField->vmax = (float)hi;
	}

	const int nCells = (int)tris.size();
	const int nPts = (int)pts.size();

	auto validTri = [&](const Triangle& t) {
		return t.v0 >= 0 && t.v1 >= 0 && t.v2 >= 0 &&
			t.v0 < nPts && t.v1 < nPts && t.v2 < nPts;
	};

	// which cells pass the active filter
	std::vector<uint8_t> pass(nCells, 0);
	for (int c = 0; c < nCells; c++) {
		double v = (c < (int)field.size()) ? field[c] : 0.0;
		pass[c] = compareFloat((float)v, results.filterValues) ? 1 : 0;
	}

	bool smooth = (results.currentShadingType == ShadingType::Interp);

	// vertex-averaged values for smooth shading of the cut planes
	std::vector<float> vavg;
	if (smooth) {
		vavg.assign(nPts, 0.0f);
		std::vector<int> cnt(nPts, 0);
		int n = std::min(nCells, (int)field.size());
		for (int c = 0; c < n; c++) {
			const Triangle& t = tris[c];
			if (!validTri(t)) continue;
			float v = (float)field[c];
			int ids[3] = { t.v0, t.v1, t.v2 };
			for (int k = 0; k < 3; k++) {
				vavg[ids[k]] += v;
				cnt[ids[k]] += 1;
			}
		}
		for (int i = 0; i < nPts; i++) {
			if (cnt[i] > 0) vavg[i] /= (float)cnt[i];
		}
	}

	// angular slices for the revolution
	const float twoPi = 6.28318530718f;
	int nSlices = std::max(8, (int)((float)results.nseg * (usSweep / twoPi)));

	// A full sweep closes on itself (slice nSlices wraps to slice 0), so the
	// lateral surfaces alone bound a closed solid and the flat cut-plane caps
	// below are only needed to seal a partial (< 360 degree) wedge.
	const bool fullRevolve = usSweep >= twoPi - 1.0e-4f;

	std::vector<float> cosT(nSlices + 1);
	std::vector<float> sinT(nSlices + 1);
	for (int k = 0; k <= nSlices; k++) {
		float a = usSweep * (float)k / (float)nSlices;
		cosT[k] = std::cos(a);
		sinT[k] = std::sin(a);
	}

	auto pushRevolved = [&](const Vec2& p, int k, float val) {
		usVertexData.push_back((float)p.z);
		usVertexData.push_back((float)p.r * cosT[k]);
		usVertexData.push_back((float)p.r * sinT[k]);
		usVertexData.push_back(val);
	};

	// ---- cut-plane caps at both ends of the sweep (partial revolve only) ----
	if (!fullRevolve) {
		const int capSlice[2] = { 0, nSlices };
		for (int c = 0; c < nCells; c++) {
			if (!pass[c]) continue;
			const Triangle& t = tris[c];
			if (!validTri(t)) continue;

			float vc = (float)((c < (int)field.size()) ? field[c] : 0.0);
			float va = smooth ? vavg[t.v0] : vc;
			float vb = smooth ? vavg[t.v1] : vc;
			float vd = smooth ? vavg[t.v2] : vc;

			for (int s = 0; s < 2; s++) {
				int k = capSlice[s];
				pushRevolved(pts[t.v0], k, va);
				pushRevolved(pts[t.v1], k, vb);
				pushRevolved(pts[t.v2], k, vd);
			}
		}
	}

	// ---- lateral surfaces for edges exposed by the selected set ----
	std::unordered_map<uint64_t, std::array<int, 2>> edgeAdj;
	edgeAdj.reserve((size_t)nCells * 3);

	auto edgeKey = [](int a, int b) -> uint64_t {
		if (a > b) std::swap(a, b);
		return ((uint64_t)(uint32_t)a << 32) | (uint32_t)b;
	};

	auto addEdge = [&](int a, int b, int c) {
		uint64_t key = edgeKey(a, b);
		auto it = edgeAdj.find(key);
		if (it == edgeAdj.end()) {
			edgeAdj.emplace(key, std::array<int, 2>{ c, -1 });
		}
		else if (it->second[1] < 0) {
			it->second[1] = c;
		}
	};

	for (int c = 0; c < nCells; c++) {
		const Triangle& t = tris[c];
		if (!validTri(t)) continue;
		addEdge(t.v0, t.v1, c);
		addEdge(t.v1, t.v2, c);
		addEdge(t.v2, t.v0, c);
	}

	for (const auto& kv : edgeAdj) {
		int c0 = kv.second[0];
		int c1 = kv.second[1];

		bool pass0 = (c0 >= 0 && c0 < nCells && pass[c0]);
		bool pass1 = (c1 >= 0 && c1 < nCells && pass[c1]);

		int owner = -1;
		if (pass0 && !pass1) owner = c0;
		else if (pass1 && !pass0) owner = c1;
		else continue; // interior to the selected set (or fully hidden)

		int aId = (int)(kv.first >> 32);
		int bId = (int)(kv.first & 0xffffffffu);
		if (aId >= nPts || bId >= nPts) continue;

		float val = (float)((owner < (int)field.size()) ? field[owner] : 0.0);

		const Vec2& A = pts[aId];
		const Vec2& B = pts[bId];

		for (int k = 0; k < nSlices; k++) {
			pushRevolved(A, k, val);
			pushRevolved(B, k, val);
			pushRevolved(B, k + 1, val);

			pushRevolved(A, k, val);
			pushRevolved(B, k + 1, val);
			pushRevolved(A, k + 1, val);
		}
	}

	usVertexCount = (int)(usVertexData.size() / 4);

	if (usVertexCount == 0) {
		return;
	}

	usBuffer.createBuffer(usVertexData.size() * sizeof(float), usVertexData.data());
	usBuffer.bind();
	usBuffer.bindVBO();
	usBuffer.enableAttribute(0, 3, GL_FLOAT, 4 * sizeof(float), (void*)0);
	usBuffer.enableAttribute(1, 1, GL_FLOAT, 4 * sizeof(float), (void*)(3 * sizeof(float)));
	usBuffer.unbind();
}

void SceneView::uploadUnstructuredUniforms() {

	shaderResultsUnstructured.use();

	if (results.currentField) {
		shaderResultsUnstructured.SetFloat("vmin", results.currentField->vmin);
		shaderResultsUnstructured.SetFloat("vmax", results.currentField->vmax);
	}

	shaderResultsUnstructured.SetInt("uColormap", 0);
}

void SceneView::drawUnstructured3D() {

	if (usVertexCount == 0) {
		return;
	}

	shaderResultsUnstructured.use();
	uploadUnstructuredUniforms();

	glActiveTexture(GL_TEXTURE0);
	colormap.bind();

	usBuffer.bind();
	glDrawArrays(GL_TRIANGLES, 0, usVertexCount);
	usBuffer.unbind();

	glActiveTexture(GL_TEXTURE0);
	colormap.unbind();
}

void SceneView::render() {

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::SetNextWindowClass(&windowClass);
	ImGui::Begin("Scene");

	rectSize = ImGui::GetContentRegionAvail();
	int viewportWidth = (int)rectSize.x;
	int viewportHeight = (int)rectSize.y;

	if (viewportWidth != frameBuffer.width || viewportHeight != frameBuffer.height) {

		// resize scene framebuffer
		frameBuffer.createBuffer(viewportWidth, viewportHeight, samples);

		//update camera and picker width and height and position
		rectPos = ImGui::GetCursorScreenPos();

		camera.setDimensions(viewportWidth, viewportHeight, rectPos);
		picker.setDimensions(viewportWidth, viewportHeight, rectPos);
	}

	// update transformation matrix and snap camera
	camera.updateTransformationMatrix();
	camera.snapCamera();

	// draw and render calls
	frameBuffer.bind();

	glEnable(GL_DEPTH_TEST);
	glClearColor(0.4f, 0.4f, 0.4f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// load transformation matrix for solution shader
	glm::mat4 model = scaleMat4(camera.model, (float)(project.lengthScale.value));
	shaderLine.loadTransformationMatrix(model, camera.view, camera.projection);
	shaderResults.loadTransformationMatrix(model, camera.view, camera.projection);
	shaderResultsUnstructured.loadTransformationMatrix(model, camera.view, camera.projection);


	// update picker
	picker.update();

	draw3DPreview();

	// end draw and render calls
	frameBuffer.unbind();

	// downscale the framebuffer
	frameBuffer.resolve();

	ImGui::Image(
		(ImTextureID)(intptr_t)frameBuffer.getTextureID(),
		ImVec2((float)viewportWidth, (float)viewportHeight),
		ImVec2(0, 1),
		ImVec2(1, 0)
	);

	handleMouse();

	//printf("RUNNING IN SCENE RENDER\n");
	ImGui::End();
	ImGui::PopStyleVar();
	//printMemoryUsage();
}