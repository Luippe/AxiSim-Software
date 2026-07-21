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
#include "fluid_properties_manager.h"

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
		configRes.normType = RESIDUAL_L1;
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
	// The energy field's residual is registered under "Temperature", not "Energy"
	// -- initConfigResiduals and kResidualOrder both use that key, and so does the
	// residualAll launch. at() rather than operator[] on purpose: a typo here used
	// to insert a phantom entry with a null res pointer instead of failing.
	if (ImGui::Checkbox("Energy", &solver.fieldOption.solveEnergy)) {
		solver.cfg.at("Temperature").enabled = solver.fieldOption.solveEnergy;
	}
	if (ImGui::Checkbox("Concentration", &solver.fieldOption.solveConcentration)) {
		solver.cfg.at("Concentration").enabled = solver.fieldOption.solveConcentration;
	}
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
			autoColumn("Residual"),
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
				autoColumn("Residual"),
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

	// Raw dimensionless value (amplitude, Hill exponents). Advances 2 columns.
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

	// Frequency is stored in inverse seconds and displayed as Hz.
	auto frequencyValue = [&](const std::string& id, double& v) {
		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::AlignTextToFramePadding();
		ImGui::PushID(id.c_str());
		ImGui::InputDouble("##value", &v, 0.0, 0.0, "%.6g");
		ImGui::PopID();
		ImGui::TableNextColumn();          // -> Unit column
		ImGui::TextUnformatted("Hz");
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
		[&](PulsatileParams& p) {
			dash();
			subRow(var == BoundaryVariable::UVelocity ? "Mean U0" : "Mean V0");
			nativeValue(idBase + "_mean", p.value);
			subRow("Amplitude A"); dimlessValue(idBase + "_amplitude", p.amplitude);
			subRow("Frequency f"); frequencyValue(idBase + "_frequency", p.frequency);
		},
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

			// Keep the serialized field in step with the inputs so saves written from
			// here still carry it, but display (and the solver) read resistance().
			layer.R = layer.resistance();

			ImGui::TableSetColumnIndex(5);
			ImGui::AlignTextToFramePadding();
			ImGui::Text("%.3g s/m", layer.resistance());

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

	// Gauss-Seidel used to be structured-only: it took its checkerboard from
	// (i+j)%2, which needs a real nr x nz grid. The face path (multiblock /
	// unstructured) now supplies an equivalent ordering by graph coloring, so every
	// mesh type can run it and there is nothing left to grey out.
	int& type = (int&)solver.configSolver.type;

	ImGui::SetNextItemWidth(-FLT_MIN);
	ImGui::AlignTextToFramePadding();

	if (ImGui::BeginCombo("##LinearSolverType", solver.linearSolverType[type])) {

		for (int i = 0; i < IM_ARRAYSIZE(solver.linearSolverType); i++) {

			const bool selected = (type == i);
			if (ImGui::Selectable(solver.linearSolverType[i], selected)) {
				type = i;
			}
			if (selected) {
				ImGui::SetItemDefaultFocus();
			}
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

			// Multigrid accelerates the pressure-correction solve specifically, so
			// it belongs next to the linear solver it replaces there -- not in the
			// General tab's field checkboxes, which pick which EQUATIONS are solved.
			labelRow("Multigrid");
			checkBox("##Multigrid", &solver.useMultigrid);

			ImGui::EndTable();
		}

		sectionHeader("Options");
		if (beginPropertyTable("SolverOptions")) {

			labelRow("Add Convection Term");
			checkBox("##ConvectionTerm", &solver.configSolver.addConvectionTerm);

			// The discretization only describes a term that is being assembled,
			// so it is meaningless with convection off.
			const bool convectionOff = !solver.configSolver.addConvectionTerm;

			labelRow("Convection Discretization");
			ImGui::BeginDisabled(convectionOff);
			createSimpleCombo("##ConvectionScheme", solver.convectionDiscretizationType, (int&)(solver.convectionScheme), IM_ARRAYSIZE(solver.convectionDiscretizationType));
			ImGui::EndDisabled();
			disabledHint(convectionOff, "Enable Add Convection Term to choose a discretization.");

			// A structured mesh is orthogonal by construction, so the deferred
			// cross term is identically zero. Force the flag off as well as
			// greying it -- runCheck does the same, and leaving a stale true here
			// would show a checked box that the solve ignores.
			const bool orthogonalMesh = mesh.currentMeshType == MeshType::Structured;
			if (orthogonalMesh) {
				project.solver.configSimple.useNonOrthCorrector = false;
			}

			labelRow("Non-Orthogonal Corrector");
			ImGui::BeginDisabled(orthogonalMesh);
			checkBox("##NonOrthCorrector", &project.solver.configSimple.useNonOrthCorrector);
			ImGui::EndDisabled();
			disabledHint(orthogonalMesh, "A structured mesh is orthogonal, so there is no cross term to correct.");

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

		// draw() drops a stale selection before this runs, so this should not fire --
		// kept so the panel does not silently depend on its one caller having done
		// that, since it is all that stands between a dropped group and a deref. Bail
		// out through End() to keep the Begin/End pair balanced.
		if (!group) {
			ImGui::End();
			return;
		}

		ImGui::PushFont(appConfig.fonts.uiFontLarge);
		sectionHeader(group->nameBuffer);
		ImGui::PopFont();

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 10.0f));
		ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.20f, 0.25f, 0.32f, 1.0f));
		ImGui::BeginChild("Properties", ImVec2(0.0f, 0.0f), true);

		if (ImGui::BeginTable("Boundary Type", 2)) {
			setupTableColumns(
				autoColumn("Label"),
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
			// Variable and Unit hold text ("Static Temperature" overflowed the old
			// 100px); Condition and Value hold stretch-width widgets, so those keep
			// explicit widths.
			setupTableColumns(
				autoColumn("Variable"),
				column("Condition", 140.0f, ImGuiTableColumnFlags_WidthStretch),
				column("Value", 110.0f, ImGuiTableColumnFlags_WidthStretch),
				autoColumn("Unit")
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
					autoColumn("Label"),
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

				// Multigrid replaces the linear solver above for the pressure
				// correction only, so it carries its own cycle count.
				if (solver.useMultigrid) {

					const bool graphPrepared = solver.solverRunning;

					labelRow("Maximum Multigrid Cycles");
					ImGui::BeginDisabled(graphPrepared);
					inputInt("##MultigridMaxIter", &project.solver.configMultigrid.maxIter);
					ImGui::EndDisabled();
					disabledHint(graphPrepared, "The multigrid graph is already prepared for this solve.");

					if (!graphPrepared && project.solver.configMultigrid.maxIter < 1) {
						project.solver.configMultigrid.maxIter = 1;
					}

					// Coarsest-level sweep count. Same capture caveat as the cycle
					// count: it decides how many smoother nodes get recorded into the
					// run graph, so it cannot move once that graph is prepared.
					labelRow("Coarsest Level Sweeps");
					ImGui::BeginDisabled(graphPrepared);
					inputInt("##MultigridLinearSweep", &project.solver.configMultigrid.linearSweep);
					ImGui::EndDisabled();
					disabledHint(graphPrepared, "The multigrid graph is already prepared for this solve.");

					if (!graphPrepared && project.solver.configMultigrid.linearSweep < 1) {
						project.solver.configMultigrid.linearSweep = 1;
					}

				}


			}
			ImGui::EndTable();
		}
	}
	else if (selectedItem == "Fluid Properties") {

		sectionHeader("Preset");
		if (beginPropertyTable("FluidPresetTable")) {

			// Derived from the live values every frame rather than stored, so a
			// hand edit to any property drops the label back to "Custom" instead of
			// leaving it claiming a preset the numbers no longer match.
			const int presetIndex = FluidPresets::matchingIndex(solver.f);

			labelRow("Fluid");
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::BeginCombo("##FluidPreset", FluidPresets::presets[presetIndex].name)) {

				for (int i = 0; i < (int)FluidPresets::presets.size(); i++) {

					const FluidPresets::FluidPreset& preset = FluidPresets::presets[i];
					const bool selected = (presetIndex == i);

					if (ImGui::Selectable(preset.name, selected)) {
						FluidPresets::apply(i, solver.f);
					}

					if (selected) {
						ImGui::SetItemDefaultFocus();
					}
				}

				ImGui::EndCombo();
			}

			ImGui::EndTable();
		}

		sectionHeader("Fluid Properties");
		if (ImGui::BeginTable("Fluid Settings", 3, UIFlags::TableSimpleFlags)) {

			// Label and Unit are text, so they auto-fit ("Diffusion Coefficient" did
			// not fit the old implicit third-of-the-panel column). Value holds a
			// stretch-width input.
			setupTableColumns(
				autoColumn("Label"),
				column("Value", 120.0f, ImGuiTableColumnFlags_WidthStretch),
				autoColumn("Unit")
			);

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

	else if (selectedItem == "Transient") {
		sectionHeader("Transient");

		if (beginPropertyTable("TransientSettings")) {

			labelRow("Enabled");
			checkBox("##TransientTerm", &solver.configSolver.transient);

			// Keep the configured values visible while the transient solver is off,
			// but make it clear that they only take effect once Enabled is checked.
			ImGui::BeginDisabled(!solver.configSolver.transient);

			// Round-tripped through a local int rather than the (int&) cast the other
			// combos use: TimeScheme is uint8_t-backed (so it fits ConfigSolver's
			// padding and keeps old .bin saves loadable), and reinterpreting it as
			// int& would write 4 bytes over a 1-byte field.
			labelRow("Time Discretization");
			int timeScheme = (int)project.solver.configSolver.timeScheme;
			if (createSimpleCombo(
				"##TimeScheme",
				project.solver.timeSchemeType,
				timeScheme,
				IM_ARRAYSIZE(project.solver.timeSchemeType)
			)) {
				project.solver.configSolver.timeScheme = (TimeScheme)timeScheme;
			}

			labelRow("Time Step dt (s)");
			ImGui::InputDouble("##timeStep", &project.solver.configSolver.dt, 0.0, 0.0, "%.5f");

			labelRow("End Time tEnd (s)");
			ImGui::InputDouble("##endTime", &project.solver.configSolver.tEnd, 0.0, 0.0, "%.5f");

			labelRow("Save Keyframe Every # Time Steps");
			ImGui::InputInt("##saveKeyFrameIter", &project.solver.saveKeyFrameIter, 0.0, 0.0);

			ImGui::EndDisabled();
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

		// A selection outlives the vector it points into. Re-meshing after a geometry
		// edit rebuilds mesh.boundaryGroups and drops any group that no longer matches,
		// so a held ID can resolve to nothing -- which used to null-deref in
		// drawPropertiesPanel. IDs are only unique within one project's lifetime
		// (nextGroupID is monotonic, but Mesh::reset() rewinds it and a load brings in
		// another file's numbering), so across a new-project or load a held ID can also
		// resolve to an unrelated boundary whose BCs would be edited in its place.
		// Validate here, once, before anything reads it -- that covers every rebuild
		// path without SolverGUI having to hear about each one. MeshGUI self-heals the
		// same way in drawBoundaryGroupGUI; it keeps its own selection, separate from
		// this.
		if (selectedBoundaryGroupID >= 0 &&
			!getBoundaryGroupByID(mesh.boundaryGroups, selectedBoundaryGroupID)) {

			selectedBoundaryGroupID = -1;
			mesh.highlightedBoundarySegmentIDs.clear();

			if (selectedItem == "Boundary Group") {
				selectedItem = "General";
			}
		}

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

		drawLeaf("Transient", &assets.icon("transient"));

		ImGui::EndChild();

		std::string continueReason;
		bool canContinueSolver = solver.canContinue(mesh, &continueReason);
		if (!canContinueSolver && !solver.solverRunning) {
			solver.continueSolver = false;
		}

		const bool running = solver.solverRunning;

		// Continue Solver decides how the NEXT run initializes, so it cannot be
		// meaningfully toggled once a solve is under way.
		ImGui::BeginDisabled(!canContinueSolver || running);
		ImGui::Checkbox("Continue Solver", &solver.continueSolver);
		ImGui::EndDisabled();

		disabledHint(
			!canContinueSolver || running,
			running ? "Solver is running." : continueReason.c_str()
		);

		// Start becomes Stop for the duration of the run. A stop is cooperative:
		// the solver finishes the iteration it is in, copies its fields back, and
		// ends there, so the partial result stays usable.
		if (running) {
			const bool stopping = solver.stopRequested;

			ImGui::BeginDisabled(stopping);
			if (actionButton(stopping ? "Stopping..." : "Stop Solver")) {
				project.solver.requestStop();
			}
			ImGui::EndDisabled();

			disabledHint(stopping, "Already stopping; finishing the current iteration.");
		}
		else if (actionButton("Start Solver")) {
			project.solver.run(project.mesh);
		}

		drawPropertiesPanel();

		ImGui::EndTabItem();

	}
}
