#include "menu.h"

#include "project.h"
#include "gui.h"

#include "file_manager.h"

Menu::Menu(Project& project, GUI& gui) :
	project(project),
	gui(gui){
	loadAtLaunch(project, "mesh");
	loadAtLaunch(project, "solver");
	//scene.solver.setDefault();
}


void Menu::drawOpen() {
	if (ImGui::BeginMenu("Open")) {

		if (ImGui::MenuItem("Mesh")) {
			loadFromExplorerMesh(project.mesh);
			project.mesh.updateAfterLoadingFile();
			gui.meshGUI.getGridConfigEdits();
		}

		if (ImGui::MenuItem("Solver")) {
			loadFromExplorerSolver(project.solver);
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
			saveLaunchMesh(project.mesh);
		}

		if (ImGui::MenuItem("Solver")) {
			saveLaunchSolver(project.solver);
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
			saveFromExplorerMesh(project.mesh);
		}

		if (ImGui::MenuItem("Solver")) {
			saveFromExplorerSolver(project.solver);
		}

		if (ImGui::MenuItem("Results")) {
			//saveFromExplorerResults(project.results);
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