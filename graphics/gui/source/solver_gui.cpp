#include "solver_gui.h"
#include "scene_view.h"		// must be in front of graphics_struct.h
#include "solver.h"
#include "graphics_struct.h"
#include "solver_struct.h"

SolverGUI::SolverGUI(SceneView& scene) :
	scene(scene),
	solver(scene.solver){
}

void SolverGUI::draw() {
	if (ImGui::BeginTabItem("Solver")) {

		if (scene.currentTab != TAB_SOLVER) {
			scene.currentTab = TAB_SOLVER;
		}

		if (ImGui::BeginTabBar("Solvers")) {

			if (ImGui::BeginTabItem("Velocity")) {

				if (ImGui::CollapsingHeader("Solver Settings"), ImGuiTreeNodeFlags_DefaultOpen) {

					if (ImGui::BeginTable("Geometry", 2)) {
						ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 70.0f);
						ImGui::TableSetupColumn("Combo", ImGuiTableColumnFlags_WidthFixed, 120.0f);

						textAtNewRow("Solver", 0, 1);
						createSimpleCombo("##Solver", solver.velocitySolverType, (int&)solver.currentVelocitySolver, IM_ARRAYSIZE(solver.velocitySolverType));
						
						ImGui::EndTable();
					}
				}

				if (ImGui::CollapsingHeader("Boundary Conditions"), ImGuiTreeNodeFlags_DefaultOpen) {
					if (ImGui::BeginTable("Boundary Conditions", 2)) {
						ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 100.0f);
						ImGui::TableSetupColumn("Combo", ImGuiTableColumnFlags_WidthFixed, 200.0f);

						textAtNewRow("Field", 0, 1);
						createSimpleCombo("##Field", solver.fieldType, (int&)solver.currentField, IM_ARRAYSIZE(solver.fieldType));
						
						ImGui::EndTable();
					}

					// boundary conditions for Simple method
					if (solver.currentVelocitySolver == VelocitySolverType::SOLVER_SIMPLE) {
						if (solver.currentField == FIELD_AXIAL_VELOCITY) {
							if (ImGui::BeginTable("Boundary Conditions", 3)) {
								ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 100.0f);
								ImGui::TableSetupColumn("BC Type", ImGuiTableColumnFlags_WidthFixed, 200.0f);
								ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 200.0f);

								drawBCCombo("Inlet", solver.uBC.inlet);
								
								ImGui::EndTable();
							}
						}
						else if (solver.currentField == FIELD_PRESSURE) {
							if (ImGui::BeginTable("Boundary Conditions", 3)) {
								ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 100.0f);
								ImGui::TableSetupColumn("BC Type", ImGuiTableColumnFlags_WidthFixed, 200.0f);
								ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 200.0f);

								drawBCCombo("Outlet", solver.pBC.outlet);
								
								ImGui::EndTable();
							}
						}
					}
				}
				if (ImGui::CollapsingHeader("Residuals"), ImGuiTreeNodeFlags_DefaultOpen) {


					// -------------------RESIDUAL TYPE SETTINGS-------------------
					if (ImGui::BeginTable("Residual Type Settings", 3)) {
						ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 300.0f);
						ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 200.0f);
						ImGui::TableSetupColumn("Advanced", ImGuiTableColumnFlags_WidthFixed, 50.0f);

						textAtNewRow("Residual Type", 0, 1);
						createSimpleCombo("##ResidualType", solver.residualType, (int&)solver.currentResidual, IM_ARRAYSIZE(solver.residualType));

						tableNextColumn();
						if (ImGui::SmallButton("...##AdvancedResidualOptions")) {
							ImGui::OpenPopup("Advanced Settings");
						}

						if (ImGui::BeginPopup("Advanced Settings")) {
							if (ImGui::BeginTable("Residual Type Settings", 2)) {
								ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 130.0f);
								ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 130.0f);

								textAtNewRow("Residual Norm Type", 0, 1);
								createSimpleCombo("##ResidualNorm", solver.residualNormType, (int&)solver.currentResidualNorm, IM_ARRAYSIZE(solver.residualNormType));

								textAtNewRow("Residual Scaling", 0, 1);
								createSimpleCombo("##ResidualScaling", solver.residualScalingType, (int&)solver.currentResidualScaling, IM_ARRAYSIZE(solver.residualScalingType));

								ImGui::EndTable();
							}
							ImGui::EndPopup();
						}
						ImGui::EndTable();
					}

					ImGui::Separator();

					// -------------------RESIDUAL AND RESIDUAL SETTINGS-------------------
					if (ImGui::BeginTable("Iteration Settings", 2)) {
						if (solver.currentVelocitySolver == SOLVER_SIMPLE) {
							ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 300.0f);
							ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 150.0f);

							textAtNewRow("Maximum Iterations", 0, 1);
							ImGui::InputInt("##SimpleMaxIter", &scene.solver.configSimple.maxIter, 0.0, 0.0);

							textAtNewRow("Plot Residual Every # Iterations", 0, 1);
							ImGui::InputInt("##SimpleCheckConv", &scene.solver.configSimple.checkConv, 0.0, 0.0);

							textAtNewRow("Momentum Tolerance", 0, 1);
							ImGui::InputDouble("##SimpleMomTol", &scene.solver.configSimple.momTol, 0.0, 0.0, "%.3e");

							textAtNewRow("Continuity Tolerance", 0, 1);
							ImGui::InputDouble("##SimpleContTol", &scene.solver.configSimple.ppTol, 0.0, 0.0, "%.3e");
						}

						ImGui::EndTable();
					}
				}
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Temperature")) {
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Concentration")) {
				ImGui::InputScalar("Inflow Concentration", ImGuiDataType_Double, &scene.solver.concBC.inlet.val);
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}

		if (ImGui::Button("Start Solver")) {
			scene.solver.run();
		}
		changeCursorOnHover();

		ImGui::EndTabItem();
	}
}



void SolverGUI::drawBCCombo(const char* label, BoundaryCondition& bc) {
	ImGui::PushID(label);

	ImGui::TableNextRow();

	// label column
	ImGui::TableSetColumnIndex(0);
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted(label);

	// combo column
	ImGui::TableSetColumnIndex(1);

	int currentType = (int)(bc.type);

	if (ImGui::Combo("##BCType", &currentType, solver.bcTypeNames, IM_ARRAYSIZE(solver.bcTypeNames))) {
		bc.type = (BCType)(currentType);
	}

	// value column
	ImGui::TableSetColumnIndex(2);
	ImGui::SetNextItemWidth(-FLT_MIN);
	ImGui::InputDouble("##BCValue", &bc.val);
	
	ImGui::PopID();
}