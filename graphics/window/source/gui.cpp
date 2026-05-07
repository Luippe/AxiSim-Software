#include "pch.h"
#include "gui.h"
#include "results.h"
#include "scene_view.h"
#include "bounding.h"
#include "solver.h"
#include "mesh.h"
#include "printer.h"
#include "manage_file.h"
#include "solver_struct.h"
#include "implot.h"

GUI::GUI(GLFWwindow* window, SceneView& scene) :
	scene(scene),
	inspector(scene),
	console(*this, scene),
	menu(*this, scene),
	mesh(scene.mesh),
	solver(scene.solver),
	results(scene.results),
	renderer(scene.renderer),
	bound(scene.bound),
	colormap(scene.colormap),
	colorbar(scene.colormap, scene.results),
	meshGUI(*this, scene),
	solverGUI(scene),
	resultsGUI(*this, scene),
	animationGUI(scene),
	config(scene.config)

{

	mesh.console = &console;
	solver.console = &console;
	results.console = &console;
	scene.picker.console = &console;

	initGUIBuffer(window);

}

// initialize gui buffers
void GUI::initGUIBuffer(GLFWwindow* window){
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImPlot::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	ImGui::StyleColorsDark();
	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init("#version 330 core");

}

void GUI::newFrame() {
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
	ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
}

void GUI::changeCursorOnHover() {
	if (ImGui::IsItemHovered()) {
		ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
	}
}

void GUI::drawUI() {
	ImGui::Begin("Window");
	if (ImGui::BeginTabBar("Main")) {

		meshGUI.draw();

		solverGUI.draw();

		resultsGUI.draw();

// ----------------------------------------- SETTINGS --------------------------------------
		if (ImGui::BeginTabItem("Settings")) {
			if (ImGui::CollapsingHeader("Bounding Box")) {
				ImGui::Checkbox("Show Bounding Box", &bound.showBB);
			}
			changeCursorOnHover();

			if (ImGui::CollapsingHeader("Options")) {
				ImGui::Checkbox("Show Mesh", &mesh.showMesh);
				ImGui::BeginDisabled(!results.isReady);
				ImGui::Checkbox("Show Wireframe", &results.showOutline);
				ImGui::EndDisabled();
			}
			changeCursorOnHover();
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}

	ImGui::End();

}

void GUI::render() {

	menu.render();

	console.draw();

	drawUI();

	// draw colorbar only in the results tab
	if (scene.currentTab == TAB_RESULTS && results.isReady) {
		colorbar.render();
		inspector.render();

		if (scene.solver.transient) {
			animationGUI.render();
		}

	}

	if (scene.currentTab == TAB_SOLVER) {
		solver.residualPlot.draw();
	}

	//printf("RUNNING IN GUI RENDER\n");
	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
