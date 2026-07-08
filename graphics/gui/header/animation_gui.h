#pragma once
#include <vector>
#include <string>

#include "field_manager.h"

struct SceneView;
class Project;
class GUI;

class AnimationGUI {
public:

	AnimationGUI(Project& project, GUI& gui);

	void render();

	// load from a .bin file, the variables needed to make an animation
	void loadAnimation(const std::string& filename);

	bool isReady = false;

private:

	// holds data for each frame
	struct FlowFrame {
		double time = 0.0;
		std::vector<Field> fields;
	};
	
	// vmin and vmax for all of the time in the animation
	struct MinMaxGlobal {
		float vmin = 0;
		float vmax = 0;
	};

	std::vector<FlowFrame> frames;
	std::vector<MinMaxGlobal> minmaxGlobals;
	
	Project& project;
	SceneView& scene;

	int currentFrame = 0;
	int previousFrame = 0;
	bool isPlaying = false;
	int maxFPS = 60;
	int minFPS = 1;
	int fps = 10;
	float accumulator = 0.0f;

	// update currernt frame based on time accumulated
	void updateCurrentFrame();

	// update the current field which will be displayed
	void updateCurrentField();

	// handle mouse and keyboard events
	void handleEvents();



};
