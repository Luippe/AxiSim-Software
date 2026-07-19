#pragma once
#include <string>

struct SceneView;
class Project;
class GUI;

// Playback control for a transient run. It owns no field data of its own: the
// frames live in Results (built from Solver::timeFrames), and this only decides
// WHICH frame is on screen, then asks Results to push it into the live fields.
class AnimationGUI {
public:

	AnimationGUI(Project& project, GUI& gui);

	void render();

private:

	Project& project;
	SceneView& scene;

	int currentFrame = 0;
	int previousFrame = -1;		// -1 forces the first frame to be applied
	bool isPlaying = false;
	int maxFPS = 60;
	int minFPS = 1;
	int fps = 10;
	float accumulator = 0.0f;

	// advance currentFrame based on time accumulated since the last draw
	void updateCurrentFrame(int frameCount);

	// handle mouse and keyboard events
	void handleEvents(int frameCount);

};
