#include "menu.h"

#include <filesystem>
#include <string>

#include "gui.h"
#include "imgui_internal.h"

#include "file_manager.h"
#include "keyboard_manager.h"
#include "unit_manager.h"

using namespace Shortcuts;
using namespace Snapping;

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

		ImGui::EndMenu();
	}
}

void Menu::drawView() {

	// Checked = the panels are showing. Unchecking leaves only the live viewport;
	// the GUI picks the flag up on the next frame, not this one.
	if (menuItem("GUI", nullptr, !project.simpleView)) {
		project.simpleView = !project.simpleView;
	}

	ImGui::Separator();

	// What the Results scene draws on top of the solution itself.
	if (beginMenu("Results")) {

		SceneView& scene = gui.scene;

		// Perspective reads depth better; orthographic keeps parallel lines
		// parallel, which is what you want when comparing sizes across the
		// domain. Both are framed from the same view height, so switching does
		// not change how big the scene looks.
		if (beginMenu("Projection")) {

			const bool perspective = scene.camera.projectionType == ProjectionType::Perspective;

			// the camera holds the live value and project.sceneView is the copy
			// that gets saved, so both move together
			if (menuItem("Perspective", nullptr, perspective)) {
				scene.camera.projectionType = ProjectionType::Perspective;
				project.sceneView.projection = SceneViewSettings::Perspective;
			}

			if (menuItem("Orthographic", nullptr, !perspective)) {
				scene.camera.projectionType = ProjectionType::Orthographic;
				project.sceneView.projection = SceneViewSettings::Orthographic;
			}

			ImGui::EndMenu();
		}

		// What a middle-drag in the scene does. Neither one clamps, so both
		// reach every orientation -- the difference is roll. Switching is
		// jump-free; it only changes what the next drag does.
		if (beginMenu("Rotation")) {

			const bool turntable = scene.camera.rotationStyle == RotationStyle::Turntable;

			if (menuItem("Turntable", nullptr, turntable)) {
				scene.camera.rotationStyle = RotationStyle::Turntable;
				project.sceneView.rotationStyle = SceneViewSettings::Turntable;
			}

			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip(
					"Yaw about up, pitch about the screen's horizontal.\n"
					"The horizon never tips, and the same drag always does the same thing."
				);
			}

			if (menuItem("Arcball", nullptr, !turntable)) {
				scene.camera.rotationStyle = RotationStyle::Arcball;
				project.sceneView.rotationStyle = SceneViewSettings::Arcball;
			}

			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip(
					"Virtual trackball, as in Fluent. Free tumble in the middle of the\n"
					"viewport, roll near the borders. Reaches rolled views a turntable cannot."
				);
			}

			ImGui::EndMenu();
		}

		ImGui::Separator();

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
		ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove
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
		ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove
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
		ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove
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
}
