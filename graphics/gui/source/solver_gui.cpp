#include "solver_gui.h"
#include "scene_view.h"		// must be in front of graphics_struct.h
#include "mesh.h"
#include "solver.h"

#include "graphics_struct.h"
#include "solver_struct.h"

#include "flag_manager.h"
#include "unit_manager.h"
#include "printer.h"

SolverGUI::SolverGUI(SceneView& scene) :
	scene(scene),
	mesh(scene.mesh),
	solver(scene.solver),
	varUnits(scene.solver.varUnits) {
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

const char* fieldTypeToString(FieldType field) {
	switch (field) {
	case FieldType::UVelocity:
		return "U Velocity";

	case FieldType::VVelocity:
		return "V Velocity";

	case FieldType::Pressure:
		return "Pressure";

	case FieldType::Energy:
		return "Energy";

	case FieldType::Concentration:
		return "Concentration";

	default:
		return "Unknown";
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

	default:
		return "Wall";
	}
}

void SolverGUI::drawFieldCheckbox() {
	ImGui::Checkbox("Axial Velocity", &solver.fieldOption.solveU);
	ImGui::Checkbox("Radial Velocity", &solver.fieldOption.solveV);
	ImGui::Checkbox("Pressure", &solver.fieldOption.solvePressure);
	ImGui::Checkbox("Energy", &solver.fieldOption.solveEnergy);
	ImGui::Checkbox("Concentration", &solver.fieldOption.solveConcentration);
}

BCType SolverGUI::getDefaultBCType(FieldType field) const {
	switch (field) {
	case FieldType::UVelocity:
		return BCType::DIRICHLET;

	case FieldType::VVelocity:
		return BCType::DIRICHLET;

	case FieldType::Pressure:
		return BCType::NEUMANN;

	case FieldType::Energy:
		return BCType::NEUMANN;

	default:

		return BCType::NONE;
	}
}

BoundaryCondition& SolverGUI::getOrCreateBC(
	BoundarySegmentGroup& group,
	FieldType field
) {

	auto it = group.bcs.find(field);

	if (it != group.bcs.end()) {
		return it->second;
	}

	BoundaryCondition bc{};
	bc.enabled = true;
	bc.type = getDefaultBCType(field);
	bc.val = 0.0;

	auto [newIt, inserted] = group.bcs.emplace(field, bc);
	return newIt->second;
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

	std::vector<FieldType> activeFields = getActiveFields();


	if (activeFields.empty()) {
		return;
	}

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

	//for (FieldType field : activeFields) {
	//	ImGui::PushID((int)(field));

	//	BoundaryCondition& bc = getOrCreateBC(*selectedGroup, field);

	//	

	//	if (ImGui::TreeNodeEx(
	//		fieldTypeToString(field),
	//		ImGuiTreeNodeFlags_DefaultOpen
	//	)) {
	//		drawBCEditor(field, bc);
	//		ImGui::TreePop();
	//	}

	//	ImGui::PopID();
	//}
}

void SolverGUI::drawBCEditor(FieldType& type, BoundaryCondition& bc) {
	return;
}

std::vector<FieldType> SolverGUI::getActiveFields() const {
	std::vector<FieldType> fields;

	const SolverFieldOption& option = solver.fieldOption;

	if (option.solveU) {
		fields.push_back(FieldType::UVelocity);
	}

	if (option.solveV) {
		fields.push_back(FieldType::VVelocity);
	}

	if (option.solvePressure) {
		fields.push_back(FieldType::Pressure);
	}

	if (option.solveEnergy) {
		fields.push_back(FieldType::Energy);
	}

	if (option.solveConcentration) {
		fields.push_back(FieldType::Concentration);
	}

	return fields;
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

	else if (selectedItem == "Boundary") {

		//// total width = sum of table width + 10 * num of columns to account for padding
		//// total height = number of rows * 31
		//ImGui::TextUnformatted("Axial Velocity");
		//ImGui::BeginChild("Axial Velocity", ImVec2(440.0f, 62.0f), true);
		//if (ImGui::BeginTable("Axial Velocity", 4)) {

		//	setupTableColumns(
		//		column("Label", 100.0f),
		//		column("BC Type", 100.0f),
		//		column("Value", 100.0f),
		//		column("Units", 100.0f));

		//	labelRow("Inlet");
		//	createSimpleCombo("##InletBCType", solver.bcInletTypeNames, (int&)(solver.uBC.inlet.type), IM_ARRAYSIZE(solver.bcInletTypeNames));

		//	tableNextColumn();
		//	inputDoubleWithUnits(
		//		"AxialInlet",
		//		solver.uBC.inlet.val,
		//		varUnits.axialUnit,
		//		Units::velocityUnits
		//	);

		//	labelRow("Outer");
		//	createSimpleCombo("##OuterBCType", solver.bcTypeNames, (int&)(solver.uBC.outer.type), IM_ARRAYSIZE(solver.bcTypeNames));

		//	tableNextColumn();
		//	inputDoubleWithUnits(
		//		"AxialOuter",
		//		solver.uBC.outer.val,
		//		varUnits.axialUnit,
		//		Units::velocityUnits
		//	);

		//	ImGui::EndTable();
		//}
		//ImGui::EndChild();

		//ImGui::Dummy(ImVec2(0.0f, 30.0f));
		//ImGui::TextUnformatted("Radial Velocity");
		//ImGui::BeginChild("Radial Velocity", ImVec2(440.0f, 40.0f), true);
		//if (ImGui::BeginTable("Boundary Conditions", 4)) {

		//	setupTableColumns(
		//		column("Label", 100.0f),
		//		column("BC Type", 100.0f),
		//		column("Value", 100.0f),
		//		column("Units", 100.0f)
		//	);

		//	labelRow("Outer");
		//	createSimpleCombo(
		//		"##OuterBCType",
		//		solver.bcTypeNames,
		//		(int&)(solver.vBC.outer.type),
		//		IM_ARRAYSIZE(solver.bcTypeNames)
		//	);

		//	tableNextColumn();
		//	inputDoubleWithUnits(
		//		"##RadialVelocity",
		//		solver.vBC.outer.val,
		//		varUnits.radialUnit,
		//		Units::velocityUnits
		//	);

		//	ImGui::EndTable();
		//}
		//ImGui::EndChild();




		//ImGui::Dummy(ImVec2(0.0f, 30.0f));
		//ImGui::TextUnformatted("Pressure");
		//ImGui::BeginChild("Pressure", ImVec2(440.0f, 40.0f), true);
		//if (ImGui::BeginTable("Boundary Conditions", 4)) {

		//	setupTableColumns(
		//		column("Label", 100.0f),
		//		column("BC Type", 100.0f),
		//		column("Value", 100.0f),
		//		column("Units", 100.0f)
		//	);

		//	labelRow("Outlet");
		//	createSimpleCombo(
		//		"##OutletBCType",
		//		solver.bcTypeNames,
		//		(int&)(solver.pBC.outlet.type),
		//		IM_ARRAYSIZE(solver.bcTypeNames)
		//	);

		//	tableNextColumn();
		//	inputDoubleWithUnits(
		//		"##Pressure",
		//		solver.pBC.outer.val,
		//		varUnits.pressureUnit,
		//		Units::pressureUnits
		//	);

		//	ImGui::EndTable();
		//}
		//ImGui::EndChild();

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

	drawBoundaryConditionGUI();

	ImGui::End();
}

void SolverGUI::draw() {
	if (ImGui::BeginTabItem("Solver")) {
		scene.currentTab = TAB_SOLVER;

		ImGui::BeginChild("SetupTree", ImVec2(260, 600), true);

		bool generalOpen = ImGui::TreeNodeEx("General", UIFlags::BranchOpenedFlags);


		if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {

			selectedBoundaryGroupID = -1;
			selectedItem = "General";

		}

		if (generalOpen) {

			drawLeaf("Solver Settings");
			changeCursorOnHover();

			ImGui::TreePop();
			changeCursorOnHover();
		}


		// draw boundary tree node
		bool boundariesOpen = ImGui::TreeNodeEx("Boundary", UIFlags::BranchOpenedFlags);

		if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {

			selectedBoundaryGroupID = -1;
			selectedItem = "Boundary";

		}

		if (boundariesOpen) {

			for (BoundarySegmentGroup& group : mesh.boundaryGroups) {
				ImGui::PushID(group.id);
				if (drawLeaf(group.name.c_str())) {
					selectedBoundaryGroupID = group.id;
				}

				ImGui::PopID();
			}

			ImGui::TreePop();
		}


























		if (ImGui::TreeNodeEx("Convergence", UIFlags::BranchOpenedFlags)) {
			drawLeaf("Residuals");
			drawLeaf("Tolerance");
			ImGui::TreePop();
		}
		changeCursorOnHover();
		
		if (ImGui::TreeNodeEx("Fluid Properties", UIFlags::BranchOpenedFlags)) {
			drawLeaf("Fluid Settings");
			ImGui::TreePop();
		}
		changeCursorOnHover();

		if (solver.transient) {
			if (ImGui::TreeNodeEx("Transient", UIFlags::BranchOpenedFlags)) {
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