#pragma once
#include "base_gui.h"
#include "boundary_struct.h"

class Project;
class Solver;
class Mesh;

class SolverGUI : public BaseGUI  {
public:
	SolverGUI(Project& project);
	void draw();

private:

	// set default values for residual settings based on the current residual type
	void setResidualDefault();

	// draw properties panel when a tree node is clicked on
	void drawPropertiesPanel();

	void drawBoundaryConditionGUI();

	void drawFieldCheckbox();

	void drawBoundaryVariableEditor(
		BoundarySegmentGroup& group,
		BoundaryVariable var,
		BoundaryCondition& bc
	);

	BoundaryCondition& getOrCreateBC(
		BoundarySegmentGroup& group,
		BoundaryVariable variable
	);

	int selectedBoundaryGroupID = -1;
	BoundaryVariable selectedBoundaryVariable = BoundaryVariable::UVelocity;

	Project& project;
	Solver& solver;
	VariableUnits& varUnits;
	Mesh& mesh;


};