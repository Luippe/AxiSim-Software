#pragma once
#include "base_gui.h"

class SceneView;
class Solver;
struct BoundaryCondition;

class SolverGUI : public BaseGUI  {
public:
	SolverGUI(SceneView& scene);
	void draw();
private:
	void drawBCCombo(const char* label, BoundaryCondition& bc);
	SceneView& scene;
	Solver& solver;
};