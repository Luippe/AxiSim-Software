#include "menu.h"

#include <string>

#include "imgui_internal.h"

#include "file_manager.h"
#include "keyboard_manager.h"
#include "unit_manager.h"

using namespace Shortcuts;

Menu::Menu(Project& project) :
	project(project) {
	loadAtLaunch(project, settings);
};


void Menu::drawOpen() {
	if (ImGui::MenuItem("Open Project")) {
		loadFromExplorerProject(project);
	}
}

void Menu::drawOpenAtLaunch() {
	if (ImGui::MenuItem("Open Current Project At Startup")) {
		saveSettings(project, settings);
	}
}

void Menu::drawSave() {

	if (ImGui::MenuItem("Save")) {
		if (!project.name.empty()) {
			saveFromPathProject(project.path, project);
		}
		else {
			saveFromExplorerProject(project);
		}
	}

	if (ImGui::MenuItem("Save As")) {
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

	if (ImGui::MenuItem("Keyboard Shortcuts")) {
		openShortcutModal = true;
	}

	if (ImGui::MenuItem("Units")) {
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
		drawShortcutButton("Undo", undoShortcut);
		drawShortcutButton("Redo", redoShortcut);

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
			drawOpen();
			drawOpenAtLaunch();
			drawSave();
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Edit")) {
			drawEditShortcut();
			ImGui::EndMenu();
		}
		ImGui::EndMainMenuBar();
	}

	drawShortcutModal();
	drawUnitsModal();
}
