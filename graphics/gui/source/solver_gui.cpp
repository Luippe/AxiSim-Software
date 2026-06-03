#include "solver_gui.h"
#include "scene_view.h"		// must be in front of graphics_struct.h
#include "mesh.h"
#include "solver.h"

#include "graphics_struct.h"
#include "solver_struct.h"

#include "flag_manager.h"
#include "unit_manager.h"
#include "printer.h"

#include "boundary_func.h"

SolverGUI::SolverGUI(SceneView& scene) :
	scene(scene),
	mesh(scene.mesh),
	solver(scene.solver),
	varUnits(scene.solver.varUnits) {
}

// ======================================================================
// -----------------------HELPER FUNCTIONS-------------------------------
// ======================================================================
const char* boundaryPropertyToString(BoundaryPropertyID id) {
	switch (id) {
	case BoundaryPropertyID::VelocityMagnitude:
		return "Velocity Magnitude";

	case BoundaryPropertyID::UVelocity:
		return "U Velocity";

	case BoundaryPropertyID::VVelocity:
		return "V Velocity";

	case BoundaryPropertyID::StaticPressure:
		return "Static Pressure";

	case BoundaryPropertyID::StaticTemperature:
		return "Static Temperature";

	case BoundaryPropertyID::Concentration:
		return "Concentration";

	case BoundaryPropertyID::TurbulenceIntensity:
		return "Turbulence Intensity";

	case BoundaryPropertyID::TurbulentViscosityRatio:
		return "Turbulent Viscosity Ratio";

	default:
		return "Unknown";
	}
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

const char* boundaryTypeToString(BoundaryType type) {
	switch (type) {
	case BoundaryType::WALL:
		return "Wall";

	case BoundaryType::VELOCITY_INLET:
		return "Velocity Inlet";

	case BoundaryType::PRESSURE_OUTLET:
		return "Pressure Outlet";

	case BoundaryType::SYMMETRY:
		return "Symmetry";

	default:
		return "Wall";
	}
}

BoundaryCondition& SolverGUI::getOrCreateBC(
	BoundarySegmentGroup& group,
	BoundaryVariable variable
) {
	auto it = group.bcs.find(variable);

	if (it != group.bcs.end()) {
		return it->second;
	}

	BoundaryCondition bc =
		BoundaryDefaults::makeDefaultBC(group.type, variable);

	auto [newIt, inserted] = group.bcs.emplace(variable, bc);

	return newIt->second;
}

void SolverGUI::drawFieldCheckbox() {
	ImGui::Checkbox("Axial Velocity", &solver.fieldOption.solveU);
	ImGui::Checkbox("Radial Velocity", &solver.fieldOption.solveV);
	ImGui::Checkbox("Pressure", &solver.fieldOption.solvePressure);
	ImGui::Checkbox("Energy", &solver.fieldOption.solveEnergy);
	ImGui::Checkbox("Concentration", &solver.fieldOption.solveConcentration);
}

const char* boundaryVariableToString(BoundaryVariable var) {

	switch (var) {
	case BoundaryVariable::UVelocity:
		return "U Velocity";

	case BoundaryVariable::VVelocity:
		return "V Velocity";

	case BoundaryVariable::Pressure:
		return "Pressure";

	case BoundaryVariable::StaticTemperature:
		return "Static Temperature";

	case BoundaryVariable::Concentration:
		return "Concentration";

	case BoundaryVariable::TurbulenceIntensity:
		return "Turbulence Intensity";

	case BoundaryVariable::TurbulentViscosityRatio:
		return "Turbulent Viscosity Ratio";

	default:
		return "Unknown";
	}
}
// ======================================================================
// -------------------BOUNDARY VARIABLE DRAW CALLS-----------------------
// ======================================================================
void SolverGUI::drawBoundaryVariableEditor(BoundaryVariable var, BoundaryCondition& bc) {

	ImGui::SeparatorText(boundaryVariableToString(var));



	const char* label = boundaryVariableToString(var);
	if (ImGui::BeginTable(label, 3)) {
		setupTableColumns(
			column("Label", 100.0f),
			column("Value", 100.0f),
			column("Unit", 100.0f)
		);
		labelRow(label);

		switch (var) {
		case BoundaryVariable::UVelocity:
			inputDoubleWithUnits("U Velocity", bc.value, varUnits.axialUnit, Units::velocityUnits);
			break;
		case BoundaryVariable::VVelocity:

			inputDoubleWithUnits("V Velocity", bc.value, varUnits.radialUnit, Units::velocityUnits);
			break;
		case BoundaryVariable::Pressure:
			inputDoubleWithUnits("Pressure", bc.value, varUnits.pressureUnit, Units::pressureUnits);
			break;
		case BoundaryVariable::StaticTemperature:
			inputDoubleWithUnits("Static Temperature", bc.value, varUnits.temperatureUnit, Units::temperatureUnits);
			break;
		}
		ImGui::EndTable();
	}
}


void SolverGUI::drawBoundaryConditionGUI() {

	BoundarySegmentGroup* selectedGroup =
		mesh.getBoundaryGroupByID(selectedBoundaryGroupID);

	if (!selectedGroup) {
		selectedBoundaryGroupID = -1;
		mesh.highlightedBoundarySegmentIDs.clear();
		return;
	}

	ImGui::SeparatorText(selectedGroup->name.c_str());


	drawTableHeader("Properties");

	if (ImGui::BeginTable("PropertyTable", 2, UIFlags::TableSimpleFlags, ImVec2(0.0f, 120.0f))) {
		setupTableColumns(
			column("Label", 150.0f),
			column("Value", 100.0f, ImGuiTableColumnFlags_WidthStretch)
		);

		labelRow("Type");
		if (createSimpleCombo("##BoundaryType", solver.boundaryType, (int&)selectedGroup->type, IM_ARRAYSIZE(solver.boundaryType))) {
			check();
		}
		ImGui::EndTable();
	}
}



std::vector<BoundaryVariable> SolverGUI::getPhysicsValueLeaves(
	const BoundarySegmentGroup& group
) const {
	std::vector<BoundaryVariable> leaves;

	switch (group.type) {

	case BoundaryType::WALL:
		leaves.push_back(BoundaryVariable::UVelocity);
		leaves.push_back(BoundaryVariable::VVelocity);

		if (solver.fieldOption.solveEnergy) {
			leaves.push_back(BoundaryVariable::StaticTemperature);
		}

		if (solver.fieldOption.solveConcentration) {
			leaves.push_back(BoundaryVariable::Concentration);
		}

		break;

	case BoundaryType::SYMMETRY:
		break;

	case BoundaryType::VELOCITY_INLET:
		leaves.push_back(BoundaryVariable::UVelocity);
		leaves.push_back(BoundaryVariable::VVelocity);

		if (solver.fieldOption.solveEnergy) {
			leaves.push_back(BoundaryVariable::StaticTemperature);
		}

		if (solver.fieldOption.solveConcentration) {
			leaves.push_back(BoundaryVariable::Concentration);
		}

		leaves.push_back(BoundaryVariable::TurbulenceIntensity);
		leaves.push_back(BoundaryVariable::TurbulentViscosityRatio);
		break;

	case BoundaryType::PRESSURE_OUTLET:
		leaves.push_back(BoundaryVariable::Pressure);

		if (solver.fieldOption.solveEnergy) {
			leaves.push_back(BoundaryVariable::StaticTemperature);
		}

		if (solver.fieldOption.solveConcentration) {
			leaves.push_back(BoundaryVariable::Concentration);
		}

		break;
	}



	return leaves;
}

void SolverGUI::drawPropertiesPanel() {

	ImGui::Begin("Overview");

	if (selectedItem == "General") {

		ImGui::TextUnformatted("General");

		drawFieldCheckbox();

	}
	else if (selectedItem == "Solver Settings") {
		ImGui::TextUnformatted("Solver");

		// total width = sum of table width + 10 * num of columns to account for padding
		// total height = number of rows * 31
		ImGui::BeginChild("Solver", ImVec2(220.0f, 62.0f), true);	
		if (ImGui::BeginTable("Solver", 2)) {

			setupTableColumns(
				column("Label", 100.0f),
				column("Combo", 100.0f)
			);

			labelRow("Solver");
			createSimpleCombo("##Solver", solver.velocitySolverType, (int&)solver.currentVelocitySolver, IM_ARRAYSIZE(solver.velocitySolverType));

			labelRow("Linear Solver");
			createSimpleCombo("##LinearSolverType", solver.linearSolverType, (int&)(solver.linearSolverConfig.type), IM_ARRAYSIZE(solver.linearSolverType));

			ImGui::EndTable();
		}
		ImGui::EndChild();

		ImGui::Dummy(ImVec2(0.0f, 10.0f));
		ImGui::TextUnformatted("Options");
		ImGui::BeginChild("Options", ImVec2(380.0f, 120.0f), true);
		if (ImGui::BeginTable("Options", 2)) {

			setupTableColumns(
				column("Label", 200.0f),
				column("Combo", 160.0f)
			);

			labelRow("Convection Discretization");
			createSimpleCombo("##ConvectionScheme", solver.convectionDiscretizationType, (int&)(solver.convectionScheme), IM_ARRAYSIZE(solver.convectionDiscretizationType));

			labelRow("Add Convection Term");
			checkBox("##ConvectionTerm", &solver.addConvectionTerm);

			labelRow("Energy Equation");
			checkBox("##EnergyEquation", &solver.energyEquation);

			labelRow("Transient");
			checkBox("##TransientTerm", &solver.transient);

			ImGui::EndTable();

		}
		ImGui::EndChild();
	}

	else if (selectedItem == "Boundary Variable") {

		BoundarySegmentGroup* group = mesh.getBoundaryGroupByID(selectedBoundaryGroupID);

		if (group) {
			BoundaryCondition& bc = getOrCreateBC(*group, selectedBoundaryVariable);

			drawBoundaryVariableEditor(selectedBoundaryVariable, bc);
		}
	}
	else if (selectedItem == "Boundary Group") {
		drawBoundaryConditionGUI();
	}
	else if (selectedItem == "Residuals") {
		ImGui::SeparatorText("Residual Type");

		if (ImGui::BeginTable("Residual Type Settings", 3)) {

			setupTableColumns(
				column("Label", 300.0f),
				column("Value", 200.0f),
				column("Advanced", 50.0f)
			);

			labelRow("Residual Type");
			if (createSimpleCombo("##ResidualType", solver.residualType, (int&)solver.currentResidual, IM_ARRAYSIZE(solver.residualType))) {
				setResidualDefault();
			}

			tableNextColumn();
			if (ImGui::SmallButton("...##AdvancedResidualOptions")) {
				ImGui::OpenPopup("Advanced Settings");
			}

			if (ImGui::BeginPopup("Advanced Settings")) {
				if (ImGui::BeginTable("Residual Type Settings", 2)) {

					setupTableColumns(
						column("Label", 130.0f),
						column("Value", 100.0f)
					);

					labelRow("Residual Norm Type");
					createSimpleCombo("##ResidualNorm", solver.residualNormType, (int&)solver.currentResidualNorm, IM_ARRAYSIZE(solver.residualNormType));

					labelRow("Residual Scaling");
					createSimpleCombo("##ResidualScaling", solver.residualScalingType, (int&)solver.currentResidualScaling, IM_ARRAYSIZE(solver.residualScalingType));

					ImGui::EndTable();
				}
				ImGui::EndPopup();
			}
			ImGui::EndTable();
		}

		ImGui::SeparatorText("Plot Residuals");

		if (ImGui::BeginTable("Plot Residuals", 3)) {

			setupTableColumns(
				column("Label1", 100.0f),
				column("Label2", 100.0f),
				column("Label3", 100.0f)
			);

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

				setupTableColumns(
					column("Label", 300.0f),
					column("Value", 150.0f)
				);

				labelRow("Maximum Iterations");
				ImGui::InputInt("##SimpleMaxIter", &scene.solver.configSimple.maxIter, 0.0, 0.0);

				labelRow("Plot Residual Every # Iterations");
				inputInt("##SimpleCheckConv", &scene.solver.configSimple.checkConv);

				labelRow("Momentum Tolerance");
				ImGui::InputDouble("##SimpleMomTol", &scene.solver.configSimple.momTol, 0.0, 0.0, "%.3e");

				labelRow("Continuity Tolerance");
				ImGui::InputDouble("##SimpleContTol", &scene.solver.configSimple.ppTol, 0.0, 0.0, "%.3e");
			
				labelRow("Linear Solver Max Iteration");
				ImGui::InputInt("##LinearSolverIteration", &scene.solver.linearSolverConfig.maxIter, 0.0, 0.0);
			
			}
			ImGui::EndTable();
		}
	}
	else if (selectedItem == "Fluid Settings") {
		ImGui::SeparatorText("Fluid Settings");
		if (ImGui::BeginTable("Fluid Settings", 3)) {

			labelRow("Density");
			inputDoubleWithUnits("##Density", solver.f.rho, varUnits.rhoUnit, Units::densityUnits, "%.6g");

			labelRow("Dynamic Viscosity");
			inputDoubleWithUnits("##DynamicViscosity", solver.f.mu, varUnits.muUnit, Units::dynamicViscosityUnits, "%.6g");

			labelRow("Diffusion Coefficient");
			inputDoubleWithUnits("##Diffusion Coefficient", solver.f.D, varUnits.DUnit, Units::diffusionCoefficientUnits, "%.6g");

			ImGui::EndTable();
		}
	}

	else if (selectedItem == "Transient Settings") {
		ImGui::SeparatorText("Transient Settings");

		if (ImGui::BeginTable("Transient Settings", 2)) {

			setupTableColumns(
				column("Label", 300.0f),
				column("Value", 150.0f)
			);
			
			labelRow("dt");
			ImGui::InputDouble("##timeStep", &scene.solver.dt, 0.0, 0.0, "%.3f");

			labelRow("tEnd");
			ImGui::InputDouble("##endTime", &scene.solver.tEnd, 0.0, 0.0, "%.3f");

			labelRow("Save keyframe every # Iterations");
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

		bool generalOpen = false;
		
		if (drawTree("General", generalOpen)) {
			selectedBoundaryGroupID = -1;
		}

		if (generalOpen) {

			drawLeaf("Solver Settings");
			changeCursorOnHover();

			ImGui::TreePop();
			changeCursorOnHover();
		}


		// draw boundary tree node
		bool boundaryOpen = false;

		if (drawTree("Boundary", boundaryOpen)) {
			selectedBoundaryGroupID = -1;
		}

		if (boundaryOpen) {

			for (BoundarySegmentGroup& group : mesh.boundaryGroups) {
				ImGui::PushID(group.id);

				bool isOpened = false;


				if (drawTree(group.name.c_str(), isOpened, UIFlagsTree::BranchClosedFlags)) {
					selectedBoundaryGroupID = group.id;
					selectedItem = "Boundary Group";
				}

				if (isOpened) {
					std::vector<BoundaryVariable> activeLeaves = getPhysicsValueLeaves(group);

					for (BoundaryVariable var : activeLeaves) {
						ImGui::PushID((int)(var));

						if (drawLeaf(boundaryVariableToString(var))) {
							selectedBoundaryGroupID = group.id;
							selectedBoundaryVariable = var;
							selectedItem = "Boundary Variable";
						}
						ImGui::PopID();
						changeCursorOnHover();
					}
					ImGui::TreePop();
				}

				ImGui::PopID();
			}
			ImGui::TreePop();


		}


























		if (ImGui::TreeNodeEx("Convergence", UIFlagsTree::BranchOpenedFlags)) {
			drawLeaf("Residuals");
			drawLeaf("Tolerance");
			ImGui::TreePop();
		}
		changeCursorOnHover();
		
		if (ImGui::TreeNodeEx("Fluid Properties", UIFlagsTree::BranchOpenedFlags)) {
			drawLeaf("Fluid Settings");
			ImGui::TreePop();
		}
		changeCursorOnHover();

		if (solver.transient) {
			if (ImGui::TreeNodeEx("Transient", UIFlagsTree::BranchOpenedFlags)) {
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