#pragma once
#include <vector>
#include <string>
#include "buffer_manager.h"
#include "field_manager.h"
#include "solver_struct.h"

struct SceneView;

class AnimationGUI {
public:

	AnimationGUI(SceneView& scene);

	void render();

	// load from a .bin file, the variables needed to make an animation
	void loadAnimation(const std::string& filename);

	bool isReady = false;
private:

	void updateFlowAnimation();

	// update texture buffer
	void updateAnimationTexture();

	// handle mouse and keyboard events
	void handleEvents();

	SceneView& scene;

	std::vector<FlowFrame> frames;
	TextureBuffer textureBuffer;

	int widgetWidth;
	int widgetHeight;

	int nr = 0;
	int nz = 0;

	int currentFrame = 0;
	bool isPlaying = false;
	int maxFPS = 120;
	int minFPS = 1;
	int fps = 10;
	float accumulator = 0.0f;

};