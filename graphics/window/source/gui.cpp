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

#include "gui_manager.h"

// ======================================================================
// -----------------------HELPER FUNCTIONS-------------------------------
// ======================================================================
void changeCursorOnHover() {
	if (ImGui::IsItemHovered()) {
		ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
	}
}

// initialize gui buffers
void initContext(ImGuiContext*& imguiContext, ImPlotContext*& implotContext, GLFWwindow* window = nullptr) {

	imguiContext = ImGui::CreateContext();
	implotContext = ImPlot::CreateContext();

	ImGui::SetCurrentContext(imguiContext);
	ImPlot::SetCurrentContext(implotContext);

	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	ImGui::StyleColorsDark();

	if (window) {
		ImGui_ImplGlfw_InitForOpenGL(window, true);
	}

	ImGui_ImplOpenGL3_Init("#version 330 core");

}

// ======================================================================
// -----------------------INITIALIZATION---------------------------------
// ======================================================================
GUI::GUI(GLFWwindow* window, SceneView& scene) :
	scene(scene),
	inspector(scene, assets),
	console(*this, scene),
	menu(*this, scene),
	mesh(scene.mesh),
	solver(scene.solver),
	results(scene.results),
	renderer(scene.renderer),
	bound(scene.bound),
	colormap(scene.colormap),
	meshGUI(*this, scene),
	solverGUI(scene),
	resultsGUI(*this, scene),
	residualPlot(scene.solver, assets),
	animationGUI(scene),
	config(scene.config)

{

	mesh.console = &console;
	solver.console = &console;
	results.console = &console;
	scene.picker.console = &console;

	solver.residualPlot = &residualPlot;

	IMGUI_CHECKVERSION();

	// initialize context. make sure to finish by setting the current context to main context
	initContext(mainImGuiContext, mainImPlotContext, window);
	initContext(exportImGuiContext, exportImPlotContext);

	ImGui::SetCurrentContext(mainImGuiContext);
	ImPlot::SetCurrentContext(mainImPlotContext);

	// initialize all icon assets
	createAssetBuffers();
}

void GUI::createAssetBuffers() {

	assets.houseIcon.createBuffer("assets/icons/house.png");
	assets.clearIcon.createBuffer("assets/icons/circle-x.png");
	assets.plusIcon.createBuffer("assets/icons/plus.png");
	assets.copyIcon.createBuffer("assets/icons/clipboard.png");
	assets.selectRegionIcon.createBuffer("assets/icons/square-dashed-mouse-pointer.png");

}

void GUI::newFrame() {

	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
	ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), UIFlags::BaseDockspaceFlags);

}

// ======================================================================
// -----------------------DRAW CALLS-------------------------------------
// ======================================================================
void GUI::drawUI() {
	ImGui::Begin("Project");
	if (ImGui::BeginTabBar("Main"), UIFlags::TabBarFlags) {

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

// ======================================================================
// -----------------------MAIN RENDER LOOP-------------------------------
// ======================================================================
void GUI::render() {

	menu.render();

	console.draw();

	drawUI();

	// draw colorbar only in the results tab
	if (scene.currentTab == TAB_RESULTS && results.isReady) {
		inspector.render();

		if (scene.solver.transient) {
			animationGUI.render();
		}

	}

	if (scene.currentTab == TAB_SOLVER) {
		residualPlot.draw();
	}

	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

	// copy to clipboard if there are any pending copies
	if (residualPlot.pendingCopy) {

		ImGuiContext* oldImGuiContext = ImGui::GetCurrentContext();
		ImPlotContext* oldImPlotContext = ImPlot::GetCurrentContext();

		ImGui::SetCurrentContext(exportImGuiContext);
		ImPlot::SetCurrentContext(exportImPlotContext);

		residualPlot.pendingCopy = false;
		residualPlot.copyActivePlotToClipboard(
			residualPlot.pendingCopyTabID,
			residualPlot.pendingCopyWidth,
			residualPlot.pendingCopyHeight);


		ImGui::SetCurrentContext(oldImGuiContext);
		ImPlot::SetCurrentContext(oldImPlotContext);
	}
}