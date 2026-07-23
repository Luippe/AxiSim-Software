#pragma once
#include "pch.h"

class SceneView;
class Camera3D;
class Results;
class Console;
struct GridConfig;
class Project;


class MousePicker {
public:

	//Console& console;
	Project& project;
	Camera3D& camera;
	Results& results;
	SceneView& scene;
	GridConfig& g;
	Console* console;

	MousePicker(Project& project, SceneView& scene);

	// pick whatever is in scene
	void pick();

	void update();

	// getter for getting current ray
	glm::vec3 getCurrentRay();

	// setter for setting the width, height, and position of ImGui scene window
	void setDimensions(int w, int h, ImVec2 pos);

private:

	int width = 1, height = 1;
	ImVec2 rectPos;

	// warpper for finding ray-cylinder intersection
	bool dataPick();

	glm::vec3 currentRay;

	// Where the pick ray starts. Under perspective that is always the eye, but
	// an orthographic view has parallel rays, so the pixel chooses the origin
	// instead of the direction -- every intersection test works off this rather
	// than camera.position.
	glm::vec3 rayOrigin;

	// check if ray intersects cap of cylinder
	bool capIntersect(const glm::vec3& capCenter, const glm::vec3& capNormal, float innerRadius, float outerRadius, float& t);

	// check if ray intersects ring of cylinder with radius rad
	bool ringIntersect(float radius, float front, float back, float& t);
};
