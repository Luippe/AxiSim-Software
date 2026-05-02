#pragma once
#include "pch.h"
#include "buffer_manager.h"

class Mesh;
class Results;
class Colormap;
struct GridConfig;
class SceneView;

class Inspector {
public:

	Inspector(SceneView& scene);

	void generate();
	void render();

	bool keepAspectRatio = false;

private:

	float aspect;
	int startX, startY;
	int endX, endY;
	float boxWidth, boxHeight;
	bool dragging = false;
	ImVec2 imageSize;
	glm::vec2 initMousePos;
	glm::vec2 currentMousePos;
	std::vector<int> selectedIndices;

	SceneView& scene;
	Mesh& mesh;
	Results& results;
	GridConfig& g;
	Colormap& colormap;
	TextureBuffer textureBuffer;

	// get and return the loction of the mouse in index notation
	glm::vec2 getMouseIndex();

	// handle all mouse events
	void updateSelectedIndices();

	// extract the value from the 2D field
	void createScalarImage();

	// create buffer using the scalarImage
	void createBuffer();

	// draw the 2D field
	void drawField();

	// draw GridConfig lines
	void drawGridConfig();

	// draw rectangle when mouse is dragged
	void drawRect();

	std::vector<float> pixels;


};