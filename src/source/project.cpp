#include "project.h"

void Project::createNew() {

	// Stop any running solve before wiping the state it reads.
	solver.shutdown();

	// Reset the shared numerical config (fluid properties, grid, iteration, display
	// units). solver.f / solver.g / solver.itr / solver.varUnits and mesh.g are all
	// references into this Config, so resetting it defaults them in one step. Must run
	// before mesh.reset(), which reads g.R / g.L to build the default domain.
	config = Config{};

	geometry.reset();
	mesh.reset();
	solver.reset();
	results.reset();

	lengthScale = LengthScale{};
	name.clear();
	path.clear();

	// units/length are back to defaults: let the GUI recenter every inspector view.
	resetInspectorViews = true;

	// a new project views the scene the default way too, not however the last
	// one left it
	sceneView = SceneViewSettings{};
	applySceneViewSettings = true;

}
