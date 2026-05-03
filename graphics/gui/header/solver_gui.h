#pragma once
#include "base_gui.h"

class SceneView;
class Solver;

class SolverGUI : public BaseGUI  {
public:
	SolverGUI(SceneView& scene);
	void draw();

private:
	// set default values for residual settings based on the current residual type
	void setResidualDefault();

	SceneView& scene;
	Solver& solver;
};