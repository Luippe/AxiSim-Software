#include "scene_view.h"
#include "display.h"
#include "camera.h"
#include "memory_manager.h"

SceneView::SceneView(Display& disp, Camera& camera, Renderer& renderer, Bounding& bound)
	: disp(disp),
	camera(camera),
	renderer(renderer),
	bound(bound),
	shaderMesh("graphics/shaders/mesh.vert", "graphics/shaders/mesh.frag"),
	shaderEdge("graphics/shaders/edge.vert", "graphics/shaders/edge.frag"),
	shaderLine("graphics/shaders/line.vert", "graphics/shaders/line.frag"),
	shaderResults("graphics/shaders/results.vert", "graphics/shaders/results.frag"),
	mesh(shaderMesh, config),
	results(mesh, solver, colormap, shaderResults),
	solver(*this, config),
	picker(*this)
{
	frameBuffer.createBuffer(disp.width, disp.height);
};

void SceneView::render() {

	if (currentTab == TAB_SOLVER) return;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin("Scene");

	ImVec2 avail = ImGui::GetContentRegionAvail();
	int viewportWidth = (int)avail.x;
	int viewportHeight = (int)avail.y;

	if (viewportWidth != frameBuffer.width || viewportHeight != frameBuffer.height)
	{
		// resize scene framebuffer
		frameBuffer.createBuffer(viewportWidth, viewportHeight);

		//update camera and picker width and height and position
		ImVec2 rectPos = ImGui::GetCursorScreenPos();

		camera.setDimensions(viewportWidth, viewportHeight, rectPos);
		picker.setDimensions(viewportWidth, viewportHeight, rectPos);
	}

	// update transformation matrix and snap camera
	camera.update();

	// draw and render calls
	frameBuffer.bind();

	glViewport(0, 0, viewportWidth, viewportHeight);
	glEnable(GL_DEPTH_TEST);
	glClearColor(0.4f, 0.4f, 0.4f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	if (currentTab == TAB_MESH) {
		// load transformation matrix for mesh shader
		shaderMesh.loadTransformationMatrix(camera);
		shaderLine.loadTransformationMatrix(camera);

		// update picker
		picker.update();

		// draw mesh
		mesh.render();

	}
	else if (currentTab == TAB_RESULTS) {

		// load transformation matrix for solution shader
		shaderResults.loadTransformationMatrix(camera);
		shaderLine.loadTransformationMatrix(camera);
		shaderEdge.loadTransformationMatrix(camera);

		// update picker
		picker.update();

		// draw results
		results.render(shaderLine, shaderEdge);
		
	}

	// draw coordinate axes
	renderer.renderAxis(shaderLine);

	// draw bounding box
	bound.renderBB(shaderLine);

	// end draw and render calls
	frameBuffer.unbind();

	// downscale the framebuffer
	frameBuffer.resolve();

	ImGui::Image(
		(ImTextureID)(intptr_t)frameBuffer.getTexture(),
		ImVec2((float)viewportWidth, (float)viewportHeight),
		ImVec2(0, 1),
		ImVec2(1, 0)
	);

	// check if the image is hovered or the window is focused
	hovered = ImGui::IsItemHovered();
	focused = ImGui::IsWindowFocused();
	//printf("RUNNING IN SCENE RENDER\n");
	ImGui::End();
	ImGui::PopStyleVar();
	//printMemoryUsage();
}