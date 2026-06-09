#include "pch.h"

#include "implot.h"

#include "project.h"
#include "gui.h"

#include "mesh.h"
#include "solver.h"
#include "results.h"

#include "bounding.h"
#include "printer.h"
#include "file_manager.h"
#include "solver_struct.h"

#include "IconsFontAwesome7.h"

#include "flag_manager.h"


// ======================================================================
// -----------------------HELPER FUNCTIONS-------------------------------
// ======================================================================
void changeCursorOnHover() {
	if (ImGui::IsItemHovered()) {
		ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
	}
}

void setContext(ImGuiContext* imguiContext, ImPlotContext* implotContext) {
	ImGui::SetCurrentContext(imguiContext);
	ImPlot::SetCurrentContext(implotContext);
}

// init imgui font awesome
void initImGuiFonts() {

	ImGuiIO& io = ImGui::GetIO();

	io.Fonts->AddFontDefault();

	ImFontConfig iconConfig;
	iconConfig.MergeMode = true;
	iconConfig.PixelSnapH = true;
	iconConfig.GlyphMinAdvanceX = 13.0f;

	static const ImWchar iconRanges[] = {
		ICON_MIN_FA,
		ICON_MAX_16_FA,
		0
	};

	io.Fonts->AddFontFromFileTTF(
		"assets/fonts/Font Awesome 7 Free-Solid-900.otf",
		13.0f,
		&iconConfig,
		iconRanges
	);
}

// initialize gui buffers
void initContext(ImGuiContext*& imguiContext, ImPlotContext*& implotContext, GLFWwindow* window = nullptr) {

	imguiContext = ImGui::CreateContext();
	implotContext = ImPlot::CreateContext();

	setContext(imguiContext, implotContext);

	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	ImGui::StyleColorsDark();

	if (window) {
		ImGui_ImplGlfw_InitForOpenGL(window, true);
	}

	ImGui_ImplOpenGL3_Init("#version 330 core");

}

void initAssetBuffers(AppAssets& assets) {

	assets.houseIcon.createBuffer("assets/icons/house.png");
	assets.clearIcon.createBuffer("assets/icons/circle-x.png");
	assets.plusIcon.createBuffer("assets/icons/plus.png");
	assets.copyIcon.createBuffer("assets/icons/clipboard.png");
	assets.selectRegionIcon.createBuffer("assets/icons/select-area.png");
	assets.connectIcon.createBuffer("assets/icons/connect-line.png");
	assets.eraseIcon.createBuffer("assets/icons/eraser.png");
	assets.rulerIcon.createBuffer("assets/icons/ruler.png");
	assets.fillCellIcon.createBuffer("assets/icons/fill-cell.png");

}

void GUI::newFrame() {

	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
	ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), UIFlags::BaseDockspaceFlags);

}	

// ======================================================================
// -----------------------INITIALIZATION---------------------------------
// ======================================================================
GUI::GUI(Project& project, GLFWwindow* window) :
	project(project),
	scene(project, *this),
	menu(project, *this),
	inspector(project, scene, assets),
	meshInspector(project.mesh, assets),
	console(*this, project),
	mesh(project.mesh),
	solver(project.solver),
	results(project.results),
	meshGUI(project, *this),
	solverGUI(project),
	resultsGUI(project, *this),
	residualPlot(project.solver, assets),
	animationGUI(project, *this),
	config(project.config)

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

	setContext(mainImGuiContext, mainImPlotContext);

	// initialize all fonts and icon assets
	initImGuiFonts();
	initAssetBuffers(assets);
}


// ======================================================================
// -----------------------DRAW CALLS-------------------------------------
// ======================================================================
void GUI::drawStatusBar() {

	ImGuiViewport* viewport = ImGui::GetMainViewport();

	float height = 26.0f;

	ImVec2 pos = ImVec2(
		viewport->WorkPos.x,
		viewport->WorkPos.y + viewport->WorkSize.y - height
	);

	ImVec2 size = ImVec2(
		viewport->WorkSize.x,
		height
	);

	ImGui::SetNextWindowPos(pos);
	ImGui::SetNextWindowSize(size);

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 4));

	ImGui::Begin("##MainStatusBar", nullptr, UIFlags::StatusBarWindowFlags);

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	ImVec2 winMin = ImGui::GetWindowPos();
	ImVec2 winMax = ImVec2(
		winMin.x + ImGui::GetWindowWidth(),
		winMin.y + ImGui::GetWindowHeight()
	);

	// green ready dot
	ImVec2 dotCenter = ImVec2(winMin.x + 16.0f, winMin.y + height * 0.5f);
	drawList->AddCircleFilled(dotCenter, 5.0f, IM_COL32(50, 220, 80, 255));

	ImGui::SetCursorScreenPos(ImVec2(winMin.x + 30.0f, winMin.y + 5.0f));
	if (project.currentTab == ViewTab::TAB_MESH) {

	}
	ImGui::TextUnformatted("Ready");

	// right side info
	const char* projectText = "Project: Untitled";
	const char* unitsText = "Units: m";

	ImVec2 projectSize = ImGui::CalcTextSize(projectText);
	ImVec2 unitsSize = ImGui::CalcTextSize(unitsText);

	float rightPad = 20.0f;
	float gap = 24.0f;

	float unitsX = winMax.x - rightPad - unitsSize.x;
	float sepX = unitsX - gap;
	float projectX = sepX - gap - projectSize.x;

	ImGui::SetCursorScreenPos(ImVec2(projectX, winMin.y + 5.0f));
	ImGui::TextDisabled("%s", projectText);

	drawList->AddLine(
		ImVec2(sepX, winMin.y + 5.0f),
		ImVec2(sepX, winMax.y - 5.0f),
		IM_COL32(80, 95, 115, 180),
		1.0f
	);

	ImGui::SetCursorScreenPos(ImVec2(unitsX, winMin.y + 5.0f));
	ImGui::TextDisabled("%s", unitsText);

	ImGui::End();

	ImGui::PopStyleVar(3);
}

void GUI::drawUI() {
	ImGui::Begin("Project");
	if (ImGui::BeginTabBar("Main"), UIFlags::TabBarFlags) {

		meshGUI.draw();

		solverGUI.draw();

		resultsGUI.draw();

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

	// mesh GUI render
	if (project.currentTab == ViewTab::TAB_MESH && mesh.isReady) {
		meshInspector.render();
	}

	// solver GUI render
	if (project.currentTab == ViewTab::TAB_SOLVER) {
		residualPlot.draw();
	}

	// results GUI render
	if (project.currentTab == ViewTab::TAB_RESULTS && results.isReady) {
		scene.render();
		inspector.render();

		if (project.solver.transient) {
			animationGUI.render();
		}
	}

	drawStatusBar();

	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

	// copy to clipboard if there are any pending copies
	if (residualPlot.pendingCopy) {

		setContext(exportImGuiContext, exportImPlotContext);

		residualPlot.pendingCopy = false;
		residualPlot.copyActivePlotToClipboard();

		setContext(mainImGuiContext, mainImPlotContext);

	}

	if (inspector.pendingCopy) {

		setContext(exportImGuiContext, exportImPlotContext);

		inspector.pendingCopy = false;
		inspector.copyActiveSurfaceToClipboard();

		setContext(mainImGuiContext, mainImPlotContext);

	}

	if (meshInspector.pendingCopy) {

		setContext(exportImGuiContext, exportImPlotContext);

		meshInspector.pendingCopy = false;
		meshInspector.copyActiveSurfaceToClipboard();

		setContext(mainImGuiContext, mainImPlotContext);

	}
}