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

	// draw properties panel when a tree node is clicked on
	void drawPropertiesPanel();


	SceneView& scene;
	Solver& solver;

};