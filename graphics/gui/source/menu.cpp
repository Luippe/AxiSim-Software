#include "menu.h"

#include <string>

#include "imgui_internal.h"

#include "project.h"

#include "file_manager.h"
#include "keyboard_manager.h"

using namespace Shortcuts;

Menu::Menu(Project& project) :
	project(project) {
	loadAtLaunch(project);
};


void Menu::drawOpen() {
	if (ImGui::BeginMenu("Open")) {
		if (ImGui::MenuItem("Project")) {
			loadFromExplorerProject(project);
		}

		ImGui::EndMenu();
	}
}

void Menu::drawOpenAtLaunch() {
	if (ImGui::BeginMenu("Open At Launch")) {

		if (ImGui::MenuItem("Project")) {
			saveLaunchProject(project);
		}

		ImGui::EndMenu();
	}
}

void Menu::drawSave() {
	if (ImGui::BeginMenu("Save")) {

		if (ImGui::MenuItem("Save Entire Project")) {
			saveFromExplorerProject(project);
		}

		ImGui::EndMenu();
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
	if (ImGui::BeginMenu("Edit")) {
		if (ImGui::MenuItem("Keyboard Shortcuts")) {
			openShortcutModal = true;
		}

		ImGui::EndMenu();
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



void Menu::render() {
	if (ImGui::BeginMainMenuBar()) {
		if (ImGui::BeginMenu("File")) {
			drawOpen();
			drawOpenAtLaunch();
			drawSave();
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Option")) {
			drawEditShortcut();
			ImGui::EndMenu();
		}
		ImGui::EndMainMenuBar();
	}

	drawShortcutModal();
}
