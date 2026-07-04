#pragma once
#include "pch.h"
#include "math_func.h"


class Camera3D {

public:
	Camera3D();

	glm::vec3 getPosition();

	void calculateRotation(const glm::vec2& prevMouse, const glm::vec2& currMouse);

	void calculatePan(float dx, float dy);

	void calculateZoom(double yoffset);

	glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

	float sensitivity = 0.3f;
	float zoom = 1.0f;
	float fov = 45.0f;
	float distance = 1.0f;

	glm::quat targetRotation;
	glm::mat4 model, view, projection;
	glm::vec3 position = glm::vec3(0.0f, 0.0f, distance);

	// go back to starting camera position
	void home();

	// smoothly move the camera to the given coordinate
	void updateTargetRotation(glm::vec3& axis);

	// set width and height of scene
	void setDimensions(int w, int h, ImVec2 pos);

	// update model, view, and projection matrix
	void updateTransformationMatrix();

	// snap the camera if there is any snapping to do
	void snapCamera();

private:

	int width, height;
	ImVec2 rectPos;

	bool snapping = false;

	// camera snapping movement speed
	float dt = 0.05f;
	float maxStep = glm::radians(180.0f) * dt; // example: 180 deg/s

	// initialize position and angle of camera when constructing class
	void initPositionAndAngle();

	glm::vec3 mapToSphere(const glm::vec2& mouse);
	glm::vec3 target = glm::vec3(0.0f, 0.0f, 0.0f);
	glm::vec3 getRight();
	glm::vec3 getUp();
	glm::vec3 getFront();
	glm::vec3 candidates[6] = {
		{1.0f, 0.0f, 0.0f},
		{-1.0f, 0.0f, 0.0f},
		{0.0f, 1.0f, 0.0f},
		{0.0f, -1.0f, 0.0f},
		{0.0f, 0.0f, 1.0f},
		{0.0f, 0.0f, -1.0f}
	};


};


class Camera2D {
public:

	Camera2D();

	Vec2 screenToWorld(const ImVec2& screen) const;
	ImVec2 worldToScreen(Vec2 world) const;
	float worldLengthToScreen(double length) const;

	void calculatePan(float dx, float dy);
	void calculateZoom(double yoffset, const ImVec2& focusScreen);

	// rescale zoom around the current center (no on-screen focus point involved),
	// e.g. when the project's display length unit changes.
	void rescaleZoom(double ratio);

	void home();

	// set width, height, and top-left position of the 2D viewport
	void setDimensions(int w, int h, ImVec2 pos);

	Vec2 center = Vec2{ 0.0, 0.0 };
	double unitsPerPixel = 0.001;



private:

	int width = 1;
	int height = 1;
	ImVec2 rectPos = ImVec2(0.0f, 0.0f);

	double minUnitsPerPixel = 1e-9;
	double maxUnitsPerPixel = 1.0;

	// initialize position and angle of camera when constructing class
	void initPosition();

};
