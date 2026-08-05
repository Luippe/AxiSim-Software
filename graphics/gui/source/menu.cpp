#include "menu.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>

#include "base_gui.h"
#include "gui.h"
#include "imgui_internal.h"

#include "file_manager.h"
#include "flag_manager.h"
#include "keyboard_manager.h"
#include "stream_capture.h"
#include "unit_manager.h"

using namespace Shortcuts;
using namespace Snapping;
using namespace UIFlagsDocking;

Menu::Menu(Project& project, GUI& gui) :
	project(project),
	assets(gui.appConfig.assets),
	gui(gui) {
	loadAtLaunch(project, settings);
};

bool Menu::beginMenu(const char* label, TextureBuffer& icon, bool enabled) {

	ImGuiWindow* itemWindow = ImGui::GetCurrentWindow();
	bool isMenuBar = itemWindow->DC.LayoutType == ImGuiLayoutType_Horizontal;
	std::string menuLabel = label;
	if (isMenuBar) {
		menuLabel = std::string(menuIconPlaceholder) + label + "##" + label;
	}

	bool isOpen = ImGui::BeginMenuEx(
		menuLabel.c_str(),
		isMenuBar ? nullptr : menuIconPlaceholder,
		enabled
	);
	drawLastMenuIcon(icon, itemWindow);
	return isOpen;
}

bool Menu::beginMenu(const char* label, bool enabled) {
	return ImGui::BeginMenu(label, enabled);
}

bool Menu::menuItem(
	const char* label,
	TextureBuffer& icon,
	const char* shortcut,
	bool selected,
	bool enabled
) {

	ImGuiWindow* itemWindow = ImGui::GetCurrentWindow();
	bool isMenuBar = itemWindow->DC.LayoutType == ImGuiLayoutType_Horizontal;
	std::string itemLabel = label;
	if (isMenuBar) {
		itemLabel = std::string(menuIconPlaceholder) + label + "##" + label;
	}

	bool clicked = ImGui::MenuItemEx(
		itemLabel.c_str(),
		isMenuBar ? nullptr : menuIconPlaceholder,
		shortcut,
		selected,
		enabled
	);
	drawLastMenuIcon(icon, itemWindow);
	return clicked;
}

bool Menu::menuItem(
	const char* label,
	const char* shortcut,
	bool selected,
	bool enabled
) {
	return ImGui::MenuItem(label, shortcut, selected, enabled);
}

void Menu::drawLastMenuIcon(TextureBuffer& icon, ImGuiWindow* itemWindow) {
	if (!itemWindow || !ImGui::IsItemVisible()) {
		return;
	}

	const unsigned int textureID = icon.getTextureID();
	if (textureID == 0) {
		return;
	}

	const ImGuiStyle& style = ImGui::GetStyle();
	const ImVec2 itemMin = ImGui::GetItemRectMin();
	const ImVec2 itemMax = ImGui::GetItemRectMax();
	const float iconSize = ImGui::GetFontSize() * menuIconScale;

	float iconX = itemMin.x + style.FramePadding.x;
	if (itemWindow->DC.LayoutType != ImGuiLayoutType_Horizontal) {
		iconX = itemMin.x + itemWindow->DC.MenuColumns.OffsetIcon;
	}

	const float iconY = itemMin.y + (itemMax.y - itemMin.y - iconSize) * 0.5f;
	itemWindow->DrawList->AddImage(
		(ImTextureID)(intptr_t)textureID,
		ImVec2(iconX, iconY),
		ImVec2(iconX + iconSize, iconY + iconSize)
	);
}

void Menu::drawNew() {

	if (beginMenu("New", assets.icon("new"))) {

		if (menuItem("Project")) {

			// Save the current project before replacing it, so unsaved work isn't lost.
			// Named project -> overwrite its file; unnamed -> prompt with Save As.
			if (!project.name.empty()) {
				saveFromPathProject(project.path, project);
			}
			else {
				saveFromExplorerProject(project);
			}

			// If the project is still unnamed, the Save As dialog was cancelled; don't
			// discard the current work by creating a new project.
			if (!project.name.empty()) {
				project.createNew();
			}
		}

		ImGui::EndMenu();
	}

}

void Menu::drawOpen() {
	if (beginMenu("Open", assets.icon("open"))) {

		if (menuItem("Project")) {
			loadFromExplorerProject(project);
		}

		if (beginMenu("Presets")) {

			//if (ImGui::MenuItem("Concentration Demo 1")) {
			//	loadPresetProject("concentration_demo_preset_1.bin", project);
			//}

			//if (ImGui::MenuItem("Concentration Demo 2")) {
			//	loadPresetProject("concentration_demo_preset_2.bin", project);
			//}

			ImGui::EndMenu();
		}

		if (menuItem("Geometry")) {
			loadFromExplorerGeometry(project.geometry);
		}

		if (menuItem("Open Current Project At Startup")) {
			saveSettings(project, settings);
		}

		ImGui::EndMenu();
	}

}

void Menu::drawExport() {

	if (beginMenu("Export")) {

		if (menuItem("Geometry")) {

			saveFromExplorerGeometry(project.geometry);

		}

		if (menuItem("Mesh")) {

			// The mesh writers report on stderr, which in a windowed session goes to
			// a console window behind the app. Without this the OpenFOAM export could
			// fail -- leaving a case directory with no controlDict in it -- and the
			// GUI would say nothing, so the first sign of trouble was blockMesh
			// refusing the case minutes later. Tee what the export says into the
			// panel the user is actually looking at.
			//
			// This also carries the boundary-group warnings, which do not fail the
			// export but decide whether the patches mean anything.
			StreamCapture captured;

			saveFromExplorerMesh(project.mesh, project.solver);

			for (const std::string& line : captured.lines()) {
				gui.console.addLine(line);
			}
		}
		// Gated on frames actually existing rather than on the Transient checkbox,
		// which can be turned on (or off) without a run behind it -- the same
		// self-gating the playback bar uses.
		if (menuItem("Animation", nullptr, false, project.results.hasAnimation())) {

			std::wstring path = saveFileDialog(FileKind::Animation);

			if (!path.empty()) {
				// mp4 or a png sequence, decided by the extension the Save-as-type
				// dropdown put on the name -- see AnimationGUI::beginExport.
				gui.animationGUI.beginExport(std::filesystem::path(path));
			}
		}

		if (menuItem("Solution", nullptr, false, project.results.isReady)) {
			saveFromExplorerSolution(project);
		}
		ImGui::EndMenu();
	}
}

void Menu::drawView() {

	// Checked = the panels are showing. Unchecking leaves only the live viewport;
	// the GUI picks the flag up on the next frame, not this one.
	if (beginMenu("Interface")) {
		ViewInterface& interface = project.interface;
		if (menuItem("Workspace", nullptr, !interface.workspace)) {
			interface.workspace = !interface.workspace;
		}
		else if (menuItem("Tool Bar", nullptr, !interface.toolbar)) {
			interface.toolbar = !interface.toolbar;
		}
		else if (menuItem("Status Bar", nullptr, !interface.statusBar)) {
			interface.statusBar = !interface.statusBar;
		}
		else if (menuItem("Console", nullptr, !interface.console)) {
			interface.console = !interface.console;
		}
		ImGui::EndMenu();
	}

	ImGui::Separator();

	// What the Results scene draws on top of the solution itself.
	if (beginMenu("Results")) {

		SceneView& scene = gui.scene;

		// flat line cross through world zero, part of the scene
		if (menuItem("Origin Axis", nullptr, scene.showOriginAxis)) {
			scene.showOriginAxis = !scene.showOriginAxis;
		}

		ImGui::EndMenu();
	}
}


void Menu::drawSave() {

	if (menuItem("Save", assets.icon("save"))) {
		if (!project.name.empty()) {
			saveFromPathProject(project.path, project);
		}
		else {
			saveFromExplorerProject(project);
		}
	}

	if (menuItem("Save As", assets.icon("save_as"))) {
		saveFromExplorerProject(project);
	}
}


bool isModifierKey(ImGuiKey key) {
	return key == ImGuiKey_LeftCtrl ||
		key == ImGuiKey_RightCtrl ||
		key == ImGuiKey_LeftShift ||
		key == ImGuiKey_RightShift ||
		key == ImGuiKey_LeftAlt ||
		key == ImGuiKey_RightAlt ||
		key == ImGuiKey_LeftSuper ||
		key == ImGuiKey_RightSuper;
}

bool captureShortcut(ImGuiKeyChord& capturedShortcut) {
    ImGuiIO& io = ImGui::GetIO();

    for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_GamepadStart; key++) {
        ImGuiKey imguiKey = (ImGuiKey)key;

        if (isModifierKey(imguiKey)) {
            continue;
        }

        if (ImGui::IsKeyPressed(imguiKey, false)) {
            capturedShortcut = io.KeyMods | imguiKey;
            return true;
        }
    }

    return false;
}

std::string shortcutButtonLabel(
	const char* label,
	ImGuiKeyChord shortcut,
	bool editing
) {
	if (editing) {
		return std::string("Press new shortcut...##") + label;
	}

	return std::string(label) + ": " + ImGui::GetKeyChordName(shortcut);

}

void Menu::drawEditShortcut() {

	if (beginMenu("Keyboard")) {
		if (menuItem("Keyboard Shortcuts")) {
			openShortcutModal = true;
		}
		if (menuItem("Snapping")) {
			openSnappingModal = true;
		}
		ImGui::EndMenu();
	}

	if (menuItem("Units", assets.icon("units"))) {
		openUnitsModal = true;
	}

	if (menuItem("Advanced Options")) {
		openAdvancedOptionsModal = true;
	}
}

bool shortcutExists(ImGuiKeyChord shortcut, ImGuiKeyChord* currentShortcut) {
	for (ImGuiKeyChord* existingShortcut : allShortcuts) {
		if (existingShortcut == currentShortcut) {
			continue;
		}

		if (*existingShortcut == shortcut) {
			return true;
		}
	}
	return false;
}

void Menu::drawSnappingModal() {
	if (openSnappingModal) {
		ImGui::OpenPopup("Snapping");
		openSnappingModal = false;
	}

	if (ImGui::BeginPopupModal(
		"Snapping",
		nullptr,
		ModalPopupFlags
	)) {
		bool justOpened = ImGui::IsWindowAppearing();

		ImGui::TextDisabled("What holding Ctrl snaps to in the sketch view");
		ImGui::Separator();

		auto snapCheckbox = [](const char* label, bool& setting, const char* tooltip) {
			ImGui::Checkbox(label, &setting);

			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("%s", tooltip);
			}
		};

		snapCheckbox(
			"Sketch",
			snapToSketch,
			"Edges of drawn geometry: line bodies, rectangle sides and centers,\n"
			"circle rims and centers, arc rims and centers."
		);

		snapCheckbox(
			"Axis",
			snapToAxis,
			"The r = 0 and z = 0 lines.\n"
			"Off by default -- they run the width of the canvas, so anything drawn\n"
			"near one gets pulled onto it. The origin snaps either way."
		);

		snapCheckbox(
			"Grid",
			snapToGrid,
			"Grid vertices. Only active while the grid is being shown."
		);

		snapCheckbox(
			"Points",
			snapToPoints,
			"Sketch points -- including line endpoints, which are stored as points --\n"
			"and the origin."
		);

		ImGui::Separator();

		if (ImGui::Button("Close")) {
			ImGui::CloseCurrentPopup();
		}

		ImGui::SameLine();

		if (ImGui::Button("Reset To Default")) {
			resetSnappingToDefault();
		}

		bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
		bool clickedOutside =
			!justOpened &&
			!ImGui::IsAnyItemActive() &&
			!ImGui::IsAnyItemHovered() &&
			!hovered &&
			ImGui::IsMouseClicked(ImGuiMouseButton_Left);

		if (clickedOutside) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

void Menu::drawShortcutModal() {
	if (openShortcutModal) {
		ImGui::OpenPopup("Keyboard Shortcuts");
		openShortcutModal = false;
	}

	if (ImGui::BeginPopupModal(
		"Keyboard Shortcuts",
		nullptr,
		ModalPopupFlags
	)) {
		bool justOpened = ImGui::IsWindowAppearing();
		static ImGuiKeyChord* editingShortcut = nullptr;
		static std::string shortcutError;

		auto drawShortcutButton = [&](
			const char* label,
			ImGuiKeyChord& shortcut
		) {
			std::string buttonText = shortcutButtonLabel(
				label,
				shortcut,
				editingShortcut == &shortcut
			);

			if (ImGui::Button(buttonText.c_str(), ImVec2(220.0f, 0.0f))) {
				editingShortcut = &shortcut;
			}
		};

		drawShortcutButton("Select Tool", selectToolShortcut);
		drawShortcutButton("Ruler Tool", rulerToolShortcut);
		drawShortcutButton("Trim Tool", trimToolShortcut);
		drawShortcutButton("Erase Tool", eraseToolShortcut);
		drawShortcutButton("Line Tool", lineToolShortcut);
		drawShortcutButton("Rectangle Tool", rectangleToolShortcut);
		drawShortcutButton("Circle Tool", circleToolShortcut);

		ImGui::Separator();

		drawShortcutButton("Reset View", resetViewShortcut);

		// Undo / Redo / Copy / Paste are deliberately not rebindable -- they keep
		// the platform-standard Ctrl+Z/Y/C/V. They stay in Shortcuts::allShortcuts
		// so the tool shortcuts above still fail to bind onto them.

		if (editingShortcut) {
			ImGuiKeyChord capturedShortcut = 0;

			if (captureShortcut(capturedShortcut)) {
				if (!shortcutExists(capturedShortcut, editingShortcut)) {
					*editingShortcut = capturedShortcut;
					editingShortcut = nullptr;
					shortcutError.clear();
				}
				else {
					shortcutError = "Shortcut already used";
				}
			}

			if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
				editingShortcut = nullptr;
				shortcutError.clear();
			}
		}

		if (!shortcutError.empty()) {
			ImGui::TextColored(
				ImVec4(1.0f, 0.2f, 0.2f, 1.0f),
				"%s",
				shortcutError.c_str()
			);
		}

		if (ImGui::Button("Close")) {
			ImGui::CloseCurrentPopup();
		}

		ImGui::SameLine();

		if (ImGui::Button("Reset To Default")) {
			resetShortcutsToDefault();
		}

		bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
		bool clickedOutside =
			!justOpened &&
			editingShortcut == nullptr &&
			!ImGui::IsAnyItemActive() &&
			!ImGui::IsAnyItemHovered() &&
			!hovered &&
			ImGui::IsMouseClicked(ImGuiMouseButton_Left);

		if (clickedOutside) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}



void Menu::drawUnitsModal() {
	if (openUnitsModal) {
		ImGui::OpenPopup("Units");
		openUnitsModal = false;
	}

	if (ImGui::BeginPopupModal(
		"Units",
		nullptr,
		ModalPopupFlags
	)) {
		bool justOpened = ImGui::IsWindowAppearing();

		VariableUnits& u = project.solver.varUnits;

		// Set by any row whose dropdown is open this frame. A combo's dropdown is
		// its own root window, not a child of this modal, so clicks inside it read
		// as "outside" to the click-away test below and would close the whole
		// modal the moment a unit is picked.
		bool comboOpen = false;

		// one label + unit dropdown row; works for any Units table (UnitOption
		// or LinearUnitOption, both expose .name).
		auto unitRow = [&](
			const char* label,
			std::uint8_t& index,
			const auto& table
		) {
			if (index >= table.size()) {
				index = 0;
			}

			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(label);
			ImGui::SameLine(220.0f);
			ImGui::SetNextItemWidth(170.0f);

			ImGui::PushID(label);
			if (ImGui::BeginCombo("##unit", table[index].name)) {
				comboOpen = true;
				for (int i = 0; i < (int)(table.size()); i++) {
					bool isSelected = index == i;
					if (ImGui::Selectable(table[i].name, isSelected)) {
						index = (std::uint8_t)(i);
					}
					if (isSelected) {
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
			ImGui::PopID();
		};

		// the project length scale keeps an index AND a display scale in sync,
		// so it needs its own row rather than the generic unitRow above.
		auto lengthScaleRow = [&](const char* label) {
			LengthScale& ls = project.lengthScale;
			if (ls.index >= Units::lengthUnits.size()) {
				ls.index = 0;
			}

			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(label);
			ImGui::SameLine(220.0f);
			ImGui::SetNextItemWidth(170.0f);

			ImGui::PushID(label);
			if (ImGui::BeginCombo("##unit", Units::lengthUnits[ls.index].name)) {
				comboOpen = true;
				for (int i = 0; i < (int)(Units::lengthUnits.size()); i++) {
					bool isSelected = ls.index == i;
					if (ImGui::Selectable(Units::lengthUnits[i].name, isSelected)) {
						ls.index = (std::uint8_t)(i);
						ls.value = 1.0 / Units::lengthUnits[i].toBase;
					}
					if (isSelected) {
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
			ImGui::PopID();
		};

		ImGui::SeparatorText("Geometry");
		lengthScaleRow("Length");

		ImGui::SeparatorText("Flow field");
		unitRow("Axial velocity", u.axialUnit, Units::velocityUnits);
		unitRow("Radial velocity", u.radialUnit, Units::velocityUnits);
		unitRow("Pressure", u.pressureUnit, Units::pressureUnits);
		unitRow("Temperature", u.temperatureUnit, Units::temperatureUnits);

		ImGui::SeparatorText("Species");
		unitRow("Concentration", u.concentrationUnit, Units::concentrationUnits);
		unitRow("Diffusion coefficient", u.DUnit, Units::diffusionCoefficientUnits);
		unitRow("Vmax", u.VmaxUnit, Units::VmaxUnits);

		ImGui::SeparatorText("Material");
		unitRow("Density", u.rhoUnit, Units::densityUnits);
		unitRow("Dynamic viscosity", u.muUnit, Units::dynamicViscosityUnits);
		unitRow("Specific heat", u.specificHeatUnit, Units::specificHeatUnits);
		unitRow("Thermal conductivity", u.heatCondUnit, Units::thermalConductivityUnits);

		ImGui::Separator();

		if (ImGui::Button("Close")) {
			ImGui::CloseCurrentPopup();
		}

		ImGui::SameLine();

		if (ImGui::Button("Reset To Default")) {
			u = VariableUnits{};
			project.lengthScale = LengthScale{};
		}

		bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
		bool clickedOutside =
			!justOpened &&
			!comboOpen &&
			!ImGui::IsAnyItemActive() &&
			!ImGui::IsAnyItemHovered() &&
			!hovered &&
			ImGui::IsMouseClicked(ImGuiMouseButton_Left);

		if (clickedOutside) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

// The leaves under each page branch. These labels ARE the section identity: the
// tree draws them and beginAdvancedSection() matches against them, so one here
// must match the string its drawAdvanced*Page() passes, or the section is drawn
// under the branch but has no leaf that reaches it on its own.
static const char* const advancedSolverSections[] = {
	"Relaxation Factors",
	"Multigrid",
	"Residuals",
	"Additional Terms",
	nullptr
};

static const char* const advancedResultsSections[] = {
	"Animation",
	"Camera",
	nullptr
};

// Nav items take the hand cursor -- the same rule the setup-tab trees follow.
// Menu is not a BaseGUI, so BaseGUI::changeCursorOnHover() is out of reach.
static void advancedNavCursor() {
	if (ImGui::IsItemHovered()) {
		ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
	}
}

void Menu::drawAdvancedNav() {

	ImGui::BeginChild(
		"##AdvancedNav",
		ImVec2(advancedNavWidth, advancedBodyHeight),
		ImGuiChildFlags_Borders
	);

	drawAdvancedBranch("Geometry", AdvancedPage::Geometry, nullptr);
	drawAdvancedBranch("Mesh", AdvancedPage::Mesh, nullptr);
	drawAdvancedBranch("Solver", AdvancedPage::Solver, advancedSolverSections);
	drawAdvancedBranch("Results", AdvancedPage::Results, advancedResultsSections);

	ImGui::EndChild();
}

void Menu::drawAdvancedBranch(const char* label, AdvancedPage page, const char* const* sections) {

	// selected = the branch itself, which is a different selection from any of
	// its leaves: it draws every section on the page rather than one
	const bool selected = advancedPage == page && advancedSection == nullptr;

	// A page with no sections gets LeafFlags so it shows no arrow to open onto
	// nothing. That also means NoTreePushOnOpen, hence no TreePop for it below.
	ImGuiTreeNodeFlags branchFlags = sections
		? UITreeFlags::BranchOpenedFlags
		: UITreeFlags::LeafFlags;

	if (selected) {
		branchFlags |= ImGuiTreeNodeFlags_Selected;
	}

	const bool open = ImGui::TreeNodeEx(label, branchFlags);
	advancedNavCursor();

	// OpenOnArrow puts selection on the label and open/close on the arrow. The
	// toggle test is what keeps a click on the arrow from also selecting.
	if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
		advancedPage = page;
		advancedSection = nullptr;
	}

	if (!sections || !open) {
		return;
	}

	for (const char* const* section = sections; *section; ++section) {

		ImGuiTreeNodeFlags leafFlags = UITreeFlags::LeafFlags;

		if (advancedPage == page &&
			advancedSection &&
			std::strcmp(advancedSection, *section) == 0) {
			leafFlags |= ImGuiTreeNodeFlags_Selected;
		}

		ImGui::TreeNodeEx(*section, leafFlags);
		advancedNavCursor();

		if (ImGui::IsItemClicked()) {
			advancedPage = page;
			advancedSection = *section;
		}
	}

	ImGui::TreePop();
}

void Menu::beginAdvancedPageBody() {
	ImGui::BeginChild(
		"##PageBody",
		ImVec2(advancedBodyWidth, advancedBodyHeight),
		ImGuiChildFlags_Borders
	);
}

void Menu::endAdvancedPageBody() {
	ImGui::EndChild();
}

bool Menu::advancedSectionVisible(const char* label) const {

	// no leaf selected means the page branch is, and that shows all of them
	return advancedSection == nullptr || std::strcmp(advancedSection, label) == 0;
}

bool Menu::beginAdvancedSection(const char* label, int col) {

	if (!advancedSectionVisible(label)) {
		return false;
	}

	// A plain title rather than a CollapsingHeader: what used to be expanded and
	// collapsed here is now picked from the tree on the left, and a header that
	// both selected and collapsed would be two ways to hide the same rows.
	ImGui::SeparatorText(label);

	// The table ID comes off the ID stack, so pushing the section label is what
	// keeps two sections from sharing (and fighting over) one table.
	ImGui::PushID(label);
	advancedTableIndex = -1;

	if (col >= 2) {
		advancedTable(col);
	}
	return true;
}

void Menu::advancedTable(int col) {
	if (col < 2) {
		return;
	}

	if (advancedTableIndex >= 0) {
		ImGui::EndTable();
	}
	advancedTableIndex++;

	// Sections hold more than one table, so the index is what separates them --
	// same string ID twice under one section would be one table with two halves.
	std::string tableID = "##Settings" + std::to_string(advancedTableIndex);
	if (!ImGui::BeginTable(tableID.c_str(), col, UIFlags::TableSimpleFlags | ImGuiTableFlags_NoSavedSettings)) {
		advancedTableIndex--;
		return;
	}

	// Same column setup as BaseGUI::beginPropertyTable: width 0 + WidthFixed
	// auto-fits the label column to its widest label so nothing is ever clipped,
	// and the value columns share whatever is left.
	ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 0.0f);
	for (int i = 1; i < col; ++i) {
		ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch, 1.0f);
	}
}

void Menu::endAdvancedSection() {
	if (advancedTableIndex >= 0) {
		ImGui::EndTable();
		advancedTableIndex = -1;
	}
	ImGui::PopID();
	ImGui::Spacing();
}

void Menu::advancedRow(const char* label) {
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted(label);

	advancedCell();
}

void Menu::advancedCell() {
	ImGui::TableNextColumn();
	ImGui::SetNextItemWidth(-FLT_MIN);
	ImGui::AlignTextToFramePadding();
}

void Menu::advancedEmptyPage(const char* what) {
	ImGui::TextDisabled("No advanced %s options yet.", what);
}

void Menu::drawAdvancedGeometryPage() {

	//if (beginAdvancedSection("Sketch")) {
	//	advancedRow("Some setting");
	//	ImGui::InputInt("##someSetting", &value, 0, 0);
	//
	//	// a second table under the same header, this one 3 columns wide
	//	advancedTable(3);
	//	advancedRow("Some pair");
	//	ImGui::InputInt("##someMin", &min, 0, 0);
	//	advancedCell();
	//	ImGui::InputInt("##someMax", &max, 0, 0);
	//
	//	endAdvancedSection();
	//}
	advancedEmptyPage("geometry");
}

void Menu::drawAdvancedMeshPage() {

	//if (beginAdvancedSection()) {

	//}
	advancedEmptyPage("mesh");
}

void Menu::drawAdvancedSolverPage() {

	Solver& solver = project.solver;

	if (beginAdvancedSection("Relaxation Factors")) {
		advancedRow("Momentum");
		ImGui::InputDouble("##MomentumRelaxation", &solver.simple.momentumRelaxation, 0, 0, "%.1f");
		advancedRow("Pressure");
		ImGui::InputDouble("##PressureRelaxation", &solver.simple.pressureRelaxation, 0, 0, "%.1f");
		advancedRow("Pressure Correction");
		ImGui::InputDouble("##CorrectionRelaxation", &solver.simple.correctionRelaxation, 0, 0, "%.1f");
		endAdvancedSection();
	}

	if (beginAdvancedSection("Multigrid")) {

		const bool graphPrepared = solver.solverRunning;

		ImGui::BeginDisabled(graphPrepared);
		advancedRow("Multigrid");
		ImGui::Checkbox("##Multigrid", &solver.configSolver.useMultigrid);
		advancedRow("Max Multigrid Iteration");
		ImGui::InputInt("##MaxIteration", &solver.configMultigrid.maxIter);
		advancedRow("Max Linear Solver Iteration");
		ImGui::InputInt("##MaxLinearSolverIteration", &solver.configMultigrid.linearSweep);
		advancedRow("Pre Restriction Linear Solver Iteration");
		ImGui::InputInt("##PreSweep", &solver.configMultigrid.linearPreSweep);
		advancedRow("Post Prolongation Linear Solver Iteration");
		ImGui::InputInt("##PostSweep", &solver.configMultigrid.linearPostSweep);
		ImGui::EndDisabled();

		if (!graphPrepared && project.solver.configMultigrid.maxIter < 1) {
			project.solver.configMultigrid.maxIter = 1;
		}

		if (!graphPrepared && project.solver.configMultigrid.linearSweep < 0) {
			project.solver.configMultigrid.linearSweep = 0;
		}

		endAdvancedSection();
	}

	// Norm and scaling per residual, moved out of the Solver tab: that table keeps
	// the per-run knobs (plot, type, tolerance), these two are set once. Headers
	// carry the meaning of the columns here, so the table is built directly rather
	// than through advancedTable().
	if (beginAdvancedSection("Residuals", 0)) {

		// Same visibility rule as the Solver tab's table -- a residual only shows
		// when its field is being solved. Continuity has no norm/scaling at all.
		auto rowVisible = [&](const char* name) -> bool {
			if (std::strcmp(name, "Continuity") == 0)    return false;
			if (std::strcmp(name, "Temperature") == 0)   return solver.fieldOption.solveEnergy;
			if (std::strcmp(name, "Concentration") == 0) return solver.fieldOption.solveConcentration;
			return true;
		};

		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8.0f, 5.0f));
		if (ImGui::BeginTable("Residual Advanced", 3, UIFlags::TableSimpleFlags | ImGuiTableFlags_NoSavedSettings)) {

			ImGui::TableSetupColumn("Residual", ImGuiTableColumnFlags_WidthFixed, 0.0f);
			ImGui::TableSetupColumn("Norm (Numerator)", ImGuiTableColumnFlags_WidthStretch, 1.0f);
			ImGui::TableSetupColumn("Scaling (Denominator)", ImGuiTableColumnFlags_WidthStretch, 1.0f);
			ImGui::TableHeadersRow();

			for (const char*& name : solver.residualPlotType) {
				if (!rowVisible(name)) {
					continue;
				}

				ConfigResidual& configResidual = solver.cfg.at(name);

				ImGui::TableNextRow();
				ImGui::PushID(name);

				ImGui::TableSetColumnIndex(0);
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted(name);

				ImGui::TableSetColumnIndex(1);
				BaseGUI::createSimpleCombo(
					"##ResidualNorm",
					solver.residualNormType,
					configResidual.normType,
					IM_ARRAYSIZE(solver.residualNormType)
				);

				ImGui::TableSetColumnIndex(2);
				BaseGUI::createSimpleCombo(
					"##ResidualScaling",
					solver.residualScalingType,
					configResidual.scaleType,
					IM_ARRAYSIZE(solver.residualScalingType)
				);

				ImGui::PopID();
			}

			ImGui::EndTable();
		}
		ImGui::PopStyleVar();

		endAdvancedSection();
	}

	if (beginAdvancedSection("Additional Terms")) {

		const bool orthogonalMesh = project.mesh.currentMeshType == MeshType::Structured;
		if (orthogonalMesh) {
			solver.configSolver.useNonOrthCorrector = false;
		}

		advancedRow("Non-Orthogonal Corrector");
		ImGui::BeginDisabled(orthogonalMesh);
		ImGui::Checkbox("##NonOrthCorrector", &project.solver.configSolver.useNonOrthCorrector);
		ImGui::EndDisabled();
		endAdvancedSection();
	}
}

void Menu::drawAdvancedResultsPage() {

	Results& results = project.results;

	if (beginAdvancedSection("Animation")) {
		advancedRow("Max stored frames");
		if (ImGui::InputInt("##MaxTimeFrames", &project.solver.maxTimeFrames, 0, 0)) {
			project.solver.maxTimeFrames = std::max(1, project.solver.maxTimeFrames);
		}
		ImGui::SetItemTooltip(
			"Hard cap on transient frames kept in memory. Each frame costs "
			"(fields x cells) doubles on the host; hitting the cap stops capture, "
			"not the solve."
		);

		endAdvancedSection();
	}

	if (beginAdvancedSection("Camera")) {

		// no (int&) cast on the enums -- these are uint8_t-backed, so that would
		// write four bytes over a one-byte field. The enum overload round-trips
		// through a local int instead.
		advancedRow("Camera Rotation");
		if (BaseGUI::createSimpleCombo("##RotationType", results.cameraRotationType, results.rotationType, IM_ARRAYSIZE(results.cameraRotationType))) {
			gui.scene.camera.rotationType = results.rotationType;
		}

		advancedRow("Camera Projection");
		if (BaseGUI::createSimpleCombo("##ProjectionType", results.cameraProjectionType, results.projectionType, IM_ARRAYSIZE(results.cameraProjectionType))) {
			gui.scene.camera.projectionType = results.projectionType;
		}

		endAdvancedSection();
	}
}

void Menu::drawAdvancedOptionModal() {
	if (openAdvancedOptionsModal) {
		ImGui::OpenPopup("Advanced Options");
		openAdvancedOptionsModal = false;
	}

	// Modals are the one popup kind ImGui does not place for you (see Begin(): the
	// fallback positioning explicitly skips them), so centering is on us. Pivot
	// 0.5,0.5 puts the window's own center on the viewport's, which is what makes
	// it work with AlwaysAutoResize -- the size is not known here. Appearing, not
	// Always, is enough: ModalPopupFlags carries NoMove, so it cannot drift.
	// Unconditional is safe -- BeginPopupModal clears the request when not open.
	ImGui::SetNextWindowPos(
		ImGui::GetMainViewport()->GetCenter(),
		ImGuiCond_Appearing,
		ImVec2(0.5f, 0.5f)
	);

	if (ImGui::BeginPopupModal(
		"Advanced Options",
		nullptr,
		ModalPopupFlags
	)) {
		bool justOpened = ImGui::IsWindowAppearing();

		// Category list on the left, the selected page's settings on the right --
		// the Visual Studio Options dialog's arrangement. Only the selected page is
		// submitted, so an ID pushed by one page can never collide with another's.
		drawAdvancedNav();

		ImGui::SameLine();

		beginAdvancedPageBody();
		switch (advancedPage) {
		case AdvancedPage::Geometry: drawAdvancedGeometryPage(); break;
		case AdvancedPage::Mesh:     drawAdvancedMeshPage();     break;
		case AdvancedPage::Solver:   drawAdvancedSolverPage();   break;
		case AdvancedPage::Results:  drawAdvancedResultsPage();  break;
		}
		endAdvancedPageBody();

		ImGui::Separator();

		// Button parked at the bottom right, under the body pane. Measured from the
		// content region rather than the two pane widths so it stays put if either
		// pane is resized.
		const float closeButtonWidth = 88.0f;
		ImGui::SetCursorPosX(
			ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - closeButtonWidth
		);

		if (ImGui::Button("Close", ImVec2(closeButtonWidth, 0.0f))) {
			ImGui::CloseCurrentPopup();
		}

		// A combo (or any nested popup) opened from a settings row is its own root
		// window, so a click inside it reads as "outside" to the test below and
		// would close the whole modal. Anything stacked above this popup suppresses
		// the test -- drawUnitsModal hits the same trap with a per-row flag.
		const ImGuiContext& g = *ImGui::GetCurrentContext();
		bool stackedPopupOpen = g.OpenPopupStack.Size > g.BeginPopupStack.Size;

		bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
		bool clickedOutside =
			!justOpened &&
			!stackedPopupOpen &&
			!ImGui::IsAnyItemActive() &&
			!ImGui::IsAnyItemHovered() &&
			!hovered &&
			ImGui::IsMouseClicked(ImGuiMouseButton_Left);

		if (clickedOutside) {
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}
}

void Menu::render() {
	if (ImGui::BeginMainMenuBar()) {
		if (ImGui::BeginMenu("File")) {
			drawNew();
			drawOpen();
			drawSave();
			drawExport();
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Edit")) {
			drawEditShortcut();
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("View")) {

			drawView();
			ImGui::EndMenu();
		}
		ImGui::EndMainMenuBar();
	}

	drawShortcutModal();
	drawSnappingModal();
	drawUnitsModal();
	drawAdvancedOptionModal();
}
