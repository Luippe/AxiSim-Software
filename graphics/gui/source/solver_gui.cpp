#include "solver_gui.h"
#include "scene_view.h"		// must be in front of graphics_struct.h
#include "mesh.h"
#include "solver.h"

#include "project.h"

#include "graphics_struct.h"
#include "solver_struct.h"
#include "app_struct.h"

#include "boundary_func.h"

#include "flag_manager.h"
#include "unit_manager.h"

#include <cfloat>
#include <string>
#include <cstring>

using namespace BoundaryDefaults;
using namespace BoundaryGet;

namespace {
	// Icon shown next to a boundary group in the setup tree, keyed by its type.
	// A missing PNG is safe: AppAssets::icon warns once and returns a blank
	// texture, and drawLeaf skips the image when the texture id is 0.
	TextureBuffer* boundaryTypeIcon(AppAssets& assets, BoundaryType type) {
		switch (type) {
			case BoundaryType::WALL:            return &assets.icon("wall");
			case BoundaryType::VELOCITY_INLET:  return &assets.icon("inlet");
			case BoundaryType::PRESSURE_OUTLET: return &assets.icon("outlet");
			case BoundaryType::SYMMETRY:        return &assets.icon("symmetry");
			case BoundaryType::FAR_FIELD:       return &assets.icon("boundary");
		}
		return nullptr;
	}
}

SolverGUI::SolverGUI(Project& project, AppConfig& appConfig) :
	project(project),
	mesh(project.mesh),
	solver(project.solver),
	appConfig(appConfig),
	assets(appConfig.assets),
	varUnits(project.solver.varUnits) {
}

// ======================================================================
// -----------------------HELPER FUNCTIONS-------------------------------
// ======================================================================
// set default values for residual settings based on the current residual type
void setResidualDefault(ConfigResidual& configRes) {
	switch (configRes.type) {
	case RESIDUAL_SCALED:
		configRes.normType = RESIDUAL_LINF;
		configRes.scaleType = RESIDUAL_SCALING_DIAGONAL;
		break;
	case RESIDUAL_RAW:
		configRes.normType = RESIDUAL_LINF;
		configRes.scaleType = RESIDUAL_SCALING_NONE;
		break;
	case RESIDUAL_RMS:
		configRes.normType = RESIDUAL_L2;
		configRes.scaleType = RESIDUAL_SCALING_SQRT_N;
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
	if (ImGui::Checkbox("Energy", &solver.fieldOption.solveEnergy)) {
		solver.cfg["Energy"].enabled = solver.fieldOption.solveEnergy;
	}
	if (ImGui::Checkbox("Concentration", &solver.fieldOption.solveConcentration)) {
		solver.cfg["Concentration"].enabled = solver.fieldOption.solveConcentration;
	}
	ImGui::Checkbox("Multigrid", &solver.useMultigrid);
}

void SolverGUI::drawResidualSettings() {
	sectionHeader("Residuals");

	// A residual's row only appears when its field is being solved: Temperature
	// needs the energy solver, Concentration needs the concentration solver.
	auto rowVisible = [&](const char* name) -> bool {
		if (std::strcmp(name, "Temperature") == 0)   return solver.fieldOption.solveEnergy;
		if (std::strcmp(name, "Concentration") == 0) return solver.fieldOption.solveConcentration;
		return true;
	};

	ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8.0f, 5.0f));
	if (ImGui::BeginTable("Residual Settings", 4, UIFlags::TableSimpleFlags | ImGuiTableFlags_NoSavedSettings)) {
		setupTableColumns(
			column("Plot", 44.0f),
			column("Residual", 105.0f),
			column("Type", 130.0f, ImGuiTableColumnFlags_WidthStretch),
			column("Tolerance", 105.0f, ImGuiTableColumnFlags_WidthStretch)
		);
		ImGui::TableHeadersRow();

		for (const char*& name : solver.residualPlotType) {
			if (!rowVisible(name)) {
				continue;
			}

			ConfigResidual& configResidual = solver.cfg.at(name);

			ImGui::TableNextRow();
			ImGui::PushID(name);

			ImGui::TableSetColumnIndex(0);
			ImGui::AlignTextToFramePadding();
			ImGui::Checkbox("##PlotResidual", &configResidual.enabled);

			ImGui::TableSetColumnIndex(1);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(name);

			// Continuity is a mass-imbalance sum with no residual "type"; it still
			// carries a tolerance.
			if (std::strcmp(name, "Continuity") != 0) {
				ImGui::TableSetColumnIndex(2);
				if (createSimpleCombo("##ResidualType", solver.residualType, (int&)configResidual.type, IM_ARRAYSIZE(solver.residualType))) {
					setResidualDefault(configResidual);
				}
			}

			ImGui::TableSetColumnIndex(3);
			inputDouble("##ResidualTol", &configResidual.tol, "%.3e");

			ImGui::PopID();
		}

		ImGui::EndTable();
	}
	ImGui::PopStyleVar();

	// Norm and scaling are advanced knobs; keep them out of the main table and only
	// expose them when the user opens this section.
	if (ImGui::CollapsingHeader("Advanced Options")) {
		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8.0f, 5.0f));
		if (ImGui::BeginTable("Residual Advanced", 3, UIFlags::TableSimpleFlags | ImGuiTableFlags_NoSavedSettings)) {
			setupTableColumns(
				column("Residual", 105.0f),
				column("Norm", 130.0f, ImGuiTableColumnFlags_WidthStretch),
				column("Scaling", 130.0f, ImGuiTableColumnFlags_WidthStretch)
			);
			ImGui::TableHeadersRow();

			for (const char*& name : solver.residualPlotType) {
				// Continuity has no norm/scaling; skip hidden fields too.
				if (!rowVisible(name) || std::strcmp(name, "Continuity") == 0) {
					continue;
				}

				ConfigResidual& configResidual = solver.cfg.at(name);

				ImGui::TableNextRow();
				ImGui::PushID(name);

				ImGui::TableSetColumnIndex(0);
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted(name);

				ImGui::TableSetColumnIndex(1);
				createSimpleCombo("##ResidualNorm", solver.residualNormType, (int&)configResidual.normType, IM_ARRAYSIZE(solver.residualNormType));

				ImGui::TableSetColumnIndex(2);
				createSimpleCombo("##ResidualScaling", solver.residualScalingType, (int&)configResidual.scaleType, IM_ARRAYSIZE(solver.residualScalingType));

				ImGui::PopID();
			}

			ImGui::EndTable();
		}
		ImGui::PopStyleVar();
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

	// Far Field exposes its freestream velocity values, but its mathematical
	// condition is fixed: U/V are always Dirichlet. Show the type as read-only
	// text instead of presenting a one-choice combo that implies it is editable.
	if (group.type == BoundaryType::FAR_FIELD) {
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(bcTypeToString(bc.type()));
		return changed;
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
			unitLabel(Units::velocityUnits, varUnits.axialUnit);
			break;
		case BoundaryVariable::VVelocity:
			inputDouble(id.c_str(), v, varUnits.radialUnit, Units::velocityUnits);
			unitLabel(Units::velocityUnits, varUnits.radialUnit);
			break;
		case BoundaryVariable::Pressure:
			inputDouble(id.c_str(), v, varUnits.pressureUnit, Units::pressureUnits);
			unitLabel(Units::pressureUnits, varUnits.pressureUnit);
			break;
		case BoundaryVariable::StaticTemperature:
			inputDouble(id.c_str(), v, varUnits.temperatureUnit, Units::temperatureUnits);
			unitLabel(Units::temperatureUnits, varUnits.temperatureUnit);
			break;
		case BoundaryVariable::Concentration:
			inputDouble(id.c_str(), v, varUnits.concentrationUnit, Units::concentrationUnits);
			unitLabel(Units::concentrationUnits, varUnits.concentrationUnit);
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
		unitLabel(Units::concentrationUnits, varUnits.concentrationUnit);
	};

	// Value + unit in maximum-rate units (Vmax, a molar surface flux).
	auto vmaxValue = [&](const std::string& id, double& v) {
		inputDouble(id.c_str(), v, varUnits.VmaxUnit, Units::VmaxUnits);
		unitLabel(Units::VmaxUnits, varUnits.VmaxUnit);
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

	// Checkbox sub-row (e.g. the inhibition toggle). Advances 2 columns like the
	// value helpers, so callers can keep emitting rows afterwards.
	auto checkRow = [&](const std::string& id, const char* paramLabel, bool& v) {
		subRow(paramLabel);
		ImGui::PushID(id.c_str());
		ImGui::Checkbox("##chk", &v);
		ImGui::PopID();
		ImGui::TableNextColumn();          // -> Unit column
		ImGui::TextUnformatted("-");
		ImGui::TableNextColumn();          // -> next row
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
			subRow("Vmax"); vmaxValue(idBase + "_Vmax", p.Vmax);
			subRow("Km");   concValue(idBase + "_Km", p.Km);
			checkRow(idBase + "_inhib", "Inhibition", p.inhibition);
			if (p.inhibition) {
				subRow("m");  dimlessValue(idBase + "_m", p.m);
				subRow("K2"); concValue(idBase + "_K2", p.K2);
				subRow("V2"); dimlessValue(idBase + "_V2", p.V2);
			}
		},
		[&](HillParams& p) {
			dash();
			subRow("Vmax"); vmaxValue(idBase + "_Vmax", p.Vmax);
			subRow("Km");   concValue(idBase + "_Km", p.Km);
			subRow("n");    dimlessValue(idBase + "_n", p.n);
			checkRow(idBase + "_inhib", "Inhibition", p.inhibition);
			if (p.inhibition) {
				subRow("m");  dimlessValue(idBase + "_m", p.m);
				subRow("K2"); concValue(idBase + "_K2", p.K2);
				subRow("V2"); dimlessValue(idBase + "_V2", p.V2);
			}
		},
	}, bc.params);
}

// ======================================================================
// -----------------------WALL LAYER DRAW CALLS--------------------------
// ======================================================================
void SolverGUI::drawWallLayerSection(
	BoundarySegmentGroup& group,
	const std::vector<BoundaryVariable>& activeLeaves
) {
	// Layers add a series transfer resistance to the wall flux, so they only
	// make sense for the scalars that pass through the wall: concentration and
	// temperature. Draw an editor for each one that is currently being solved.
	for (BoundaryVariable var : activeLeaves) {
		if (var != BoundaryVariable::Concentration &&
			var != BoundaryVariable::StaticTemperature) {
			continue;
		}
		drawLayerEditor(group, var);
	}
}

void SolverGUI::drawLayerEditor(
	BoundarySegmentGroup& group,
	BoundaryVariable var
) {
	const char* varLabel = boundaryVariableToString(var);
	std::vector<Layer>& layers = group.layers[var];

	ImGui::Dummy(ImVec2(0.0f, 10.0f));

	std::string title = std::string(varLabel) + " Wall Layers";
	sectionHeader(title.c_str());

	ImGui::PushID(varLabel);

	if (ImGui::Button("Add Layer")) {
		layers.push_back(Layer{});
	}

	if (!layers.empty() && ImGui::BeginTable("Layers", 7)) {
		setupTableColumns(
			column("#", 30.0f),
			column("k", 70.0f),
			column("Unit", 65.0f),
			column("d", 70.0f),
			column("Unit", 65.0f),
			column("R = d/k", 110.0f),
			column("", 30.0f)
		);
		ImGui::TableHeadersRow();

		int removeIndex = -1;

		for (int i = 0; i < (int)layers.size(); i++) {
			Layer& layer = layers[i];

			ImGui::TableNextRow();
			ImGui::PushID(i);

			ImGui::TableSetColumnIndex(0);
			ImGui::AlignTextToFramePadding();
			ImGui::Text("%d", i + 1);

			ImGui::TableSetColumnIndex(1);
			inputDouble("##k", layer.k, varUnits.DUnit, Units::diffusionCoefficientUnits);

			ImGui::TableSetColumnIndex(2);
			unitLabel(Units::diffusionCoefficientUnits, varUnits.DUnit);

			ImGui::TableSetColumnIndex(3);
			inputDouble("##d", layer.d, project.lengthScale.index, Units::lengthUnits);

			ImGui::TableSetColumnIndex(4);
			unitLabel(Units::lengthUnits, project.lengthScale.index);

			// R is fully derived from D and d; recompute it so the stored value
			// the solver will read stays in sync with the inputs.
			layer.R = (layer.k != 0.0) ? layer.d / layer.k : 0.0;

			ImGui::TableSetColumnIndex(5);
			ImGui::AlignTextToFramePadding();
			ImGui::Text("%.3g s/m", layer.R);

			ImGui::TableSetColumnIndex(6);
			if (ImGui::SmallButton("x")) {
				removeIndex = i;
			}

			ImGui::PopID();
		}

		ImGui::EndTable();

		if (removeIndex >= 0) {
			layers.erase(layers.begin() + removeIndex);
		}
	}

	ImGui::PopID();
}

void SolverGUI::drawLinearSolverCombo() {

	// Red-Black Gauss-Seidel relies on the structured checkerboard coloring, so it
	// is unavailable on an unstructured mesh. Coerce a stale selection back to
	// Jacobi and grey out the entry so it can't be re-picked.
	const bool unstructured = mesh.currentMeshType == MeshType::Unstructured;

	int& type = (int&)solver.configSolver.type;
	if (unstructured && type == LINEAR_GS_RB) {
		type = LINEAR_JACOBI;
	}

	ImGui::SetNextItemWidth(-FLT_MIN);
	ImGui::AlignTextToFramePadding();

	if (ImGui::BeginCombo("##LinearSolverType", solver.linearSolverType[type])) {

		for (int i = 0; i < IM_ARRAYSIZE(solver.linearSolverType); i++) {

			ImGui::BeginDisabled(unstructured && i == LINEAR_GS_RB);

			const bool selected = (type == i);
			if (ImGui::Selectable(solver.linearSolverType[i], selected)) {
				type = i;
			}
			if (selected) {
				ImGui::SetItemDefaultFocus();
			}

			ImGui::EndDisabled();
		}

		ImGui::EndCombo();
	}
}

void SolverGUI::drawPropertiesPanel() {

	ImGui::Begin("Overview");

	if (selectedItem == "General") {

		sectionHeader("Fields");
		drawFieldCheckbox();


	}
	else if (selectedItem == "Solver") {

		sectionHeader("Solver");
		if (beginPropertyTable("SolverGeneral")) {
			labelRow("Solver");
			createSimpleCombo("##Solver", solver.velocitySolverType, (int&)solver.currentVelocitySolver, IM_ARRAYSIZE(solver.velocitySolverType));

			labelRow("Linear Solver");
			drawLinearSolverCombo();

			ImGui::EndTable();
		}

		sectionHeader("Options");
		if (beginPropertyTable("SolverOptions", 200.0f)) {

			labelRow("Add Convection Term");
			checkBox("##ConvectionTerm", &solver.configSolver.addConvectionTerm);

			labelRow("Transient");
			checkBox("##TransientTerm", &solver.configSolver.transient);

			labelRow("Convection Discretization");
			createSimpleCombo("##ConvectionScheme", solver.convectionDiscretizationType, (int&)(solver.convectionScheme), IM_ARRAYSIZE(solver.convectionDiscretizationType));

			labelRow("Non-Orthogonal Correctors");
			inputInt("##NonOrthCorrectors", &project.solver.configSimple.nNonOrthCorrectors);
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

			ImGui::EndTable();
		}

	}
	else if (selectedItem == "Boundary Group") {
		BoundarySegmentGroup* group = getBoundaryGroupByID(mesh.boundaryGroups, selectedBoundaryGroupID);

		ImGui::PushFont(appConfig.fonts.uiFontLarge);
		sectionHeader(group->nameBuffer);
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

		// Keep wall-layer stacks only for variables that still belong to this
		// group. Layers apply to Concentration / Static Temperature on a Wall;
		// switching boundary type or turning off a field must not leave orphans.
		for (auto it = group->layers.begin(); it != group->layers.end(); ) {
			bool keep =
				group->type == BoundaryType::WALL &&
				(it->first == BoundaryVariable::Concentration ||
				 it->first == BoundaryVariable::StaticTemperature);

			if (keep) {
				bool active = false;
				for (BoundaryVariable v : activeLeaves) {
					if (v == it->first) { active = true; break; }
				}
				keep = active;
			}

			if (!keep) {
				it = group->layers.erase(it);
			}
			else {
				++it;
			}
		}

		//if (group->type == BoundaryType::FAR_FIELD) {
		//	ImGui::TextDisabled("U and V use fixed Dirichlet conditions with editable values.");
		//	ImGui::TextDisabled("Pressure and scalars remain fixed at zero gradient (Neumann = 0).");
		//	ImGui::Dummy(ImVec2(0.0f, 6.0f));
		//}

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

		// Walls can carry a multi-layer membrane/coating stack for the scalars
		// that diffuse through them (concentration / temperature).
		if (group->type == BoundaryType::WALL) {
			drawWallLayerSection(*group, activeLeaves);
		}

		ImGui::EndChild();
		ImGui::PopStyleColor();
		ImGui::PopStyleVar();

	}
	else if (selectedItem == "Residual") {
		drawResidualSettings();

		sectionHeader("Tolerance");

		if (ImGui::BeginTable("Iteration Settings", 2, UIFlags::TableSimpleFlags)) {
			if (solver.currentVelocitySolver == SOLVER_SIMPLE) {

				setupTableColumns(
					column("Label", 250.0f),
					column("Value", 150.0f, ImGuiTableColumnFlags_WidthStretch)
				);

				labelRow("Plot Residual Every # Iterations");
				inputInt("##SimpleCheckConv", &project.solver.configSimple.checkConv);
				if (project.solver.configSimple.checkConv < 1) {
					project.solver.configSimple.checkConv = 1;
				}

				labelRow("Maximum Outer Iterations");
				inputInt("##SimpleMaxIter", &project.solver.configSimple.maxIter);
				if (project.solver.configSimple.maxIter < 1) {
					project.solver.configSimple.maxIter = 1;
				}

				labelRow("Maximum Linear Solver Iteration");
				inputInt("##LinearSolverIteration", &project.solver.configSolver.maxIter);
				if (project.solver.configSolver.maxIter < 1) {
					project.solver.configSolver.maxIter = 1;
				}

				//if (solver.useMultigrid) {

				//	labelRow("Maximum Multigrid Solve");
				//	inputInt("##MultigridSolve", &project.solver.configSolver.maxIter);
				//	if (project.solver.configSolver.maxIter < 1) {
				//		project.solver.configSolver.maxIter = 1;
				//	}

				//}


			}
			ImGui::EndTable();
		}
	}
	else if (selectedItem == "Fluid Properties") {
		sectionHeader("Fluid Properties");
		if (ImGui::BeginTable("Fluid Settings", 3)) {

			labelRow("Density");
			inputDouble("##Density", solver.f.rho, varUnits.rhoUnit, Units::densityUnits, "%.6g");
			unitLabel(Units::densityUnits, varUnits.rhoUnit);

			labelRow("Dynamic Viscosity");
			inputDouble("##DynamicViscosity", solver.f.mu, varUnits.muUnit, Units::dynamicViscosityUnits, "%.6g");
			unitLabel(Units::dynamicViscosityUnits, varUnits.muUnit);

			labelRow("Diffusion Coefficient");
			inputDouble("##DiffusionCoefficient", solver.f.D, varUnits.DUnit, Units::diffusionCoefficientUnits, "%.6g");
			unitLabel(Units::diffusionCoefficientUnits, varUnits.DUnit);

			labelRow("Heat Capacity");
			inputDouble("##HeatCapacity", solver.f.cp, varUnits.specificHeatUnit, Units::specificHeatUnits, "%.6g");
			unitLabel(Units::specificHeatUnits, varUnits.specificHeatUnit);

			ImGui::EndTable();
		}
	}

	else if (selectedItem == "Transient Settings") {
		sectionHeader("Transient Settings");

		if (ImGui::BeginTable("Transient Settings", 2)) {

			setupTableColumns(
				column("Label", 300.0f),
				column("Value", 150.0f)
			);

			labelRow("dt");
			ImGui::InputDouble("##timeStep", &project.solver.configSolver.dt, 0.0, 0.0, "%.3f");

			labelRow("tEnd");
			ImGui::InputDouble("##endTime", &project.solver.configSolver.tEnd, 0.0, 0.0, "%.3f");

			labelRow("Save keyframe every # Iterations");
			ImGui::InputInt("##saveKeyFrameIter", &project.solver.saveKeyFrameIter, 0.0, 0.0);

			ImGui::EndTable();
		}
	}
	ImGui::End();
}

void SolverGUI::draw() {

	ImGuiTabItemFlags tabFlags = ImGuiTabItemFlags_None;
	if (project.tabSwitchRequested && project.requestedTab == ViewTab::TAB_SOLVER) {
		tabFlags = ImGuiTabItemFlags_SetSelected;
	}

	if (ImGui::BeginTabItem("Solver", nullptr, tabFlags)) {
		project.currentTab = ViewTab::TAB_SOLVER;

		ImGui::BeginChild("SetupTree", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing() - 40.0f), true);

		if (drawLeaf("General", &assets.icon("general"))) {
			selectedBoundaryGroupID = -1;
		}

		// draw solver tree node
		drawLeaf("Solver", &assets.icon("solver"));

		// draw boundary tree node
		bool boundariesOpen = false;
		if (drawTree("Boundary", boundariesOpen, &assets.icon("boundary"))) {
			selectedBoundaryGroupID = -1;
			mesh.highlightedBoundarySegmentIDs.clear();
		}

		if (boundariesOpen) {
			for (BoundarySegmentGroup& group : mesh.boundaryGroups) {
				ImGui::PushID(group.id);

				if (drawLeaf(group.name.c_str(), boundaryTypeIcon(assets, group.type))) {
					selectedBoundaryGroupID = group.id;
					mesh.highlightSegmentsInGroup(group);
					selectedItem = "Boundary Group";
				}

				ImGui::PopID();
			}

			ImGui::TreePop();
		}

		drawLeaf("Residual", &assets.icon("residuals"));

		drawLeaf("Fluid Properties", &assets.icon("fluid_properties"));

		if (solver.configSolver.transient) {
			if (treeHeader("Transient")) {
				drawLeaf("Transient Settings");
				ImGui::TreePop();
			}
		}

		ImGui::EndChild();

		std::string continueReason;
		bool canContinueSolver = solver.canContinue(mesh, &continueReason);
		if (!canContinueSolver && !solver.solverRunning) {
			solver.continueSolver = false;
		}

		ImGui::BeginDisabled(!canContinueSolver);
		ImGui::Checkbox("Continue Solver", &solver.continueSolver);
		ImGui::EndDisabled();

		if (!canContinueSolver &&
			ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
			ImGui::SetTooltip("%s", continueReason.c_str());
		}

		if (actionButton("Start Solver")) {
			project.solver.run(project.mesh);
		}

		drawPropertiesPanel();

		ImGui::EndTabItem();

	}
}
