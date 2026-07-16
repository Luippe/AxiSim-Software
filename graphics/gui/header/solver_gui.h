#pragma once
#include "base_gui.h"
#include "boundary_struct.h"

class Project;
class Solver;
class Mesh;
struct AppConfig;
struct AppAssets;

class SolverGUI : public BaseGUI  {
public:
	SolverGUI(Project& project, AppConfig& appConfig);
	void draw();

private:

	// draw properties panel when a tree node is clicked on
	void drawPropertiesPanel();

	void drawFieldCheckbox();

	void drawResidualSettings();

	void drawRowBoundaryVariableEditor(
		BoundarySegmentGroup& group,
		BoundaryVariable var,
		BoundaryCondition& bc
	);

	// draw the multi-layer wall editor for each layer-bearing variable
	// (Concentration / Static Temperature) of a Wall boundary group
	void drawWallLayerSection(
		BoundarySegmentGroup& group,
		const std::vector<BoundaryVariable>& activeLeaves
	);

	// draw the add/edit/remove table for one variable's wall layer stack
	void drawLayerEditor(
		BoundarySegmentGroup& group,
		BoundaryVariable var
	);

	BoundaryCondition& getOrCreateBC(
		BoundarySegmentGroup& group,
		BoundaryVariable variable
	);

	int selectedBoundaryGroupID = -1;

	Project& project;
	Solver& solver;
	VariableUnits& varUnits;
	Mesh& mesh;
	AppConfig& appConfig;
	AppAssets& assets;


};
