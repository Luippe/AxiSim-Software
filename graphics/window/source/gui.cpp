#include "pch.h"
#include "gui.h"

#include "implot.h"

#include "display.h"
#include "project.h"

#include "mesh.h"
#include "solver.h"
#include "results.h"

#include "printer.h"
#include "file_manager.h"
#include "solver_struct.h"

#include "flag_manager.h"
#include "keyboard_manager.h"

using namespace Shortcuts;

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

void applyTextColors() {
	ImGuiStyle& style = ImGui::GetStyle();
	ImVec4* colors = style.Colors;

	// Main normal text
	colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.00f);

	// Disabled text / gray text
	colors[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.60f, 0.65f, 1.00f);
}

void initImGuiFonts(AppFonts& fonts) {


	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->Clear();

	fonts.defaultFont = io.Fonts->AddFontDefault();
	fonts.uiFontSmall = io.Fonts->AddFontFromFileTTF("assets/fonts/Roboto-Regular.ttf", 14.0f);
	fonts.uiFontNormal = io.Fonts->AddFontFromFileTTF("assets/fonts/Roboto-Regular.ttf", 18.0f);
	fonts.uiFontLarge = io.Fonts->AddFontFromFileTTF("assets/fonts/Roboto-Regular.ttf",	24.0f);

    IM_ASSERT(fonts.uiFontNormal != nullptr);

    io.FontDefault = fonts.uiFontNormal;

}

// initialize gui buffers
void initContext(ImGuiContext*& imguiContext, ImPlotContext*& implotContext, GLFWwindow* window = nullptr) {

	imguiContext = ImGui::CreateContext();
	implotContext = ImPlot::CreateContext();

	setContext(imguiContext, implotContext);

	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.ConfigWindowsMoveFromTitleBarOnly = true;
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
	assets.drawRectangleIcon.createBuffer("assets/icons/draw-rectangle.png");
	assets.drawCircleIcon.createBuffer("assets/icons/draw-circle.png");
	assets.drawLineIcon.createBuffer("assets/icons/draw-line.png");
	assets.selectIcon.createBuffer("assets/icons/select.png");
	assets.trimIcon.createBuffer("assets/icons/trim.png");
	assets.crossArrowIcon.createBuffer("assets/icons/cross-arrow.png");
	assets.gridIcon.createBuffer("assets/icons/grid.png");

}

void GUI::newFrame() {

	setContext(mainImGuiContext, mainImPlotContext);

	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	ImGuiViewport* viewport = ImGui::GetMainViewport();

	ImVec2 dockPos = viewport->WorkPos;
	ImVec2 dockSize = viewport->WorkSize;
	dockSize.y -= statusBarHeight;

	ImGui::SetNextWindowPos(dockPos);
	ImGui::SetNextWindowSize(dockSize);
	ImGui::SetNextWindowViewport(viewport->ID);

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

	ImGui::Begin("MainDockSpace", nullptr, UIFlagsDocking::MainDockWindowFlags);

	ImGuiID dockspaceID = ImGui::GetID("MainDockSpaceID");
	ImGui::DockSpace(dockspaceID, ImVec2(0.0f, 0.0f), UIFlags::BaseDockspaceFlags);

	ImGui::End();

	ImGui::PopStyleVar(3);
}

// ======================================================================
// -----------------------INITIALIZATION---------------------------------
// ======================================================================
GUI::GUI(Project& project, Display& disp) :

	project(project),
	sketch(project, *this),
	scene(project, *this),
	menu(project),
	inspector(project, scene, appConfig),
	meshInspector(project, appConfig),
	console(*this, project),
	mesh(project.mesh),
	solver(project.solver),
	results(project.results),
	geometryGUI(project, *this),
	meshGUI(project, *this),
	solverGUI(project, appConfig),
	resultsGUI(project, *this),
	residualPlot(project.solver, appConfig),
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
	initContext(mainImGuiContext, mainImPlotContext, disp.window);
	initContext(exportImGuiContext, exportImPlotContext);

	setContext(mainImGuiContext, mainImPlotContext);

	// initialize all fonts and icon assets
	initImGuiFonts(appConfig.fonts);
	applyTextColors();
	initAssetBuffers(appConfig.assets);

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

	// vertical centering: text and the framed combo share one center line
	float fontHeight = ImGui::GetFontSize();
	float comboFramePadY = 2.0f;
	float comboHeight = fontHeight + comboFramePadY * 2.0f;
	float centerY = winMin.y + height * 0.5f;
	float textY = centerY - fontHeight * 0.5f;
	float comboY = centerY - comboHeight * 0.5f;

	// green ready dot
	ImVec2 dotCenter = ImVec2(winMin.x + 16.0f, centerY);
	drawList->AddCircleFilled(dotCenter, 5.0f, IM_COL32(50, 220, 80, 255));

	ImGui::SetCursorScreenPos(ImVec2(winMin.x + 30.0f, textY));
	if (project.currentTab == ViewTab::TAB_MESH) {

	}
	ImGui::TextUnformatted("Ready");

	// right side info
	const char* projectText = "Project: ";
	const char* unitsLabel = "Units: ";
	const char* projectName = project.name.empty() ? "Untitled" : project.name.c_str();

	ImVec2 projectSize = ImGui::CalcTextSize(projectText);
	ImVec2 projectNameSize = ImGui::CalcTextSize(projectName);
	ImVec2 unitsLabelSize = ImGui::CalcTextSize(unitsLabel);

	float rightPad = 20.0f;
	float gap = 24.0f;
	float labelComboGap = 6.0f;
	float unitsComboWidth = 52.0f;

	float unitsComboX = winMax.x - rightPad - unitsComboWidth;
	float unitsLabelX = unitsComboX - labelComboGap - unitsLabelSize.x;
	float sepX = unitsLabelX - gap;
	float projectX = sepX - gap - projectSize.x - projectNameSize.x;

	ImGui::SetCursorScreenPos(ImVec2(projectX, textY));
	ImGui::TextDisabled("%s", projectText);
	ImGui::SameLine(0.0f, 0.0f);
	ImGui::TextDisabled("%s", projectName);

	drawList->AddLine(
		ImVec2(sepX, comboY),
		ImVec2(sepX, comboY + comboHeight),
		IM_COL32(80, 95, 115, 180),
		1.0f
	);

	ImGui::SetCursorScreenPos(ImVec2(unitsLabelX, textY));
	ImGui::TextDisabled("%s", unitsLabel);

	ImGui::SetCursorScreenPos(ImVec2(unitsComboX, comboY));
	ImGui::SetNextItemWidth(unitsComboWidth);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, comboFramePadY));
	ImGui::PushID("StatusBarUnits");

	uint8_t currentUnitIndex = Units::lengthUnitIndexForScale(project.lengthScale.value);
	const char* unitsPreview = Units::lengthUnits[currentUnitIndex].name;
	if (ImGui::BeginCombo("##units", unitsPreview)) {
		for (int i = 0; i < (int)Units::lengthUnits.size(); i++) {
			bool isSelected = currentUnitIndex == i;

			if (ImGui::Selectable(Units::lengthUnits[i].name, isSelected)) {
				if (currentUnitIndex != i) {
					project.lengthScale.index = (uint8_t)i;
					project.lengthScale.value = 1.0 / Units::lengthUnits[i].toBase;
				}
			}

			if (isSelected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}

	ImGui::PopID();
	ImGui::PopStyleVar();

	ImGui::End();

	ImGui::PopStyleVar(3);
}

void GUI::drawUI() {
	ImGui::Begin("Project");
	if (ImGui::BeginTabBar("Main", UIFlags::TabBarFlags)) {

		geometryGUI.draw();

		meshGUI.draw();

		solverGUI.draw();

		resultsGUI.draw();

		ImGui::EndTabBar();
	}
	ImGui::End();
}

void GUI::handleKeyInput() {

	if (ImGui::IsKeyChordPressed(saveProjectShortcut)) {
		saveHotkeyPressed(project);
	}
}

// ======================================================================
// -----------------------MAIN RENDER LOOP-------------------------------
// ======================================================================
void GUI::render() {

	menu.render();

	console.draw();

	drawUI();

	// mesh GUI render
	if (project.currentTab == ViewTab::TAB_GEOMETRY) {
		sketch.render();
	}

	if (project.currentTab == ViewTab::TAB_MESH) {
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

		if (project.solver.configSolver.transient) {
			animationGUI.render();
		}
	}

	handleKeyInput();

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

	if (sketch.pendingCopy) {

		setContext(exportImGuiContext, exportImPlotContext);

		sketch.pendingCopy = false;
		sketch.copyActiveSurfaceToClipboard();

		setContext(mainImGuiContext, mainImPlotContext);

	}
}
