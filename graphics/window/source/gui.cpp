#include "pch.h"
#include "gui.h"

#include <filesystem>
#include <iostream>
#include <unordered_set>

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
using namespace UITabBarFlags;

// ======================================================================
// -----------------------HELPER FUNCTIONS-------------------------------
// ======================================================================
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

	// Opaque menus / dropdowns (StyleColorsDark leaves these ~6% transparent)
	colors[ImGuiCol_PopupBg] = ImVec4(0.11f, 0.11f, 0.12f, 1.00f);
}

// UI font sizes (px). One scale knob drives all text and the font-sized tree/
// header icons; bump kUiScale to enlarge the whole UI. kUiFontNormal also sizes
// the tree indent (see initContext) so each branch icon aligns over its children.
namespace {
	constexpr float kUiScale      = 1.3f;
	constexpr float kUiFontSmall  = 14.0f * kUiScale;
	constexpr float kUiFontNormal = 18.0f * kUiScale;
	constexpr float kUiFontLarge  = 24.0f * kUiScale;
}

void initImGuiFonts(AppFonts& fonts) {


	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->Clear();

	fonts.defaultFont = io.Fonts->AddFontDefault();
	fonts.uiFontSmall = io.Fonts->AddFontFromFileTTF("assets/fonts/Roboto-Regular.ttf", kUiFontSmall);
	fonts.uiFontNormal = io.Fonts->AddFontFromFileTTF("assets/fonts/Roboto-Regular.ttf", kUiFontNormal);
	fonts.uiFontLarge = io.Fonts->AddFontFromFileTTF("assets/fonts/Roboto-Regular.ttf",	kUiFontLarge);

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

	// Indent tree children by the node's arrow width (fontSize + 2*FramePadding.x)
	// so a branch's icon lines up directly above its child-leaf icons. ImGui's
	// default IndentSpacing (21px) assumes the ~13px default font; the larger UI
	// font needs a wider indent to stay aligned.
	ImGui::GetStyle().IndentSpacing = kUiFontNormal + ImGui::GetStyle().FramePadding.x * 2.0f;

	if (window) {
		ImGui_ImplGlfw_InitForOpenGL(window, true);
	}

	ImGui_ImplOpenGL3_Init("#version 330 core");

}

TextureBuffer& AppAssets::icon(const std::string& name) {
	auto it = icons.find(name);
	if (it != icons.end()) return it->second;

	// unknown name: warn once and hand back a blank texture (id 0 draws a plain
	// quad) so a typo is visible but never crashes.
	static std::unordered_set<std::string> warned;
	if (warned.insert(name).second) {
		std::cerr << "[icons] missing icon '" << name
			<< "' (no '" << name << ".png' under assets/icons)\n";
	}
	static TextureBuffer blank;
	return blank;
}

// load every PNG under assets/icons (recursively, so image_buttons/, headers/,
// and any future subfolder are all included), keyed by file name without the
// extension. drop a PNG anywhere in that tree to add an icon — no code change.
void initAssetBuffers(AppAssets& assets) {

	namespace fs = std::filesystem;
	const std::string dir = "assets/icons";

	std::error_code ec;
	for (const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
		const fs::path& path = entry.path();
		if (path.extension() != ".png") continue;

		std::string name = path.stem().string();
		if (assets.icons.count(name)) {
			std::cerr << "[icons] duplicate icon name '" << name << "' ("
				<< path.string() << ") overrides an earlier one\n";
		}
		assets.icons[name].createBuffer(path.string().c_str());
	}
	if (ec) {
		std::cerr << "[icons] could not read '" << dir << "': " << ec.message() << "\n";
	}
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
	ImGui::DockSpace(dockspaceID, ImVec2(0.0f, 0.0f), UIDockFlags::BaseDockspaceFlags);

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
	menu(project, *this),
	inspector(project, scene, appConfig),
	meshInspector(project, appConfig),
	console(*this, project),
	mesh(project.mesh),
	solver(project.solver),
	results(project.results),
	geometryGUI(project, appConfig.assets),
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
	meshInspector.console = &console;
	inspector.console = &console;

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
void GUI::drawTutorial() {
	if (!showingTutorial) return;

}

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

	// length unit is edited in the Units modal (Option -> Edit -> Units);
	// the status bar only displays the active unit.
	uint8_t currentUnitIndex = Units::lengthUnitIndexForScale(project.lengthScale.value);
	const char* unitsPreview = Units::lengthUnits[currentUnitIndex].name;

	ImGui::SetCursorScreenPos(ImVec2(unitsComboX, textY));
	ImGui::TextDisabled("%s", unitsPreview);

	ImGui::End();

	ImGui::PopStyleVar(3);
}

void GUI::drawUI() {
	ImGui::Begin("Project");
	if (ImGui::BeginTabBar("Main", TabBarFlags)) {

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
		if (saveHotkeyPressed(project)) {
			console.addLine("Project Saved!");
		}
	}
}

// ======================================================================
// -----------------------MAIN RENDER LOOP-------------------------------
// ======================================================================
void GUI::render() {

	menu.render();

	// a project load (via the menu or at launch) requests that every surface
	// inspector recenter/re-zoom to the loaded units. Fan the request out here,
	// before the tab viewers render, so each applies it on this frame.
	if (project.resetInspectorViews) {
		sketch.requestResetView();
		meshInspector.requestResetView();
		inspector.requestResetView();
		project.resetInspectorViews = false;
	}

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
	drawTutorial();

	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

	// copy to clipboard if there are any pending copies
	if (residualPlot.pendingCopy) {

		setContext(exportImGuiContext, exportImPlotContext);

		residualPlot.pendingCopy = false;
		residualPlot.copyActivePlotToClipboard();

		setContext(mainImGuiContext, mainImPlotContext);

		console.addLine("copied to clipboard!");

	}

	if (inspector.pendingCopy) {

		setContext(exportImGuiContext, exportImPlotContext);

		inspector.pendingCopy = false;
		inspector.copyActiveSurfaceToClipboard();

		setContext(mainImGuiContext, mainImPlotContext);

		console.addLine("copied to clipboard!");

	}

	if (meshInspector.pendingCopy) {

		setContext(exportImGuiContext, exportImPlotContext);

		meshInspector.pendingCopy = false;
		meshInspector.copyActiveSurfaceToClipboard();

		setContext(mainImGuiContext, mainImPlotContext);

		console.addLine("copied to clipboard!");

	}

	if (sketch.pendingCopy) {

		setContext(exportImGuiContext, exportImPlotContext);

		sketch.pendingCopy = false;
		sketch.copyActiveSurfaceToClipboard();

		setContext(mainImGuiContext, mainImPlotContext);

		console.addLine("copied to clipboard!");

	}
}
