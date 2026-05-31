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

	BCType getDefaultBCType(FieldType field) const;

	std::vector<FieldType> getActiveFields() const;

	BoundaryCondition& getOrCreateBC(BoundarySegmentGroup& group, FieldType field);

	void drawBCEditor(FieldType& type, BoundaryCondition& bc);

	int selectedBoundaryGroupID = -1;
	SceneView& scene;
	Solver& solver;
	VariableUnits& varUnits;
	Mesh& mesh;

};