#include "menu.h"

#include "project.h"
#include "gui.h"

#include "file_manager.h"

Menu::Menu(Project& project, GUI& gui) :
	project(project),
	gui(gui){
	loadAtLaunch(project);
	//loadAtLaunch(project, "solver");
	//scene.solver.setDefault();
}


void Menu::drawOpen() {
	if (ImGui::BeginMenu("Open")) {
		if (ImGui::MenuItem("Project")) {
			loadFromExplorerProject(project);
			project.mesh.updateAfterLoadingFile();
			gui.meshGUI.getGridConfigEdits();
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