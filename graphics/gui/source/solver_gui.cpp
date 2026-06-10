#include "solver_gui.h"
#include "scene_view.h"		// must be in front of graphics_struct.h
#include "mesh.h"
#include "solver.h"

#include "IconsFontAwesome7.h"

#include "project.h"

#include "graphics_struct.h"
#include "solver_struct.h"

#include "boundary_func.h"

#include "flag_manager.h"
#include "unit_manager.h"
#include "printer.h"


SolverGUI::SolverGUI(Project& project) :
	project(project),
	mesh(project.mesh),
	solver(project.solver),
	varUnits(project.solver.varUnits) {
}

// ======================================================================
// -----------------------HELPER FUNCTIONS-------------------------------
// ======================================================================
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
		BoundaryDefaults::makeDefaultBC(group, variable);

	auto [newIt, inserted] = group.bcs.emplace(variable, bc);

	return newIt->second;
}

void SolverGUI::drawFieldCheckbox() {
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

	default:
		return "Unknown";
	}
}

const char* bcTypeToString(BCType type) {
	switch (type) {
	case BCType::DIRICHLET:			return "Dirichlet";
	case BCType::NEUMANN:			return "Neumann";
	case BCType::FULLY_DEVELOPED:	return "Fully Developed";
	default:						return "Unknown";
	}
}

// ======================================================================
// -------------------BOUNDARY VARIABLE DRAW CALLS-----------------------
// ======================================================================
bool createBCTypeCombo(
	const char* label,
	const BoundaryVariable selectedVar,
	BoundaryCondition& bc,
	BoundarySegmentGroup& group
) {

	std::vector<BCType> allowedBCTypes = BoundaryDefaults::getAllowedBCType(selectedVar, group);

	bool changed = false;

	bool currentAllowed = false;
	for (BCType type : allowedBCTypes) {
		if (bc.type == type) {
			currentAllowed = true;
			break;
		}
	}

	if (!currentAllowed) {
		bc.type = allowedBCTypes[0];
		changed = true;
	}

	ImGui::SetNextItemWidth(-FLT_MIN);
	if (ImGui::BeginCombo(label, bcTypeToString(bc.type))) {

		for (BCType type : allowedBCTypes) {
			bool isSelected = bc.type == type;

			if (ImGui::Selectable(bcTypeToString(type), isSelected)) {
				if (bc.type != type) {
					bc.type = type;
					changed = true;
				}
			}

			if (isSelected) {
				ImGui::SetItemDefaultFocus();
			}
		}

		ImGui::EndCombo();
	}
	return changed;
}

void SolverGUI::drawBoundaryVariableEditor(BoundaryVariable var, BoundaryCondition& bc, BoundarySegmentGroup& group) {

	ImGui::SeparatorText(boundaryVariableToString(var));

	const char* label = boundaryVariableToString(var);
	if (ImGui::BeginTable(label, 4)) {
		setupTableColumns(
			column("Label", 100.0f),
			column("BC", 100.0f),
			column("Value", 100.0f),
			column("Unit", 100.0f)
		);

		labelRow(label);
		switch (var) {
		case BoundaryVariable::UVelocity:
			createBCTypeCombo("##UVelocity", var, bc, group);
			ImGui::TableNextColumn();
			inputDouble("U Velocity", bc.value, varUnits.axialUnit, Units::velocityUnits);
			comboUnit("UVelocity", varUnits.axialUnit, Units::velocityUnits);
			break;
		case BoundaryVariable::VVelocity:
			createBCTypeCombo("##VVelocity", var, bc, group);
			ImGui::TableNextColumn();
			inputDouble("V Velocity", bc.value, varUnits.radialUnit, Units::velocityUnits);
			comboUnit("VVelocity", varUnits.radialUnit, Units::velocityUnits);
			break;
		case BoundaryVariable::Pressure:
			createBCTypeCombo("##Pressure", var, bc, group);
			ImGui::TableNextColumn();
			inputDouble("Pressure", bc.value, varUnits.pressureUnit, Units::pressureUnits);
			comboUnit("Pressure", varUnits.pressureUnit, Units::pressureUnits);
			break;
		case BoundaryVariable::StaticTemperature:
			createBCTypeCombo("##StaticTemperature", var, bc, group);
			ImGui::TableNextColumn();
			inputDouble("Static Temperature", bc.value, varUnits.temperatureUnit, Units::temperatureUnits);
			comboUnit("StaticTemperature", varUnits.temperatureUnit, Units::temperatureUnits);

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

			drawBoundaryVariableEditor(selectedBoundaryVariable, bc, *group);
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
				ImGui::InputInt("##SimpleMaxIter", &project.solver.configSimple.maxIter, 0.0, 0.0);

				labelRow("Plot Residual Every # Iterations");
				inputInt("##SimpleCheckConv", &project.solver.configSimple.checkConv);

				labelRow("Momentum Tolerance");
				ImGui::InputDouble("##SimpleMomTol", &project.solver.configSimple.momTol, 0.0, 0.0, "%.3e");

				labelRow("Continuity Tolerance");
				ImGui::InputDouble("##SimpleContTol", &project.solver.configSimple.ppTol, 0.0, 0.0, "%.3e");
			
				labelRow("Linear Solver Max Iteration");
				ImGui::InputInt("##LinearSolverIteration", &project.solver.linearSolverConfig.maxIter, 0.0, 0.0);
			
			}
			ImGui::EndTable();
		}
	}
	else if (selectedItem == "Fluid Settings") {
		ImGui::SeparatorText("Fluid Settings");
		if (ImGui::BeginTable("Fluid Settings", 3)) {

			labelRow("Density");
			inputDouble("##Density", solver.f.rho, varUnits.rhoUnit, Units::densityUnits, "%.6g");
			comboUnit("##DensityUnit", varUnits.rhoUnit, Units::densityUnits);

			labelRow("Dynamic Viscosity");
			inputDouble("##DynamicViscosity", solver.f.mu, varUnits.muUnit, Units::dynamicViscosityUnits, "%.6g");
			comboUnit("##DynamicViscosityUnit", varUnits.muUnit, Units::dynamicViscosityUnits);

			labelRow("Diffusion Coefficient");
			inputDouble("##DiffusionCoefficient", solver.f.D, varUnits.DUnit, Units::diffusionCoefficientUnits, "%.6g");
			comboUnit("##DiffusionCoefficientUnit", varUnits.DUnit, Units::diffusionCoefficientUnits);

			labelRow("Heat Capacity");
			inputDouble("##HeatCapacity", solver.f.cp, varUnits.specificHeatUnit, Units::specificHeatUnits, "%.6g");
			comboUnit("##HeatCapacityUnit", varUnits.specificHeatUnit, Units::specificHeatUnits);

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
			ImGui::InputDouble("##timeStep", &project.solver.dt, 0.0, 0.0, "%.3f");

			labelRow("tEnd");
			ImGui::InputDouble("##endTime", &project.solver.tEnd, 0.0, 0.0, "%.3f");

			labelRow("Save keyframe every # Iterations");
			ImGui::InputInt("##saveKeyFrameIter", &project.solver.saveKeyFrameIter, 0.0, 0.0);

			ImGui::EndTable();
		}
	}
	ImGui::End();
}

void SolverGUI::draw() {
	if (ImGui::BeginTabItem("Solver")) {
		project.currentTab = ViewTab::TAB_SOLVER;

		ImGui::BeginChild("SetupTree", ImVec2(0.0f, 600.0f), true);

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
					std::vector<BoundaryVariable> activeLeaves = BoundaryDefaults::getVariableFromBoundaryType(
						group, 
						solver.fieldOption.solveEnergy,
						solver.fieldOption.solveConcentration
					);

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
			project.solver.run(project.mesh);
		}
		changeCursorOnHover();

		drawPropertiesPanel();

		ImGui::EndTabItem();

	}
}