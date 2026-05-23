#include "solver_gui.h"
#include "scene_view.h"		// must be in front of graphics_struct.h
#include "solver.h"

#include "graphics_struct.h"
#include "solver_struct.h"

#include "gui_manager.h"
#include "unit_manager.h"
#include "printer.h"

SolverGUI::SolverGUI(SceneView& scene) :
	scene(scene),
	solver(scene.solver){
}

void SolverGUI::setResidualDefault() {
	switch (solver.currentResidual) {
	case RESIDUAL_RAW:
		solver.currentResidualNorm = RESIDUAL_LINF;
		solver.currentResidualScaling = RESIDUAL_SCALING_NONE;
		break;
	case RESIDUAL_RMS:
		solver.currentResidualNorm = RESIDUAL_L2;
		solver.currentResidualScaling = RESIDUAL_SCALING_SQRT_N;
		break;
	}
}

void SolverGUI::drawPropertiesPanel() {

	ImGui::Begin("Overview");

	if (selectedItem == "Solver Settings") {
		ImGui::SeparatorText("Solver Settings");

		if (ImGui::BeginTable("Geometry", 2)) {

			ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 70.0f);
			ImGui::TableSetupColumn("Combo", ImGuiTableColumnFlags_WidthFixed, 120.0f);

			textAtNewRow("Solver", 0, 1);
			createSimpleCombo("##Solver", solver.velocitySolverType, (int&)solver.currentVelocitySolver, IM_ARRAYSIZE(solver.velocitySolverType));

			textAtNewRow("Linear Solver", 0, 1);
			createSimpleCombo("##LinearSolverType", solver.linearSolverType, (int&)(solver.linearSolverConfig.type), IM_ARRAYSIZE(solver.linearSolverType));
			
			textAtNewRow("Convection Discretization Scheme", 0, 1);
			createSimpleCombo("##ConvectionScheme", solver.convectionDiscretizationType, (int&)(solver.convectionScheme), IM_ARRAYSIZE(solver.convectionDiscretizationType));

			textAtNewRow("Add Convection Term", 0, 1);
			ImGui::Checkbox("##ConvectionTerm", &solver.addConvectionTerm);
			
			textAtNewRow("Transient", 0, 1);
			ImGui::Checkbox("##TransientTerm", &solver.transient);
			ImGui::EndTable();

			ImGui::Dummy(ImVec2(0.0f, 30.0f));
			ImGui::SeparatorText("Add Solvers");
			ImGui::Spacing();

		}
	}
	else if (selectedItem == "Velocity BC") {
		ImGui::SeparatorText("Velocity BC");

		if (ImGui::BeginTable("Boundary Conditions", 3)) {

			ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 100.0f);
			ImGui::TableSetupColumn("BC Type", ImGuiTableColumnFlags_WidthFixed, 200.0f);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 200.0f);

			textAtNewRow("Inlet", 0, 1);
			createSimpleCombo("##InletBCType", solver.bcInletTypeNames, (int&)(solver.uBC.inlet.type), IM_ARRAYSIZE(solver.bcInletTypeNames));

			tableNextColumn();
			createInputDouble("##InletBCValue", &solver.uBC.inlet.val);

			ImGui::EndTable();
		}

	}
	else if (selectedItem == "Pressure BC") {
		ImGui::SeparatorText("Pressure BC");

		if (ImGui::BeginTable("Boundary Conditions", 3)) {

			ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 100.0f);
			ImGui::TableSetupColumn("BC Type", ImGuiTableColumnFlags_WidthFixed, 200.0f);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 200.0f);

			textAtNewRow("Outlet", 0, 1);
			createSimpleCombo("##OutletBCType", solver.bcTypeNames, (int&)(solver.pBC.outlet.type), IM_ARRAYSIZE(solver.bcTypeNames));

			tableNextColumn();
			createInputDouble("##OutletBCValue", &solver.pBC.outlet.val);

			ImGui::EndTable();
		}
	}
	else if (selectedItem == "Residuals") {
		ImGui::SeparatorText("Residual Type");

		if (ImGui::BeginTable("Residual Type Settings", 3)) {

			ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 300.0f);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 200.0f);
			ImGui::TableSetupColumn("Advanced", ImGuiTableColumnFlags_WidthFixed, 50.0f);

			textAtNewRow("Residual Type", 0, 1);
			if (createSimpleCombo("##ResidualType", solver.residualType, (int&)solver.currentResidual, IM_ARRAYSIZE(solver.residualType))) {
				setResidualDefault();
			}

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

		ImGui::SeparatorText("Plot Residuals");

		if (ImGui::BeginTable("Plot Residuals", 3)) {

			ImGui::TableSetupColumn("Label1", ImGuiTableColumnFlags_WidthFixed, 100.0f);
			ImGui::TableSetupColumn("Label2", ImGuiTableColumnFlags_WidthFixed, 100.0f);
			ImGui::TableSetupColumn("Label3", ImGuiTableColumnFlags_WidthFixed, 100.0f);

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Checkbox("U", &solver.enabledResiduals.plotU);
			ImGui::TableNextColumn();
			ImGui::Checkbox("V", &solver.enabledResiduals.plotV);
			ImGui::TableNextColumn();
			ImGui::Checkbox("P", &solver.enabledResiduals.plotP);
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Checkbox("Continuity", &solver.enabledResiduals.plotCont);
			ImGui::TableNextColumn();
			ImGui::Checkbox("Temperature", &solver.enabledResiduals.plotTemp);
			ImGui::TableNextColumn();
			ImGui::Checkbox("Concentration", &solver.enabledResiduals.plotConc);

			ImGui::EndTable();
		}

	}
	else if (selectedItem == "Tolerance") {
		ImGui::SeparatorText("Tolerance");

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
			
				textAtNewRow("Linear Solver Max Iteration", 0, 1);
				ImGui::InputInt("##LinearSolverIteration", &scene.solver.linearSolverConfig.maxIter, 0.0, 0.0);
			
			}
			ImGui::EndTable();
		}
	}
	else if (selectedItem == "Fluid Settings") {
		ImGui::SeparatorText("Fluid Settings");
		if (ImGui::BeginTable("Fluid Settings", 3)) {
			inputDoubleWithUnits("Density", solver.f.rho, solver.varUnits.rhoUnit, Units::densityUnits);
			inputDoubleWithUnits("Dynamic Viscosity", solver.f.mu, solver.varUnits.muUnit, Units::dynamicViscosityUnits);
			inputDoubleWithUnits("Diffusion Coefficient", solver.f.D, solver.varUnits.DUnit, Units::diffusionCoefficientUnits);
			ImGui::EndTable();
		}


	}
	else if (selectedItem == "Transient Settings") {
		ImGui::SeparatorText("Transient Settings");

		if (ImGui::BeginTable("Transient Settings", 2)) {
			ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 300.0f);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 200.0f);
			
			textAtNewRow("dt", 0, 1);
			ImGui::InputDouble("##timeStep", &scene.solver.dt, 0.0, 0.0, "%.3f");

			textAtNewRow("tEnd", 0, 1);
			ImGui::InputDouble("##endTime", &scene.solver.tEnd, 0.0, 0.0, "%.3f");

			textAtNewRow("Save keyframe every # Iterations", 0, 1);
			ImGui::InputInt("##saveKeyFrameIter", &scene.solver.saveKeyFrameIter, 0.0, 0.0);

			ImGui::EndTable();
		}
	}
	ImGui::End();
}

void SolverGUI::draw() {
	if (ImGui::BeginTabItem("Solver")) {
		scene.currentTab = TAB_SOLVER;

		ImGui::BeginChild("SetupTree", ImVec2(260, 600), true);

		if (ImGui::TreeNodeEx("General", UIFlags::BranchFlags)) {
			drawLeaf("Solver Settings");
			ImGui::TreePop();
		}
		changeCursorOnHover();

		if (ImGui::TreeNodeEx("Boundary Conditions", UIFlags::BranchFlags)) {
			drawLeaf("Velocity BC");
			drawLeaf("Pressure BC");
			ImGui::TreePop();
		}
		changeCursorOnHover();

		if (ImGui::TreeNodeEx("Convergence", UIFlags::BranchFlags)) {
			drawLeaf("Residuals");
			drawLeaf("Tolerance");
			ImGui::TreePop();
		}
		changeCursorOnHover();
		
		if (ImGui::TreeNodeEx("Fluid Properties", UIFlags::BranchFlags)) {
			drawLeaf("Fluid Settings");
			ImGui::TreePop();
		}
		changeCursorOnHover();

		if (solver.transient) {
			if (ImGui::TreeNodeEx("Transient", UIFlags::BranchFlags)) {
				drawLeaf("Transient Settings");
				ImGui::TreePop();
			}
		}
		changeCursorOnHover();

		ImGui::EndChild();



		ImGui::Checkbox("Continue Solver", &solver.continueSolver);

		if (ImGui::Button("Start Solver")) {
			scene.solver.run();
		}
		changeCursorOnHover();

		drawPropertiesPanel();

		ImGui::EndTabItem();

	}
}