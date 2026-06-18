#pragma once
#include "pch.h"
#include "bounding.h"

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
	Bounding& bound;
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

	int width, height;
	ImVec2 rectPos;

	// picker for axis
	void axisBBPick();

	// warpper for finding ray-cylinder intersection
	bool dataPick();

	glm::vec3 currentRay;
	glm::vec3 calculateMouseRay();
	glm::vec4 toEyeCoords(glm::vec4 clipCoords);
	glm::vec3 toWorldCoords(glm::vec4 eyeCoords);

	// check if ray has collided with a given bounding box. assign distance t
	bool BBIntersect(BoundingBox& box, float& t);

	// check if ray intersects cap of cylinder
	bool capIntersect(const glm::vec3& capCenter, const glm::vec3& capNormal, float radius, float& t);

	// check if ray intersects ring of cylinder with radius rad
	bool ringIntersect(float radius, float& t);

	// given a bounding box ID, return the picked axis
	glm::vec3 getPickedAxis(int& pickedID);
};
