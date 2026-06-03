#pragma once
#include "base_gui.h"
#include "boundary_struct.h"

class SceneView;
class Solver;
class Mesh;

class SolverGUI : public BaseGUI  {
public:
	SolverGUI(SceneView& scene);
	void draw();

private:
	// set default values for residual settings based on the current residual type
	void setResidualDefault();

	// draw properties panel when a tree node is clicked on
	void drawPropertiesPanel();

	void drawBoundaryConditionGUI();

	void drawFieldCheckbox();

	void drawBoundaryVariableEditor(BoundaryVariable var, BoundaryCondition& bc);

	std::vector<BoundaryVariable> getPhysicsValueLeaves(
		const BoundarySegmentGroup& group
	) const;

	BoundaryCondition& getOrCreateBC(
		BoundarySegmentGroup& group,
		BoundaryVariable variable
	);


	int selectedBoundaryGroupID = -1;
	BoundaryVariable selectedBoundaryVariable = BoundaryVariable::UVelocity;

	SceneView& scene;
	Solver& solver;
	VariableUnits& varUnits;
	Mesh& mesh;


};