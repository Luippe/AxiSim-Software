#include "menu.h"
#include "scene_view.h"
#include "file_manager.h"
#include "gui.h"

Menu::Menu(GUI& gui, SceneView& scene) :
	scene(scene),
	gui(gui){
	loadAtLaunch(scene.mesh, scene.solver, scene.results);
	//scene.solver.setDefault();
}


void Menu::drawOpen() {
	if (ImGui::BeginMenu("Open")) {

		if (ImGui::MenuItem("Mesh")) {
			loadFromExplorerMesh(scene.mesh);
			scene.mesh.updateAfterLoadingFile();
			gui.meshGUI.getGridConfigEdits();
		}

		if (ImGui::MenuItem("Solver")) {
			loadFromExplorerSolver(scene.solver);
		}

		if (ImGui::MenuItem("Results")) {

		}

		ImGui::Separator();

		if (ImGui::MenuItem("Project")) {

		}

		ImGui::EndMenu();
	}
}

void Menu::drawOpenAtLaunch() {
	if (ImGui::BeginMenu("Open At Launch")) {

		if (ImGui::MenuItem("Mesh")) {
			saveLaunchMesh(scene.mesh);
		}

		if (ImGui::MenuItem("Solver")) {
			saveLaunchSolver(scene.solver);
		}

		if (ImGui::MenuItem("Results")) {

		}

		ImGui::Separator();

		if (ImGui::MenuItem("Project")) {

		}

		ImGui::EndMenu();
	}
}

void Menu::drawSave() {
	if (ImGui::BeginMenu("Save")) {
		if (ImGui::MenuItem("Mesh")) {
			saveFromExplorerMesh(scene.mesh);
		}

		if (ImGui::MenuItem("Solver")) {
			saveFromExplorerSolver(scene.solver);
		}

		if (ImGui::MenuItem("Results")) {
			//saveFromExplorerResults(scene.results);
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Save Entire Project")) {

		}

		ImGui::EndMenu();
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

		ImGui::EndMainMenuBar();
	}
}