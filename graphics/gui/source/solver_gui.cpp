#include "solver_gui.h"
#include "scene_view.h"		// must be in front of graphics_struct.h
#include "mesh.h"
#include "solver.h"

#include "project.h"

#include "graphics_struct.h"
#include "solver_struct.h"

#include "boundary_func.h"

#include "flag_manager.h"
#include "unit_manager.h"
#include "printer.h"

#include <string>

using namespace BoundaryDefaults;
using namespace BoundaryGet;

SolverGUI::SolverGUI(Project& project, AppConfig& appConfig) :
	project(project),
	mesh(project.mesh),
	solver(project.solver),
	appConfig(appConfig),
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



BoundaryCondition& SolverGUI::getOrCreateBC(
	BoundarySegmentGroup& group,
	BoundaryVariable variable
) {
	auto it = group.bcs.find(variable);

	if (it != group.bcs.end()) {
		return it->second;
	}

	BoundaryCondition bc = makeDefaultBC(group, variable);

	auto [newIt, inserted] = group.bcs.emplace(variable, bc);

	return newIt->second;
}

void SolverGUI::drawFieldCheckbox() {
	ImGui::Checkbox("Energy", &solver.fieldOption.solveEnergy);
	ImGui::Checkbox("Concentration", &solver.fieldOption.solveConcentration);
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

	std::vector<BCType> allowedBCTypes = getAllowedBCType(selectedVar, group);

	bool changed = false;

	bool currentAllowed = false;
	for (BCType type : allowedBCTypes) {
		if (bc.type() == type) {
			currentAllowed = true;
			break;
		}
	}

	if (!currentAllowed) {
		bc.setType(allowedBCTypes[0]);
		changed = true;
	}

	ImGui::SetNextItemWidth(-FLT_MIN);
	if (ImGui::BeginCombo(label, bcTypeToString(bc.type()))) {

		for (BCType type : allowedBCTypes) {
			bool isSelected = bc.type() == type;

			if (ImGui::Selectable(bcTypeToString(type), isSelected)) {
				if (bc.type() != type) {
					bc.setType(type);
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

// Overloaded-lambda helper for std::visit (handle each variant alternative).
namespace {
	template<class... Ts> struct overload : Ts... { using Ts::operator()...; };
	template<class... Ts> overload(Ts...) -> overload<Ts...>;
}

void SolverGUI::drawRowBoundaryVariableEditor(
	BoundarySegmentGroup& group,
	BoundaryVariable var,
	BoundaryCondition& bc
) {

	const char* varLabel = boundaryVariableToString(var);
	const std::string idBase = varLabel;

	// Value + unit in the boundary variable's own unit family (advances 2 columns).
	auto nativeValue = [&](const std::string& id, double& v) {
		switch (var) {
		case BoundaryVariable::UVelocity:
			inputDouble(id.c_str(), v, varUnits.axialUnit, Units::velocityUnits);
			comboUnit(id.c_str(), varUnits.axialUnit, Units::velocityUnits);
			break;
		case BoundaryVariable::VVelocity:
			inputDouble(id.c_str(), v, varUnits.radialUnit, Units::velocityUnits);
			comboUnit(id.c_str(), varUnits.radialUnit, Units::velocityUnits);
			break;
		case BoundaryVariable::Pressure:
			inputDouble(id.c_str(), v, varUnits.pressureUnit, Units::pressureUnits);
			comboUnit(id.c_str(), varUnits.pressureUnit, Units::pressureUnits);
			break;
		case BoundaryVariable::StaticTemperature:
			inputDouble(id.c_str(), v, varUnits.temperatureUnit, Units::temperatureUnits);
			comboUnit(id.c_str(), varUnits.temperatureUnit, Units::temperatureUnits);
			break;
		case BoundaryVariable::Concentration:
			inputDouble(id.c_str(), v, varUnits.concentrationUnit, Units::concentrationUnits);
			comboUnit(id.c_str(), varUnits.concentrationUnit, Units::concentrationUnits);
			break;
		default:
			ImGui::TableNextColumn();
			ImGui::TableNextColumn();
			break;
		}
	};

	// Value + unit in concentration units (e.g. Km, a half-saturation concentration).
	auto concValue = [&](const std::string& id, double& v) {
		inputDouble(id.c_str(), v, varUnits.concentrationUnit, Units::concentrationUnits);
		comboUnit(id.c_str(), varUnits.concentrationUnit, Units::concentrationUnits);
	};

	// Raw value, no unit (Vmax, Hill exponents). Advances 2 columns.
	auto dimlessValue = [&](const std::string& id, double& v) {
		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::AlignTextToFramePadding();
		ImGui::PushID(id.c_str());
		ImGui::InputDouble("##value", &v, 0.0, 0.0, "%.3g");
		ImGui::PopID();
		ImGui::TableNextColumn();          // -> Unit column
		ImGui::TextUnformatted("-");
		ImGui::TableNextColumn();          // -> next row
	};

	// Blank Value + Unit cells for types with no inline primary (advances 2 columns).
	auto dash = [&]() {
		ImGui::TextUnformatted("-");
		ImGui::TableNextColumn();
		ImGui::TextUnformatted("-");
		ImGui::TableNextColumn();
	};

	// Start an indented parameter sub-row and move to the Value column.
	auto subRow = [&](const char* paramLabel) {
		labelRow(paramLabel);
		ImGui::TableNextColumn();          // skip the Condition column
	};

	// --- main row: Variable | Condition | (primary value or dash) | unit ---
	labelRow(varLabel);
	const std::string condId = "##cond_" + idBase;
	createBCTypeCombo(condId.c_str(), var, bc, group);
	ImGui::TableNextColumn();              // -> Value column

	std::visit(overload{
		[&](DirichletParams& p)      { nativeValue(idBase + "_val", p.value); },
		[&](NeumannParams& p)        { nativeValue(idBase + "_val", p.value); },
		[&](FullyDevelopedParams& p) { nativeValue(idBase + "_val", p.value); },
		[&](NoneParams&)             { dash(); },
		[&](MichaelisMentenParams& p) {
			dash();
			subRow("Vmax"); dimlessValue(idBase + "_Vmax", p.Vmax);
			subRow("Km");   concValue(idBase + "_Km", p.Km);
		},
		[&](HillParams& p) {
			dash();
			subRow("Vmax"); dimlessValue(idBase + "_Vmax", p.Vmax);
			subRow("Km");   concValue(idBase + "_Km", p.Km);
			subRow("n");    dimlessValue(idBase + "_n", p.n);
			subRow("m");    dimlessValue(idBase + "_m", p.m);
		},
	}, bc.params);
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
	else if (selectedItem == "Boundary Group") {
		BoundarySegmentGroup* group = getBoundaryGroupByID(mesh.boundaryGroups, selectedBoundaryGroupID);

		ImGui::PushFont(appConfig.fonts.uiFontLarge);
		ImGui::SeparatorText(group->nameBuffer);
		ImGui::PopFont();

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 10.0f));
		ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.20f, 0.25f, 0.32f, 1.0f));
		ImGui::BeginChild("Properties", ImVec2(0.0f, 0.0f), true);

		if (ImGui::BeginTable("Boundary Type", 2)) {
			setupTableColumns(
				column("Label", 100.0f),
				column("Boundary Type", 100.0f, ImGuiTableColumnFlags_WidthStretch)
			);

			labelRow("Type");
			createSimpleCombo("##BoundaryType", solver.boundaryType, (int&)group->type, IM_ARRAYSIZE(solver.boundaryType));
			
			ImGui::EndTable();
		}

		ImGui::Dummy(ImVec2(0.0f, 10.0f));

		std::vector<BoundaryVariable> activeLeaves = getVariableFromBoundaryType(
			group->type,
			solver.fieldOption.solveEnergy,
			solver.fieldOption.solveConcentration
		);

		// Drop BCs left over from a previous boundary type so a group never
		// carries conditions that don't belong to its current type. e.g. a
		// Symmetry boundary must not keep Dirichlet U/P inherited from when it
		// was a Wall or Pressure Outlet.
		for (auto it = group->bcs.begin(); it != group->bcs.end(); ) {
			bool active = false;
			for (BoundaryVariable v : activeLeaves) {
				if (v == it->first) { active = true; break; }
			}
			if (!active) {
				it = group->bcs.erase(it);
			}
			else {
				++it;
			}
		}

		if (ImGui::BeginTable("Boundary Variable Editor", 4)) {
			setupTableColumns(
				column("Variable", 100.0f),
				column("Condition", 100.0f),
				column("Value", 100.0f),
				column("Unit", 100.0f)
			);

			ImGui::TableHeadersRow();

			for (BoundaryVariable& var : activeLeaves) {

				BoundaryCondition& bc = getOrCreateBC(*group, var);
				drawRowBoundaryVariableEditor(*group, var, bc);

			}

			ImGui::EndTable();
		}
		ImGui::EndChild();
		ImGui::PopStyleColor();
		ImGui::PopStyleVar();

	}
	else if (selectedItem == "Residuals") {
		ImGui::SeparatorText("Residual Type");

		if (ImGui::BeginTable("Residual Type Settings", 3)) {

			setupTableColumns(
				column("Label", 100.0f),
				column("Value", 150.0f),
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
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Checkbox("V", &solver.enabledResiduals.plotV);
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Checkbox("Continuity", &solver.enabledResiduals.plotCont);
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);

			if (solver.fieldOption.solveEnergy) {
				ImGui::Checkbox("Temperature", &solver.enabledResiduals.plotTemp);
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);

			}

			if (solver.fieldOption.solveConcentration) {
				ImGui::Checkbox("Concentration", &solver.enabledResiduals.plotConc);
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);

			}
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
				if (project.solver.configSimple.maxIter < 1) {
					project.solver.configSimple.maxIter = 1;
				}

				labelRow("Plot Residual Every # Iterations");
				inputInt("##SimpleCheckConv", &project.solver.configSimple.checkConv);
				if (project.solver.configSimple.checkConv < 1) {
					project.solver.configSimple.checkConv = 1;
				}

				labelRow("Momentum Tolerance");
				ImGui::InputDouble("##SimpleMomTol", &project.solver.configSimple.momTol, 0.0, 0.0, "%.3e");

				labelRow("Continuity Tolerance");
				ImGui::InputDouble("##SimpleContTol", &project.solver.configSimple.ppTol, 0.0, 0.0, "%.3e");

				labelRow("Linear Solver Max Iteration");
				ImGui::InputInt("##LinearSolverIteration", &project.solver.linearSolverConfig.maxIter, 0.0, 0.0);
				if (project.solver.linearSolverConfig.maxIter < 1) {
					project.solver.linearSolverConfig.maxIter = 1;
				}

				labelRow("Non-Orthogonal Correctors");
				ImGui::InputInt("##NonOrthCorrectors", &project.solver.configSimple.nNonOrthCorrectors, 0.0, 0.0);
				if (project.solver.configSimple.nNonOrthCorrectors < 0) {
					project.solver.configSimple.nNonOrthCorrectors = 0;
				}

				labelRow("Pressure Gradient");
				createSimpleCombo(
					"##GradientScheme",
					project.solver.gradientSchemeType,
					(int&)project.solver.gradientScheme,
					IM_ARRAYSIZE(project.solver.gradientSchemeType)
				);

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
		bool boundariesOpen = ImGui::TreeNodeEx("Boundary", UIFlagsTree::BranchOpenedFlags);

		if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {

			selectedBoundaryGroupID = -1;
			mesh.highlightedBoundarySegmentIDs.clear();
			selectedItem = "Boundary";

		}

		if (boundariesOpen) {
			for (BoundarySegmentGroup& group : mesh.boundaryGroups) {
				ImGui::PushID(group.id);

				if (drawLeaf(group.name.c_str())) {
					selectedBoundaryGroupID = group.id;
					mesh.highlightSegmentsInGroup(group);
					selectedItem = "Boundary Group";
				}

				ImGui::PopID();
			}

			ImGui::TreePop();
		}
		changeCursorOnHover();
























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
