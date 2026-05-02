#pragma once
#include "pch.h"
#include "math_func.h"


class Camera {

public:
	Camera() {};

	glm::vec3 getPosition();

	void calculateRotation(const glm::vec2& prevMouse, const glm::vec2& currMouse);
	void calculatePan(float dx, float dy);
	void calculateZoom(double yoffset);
	void update();
	glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

	float sensitivity = 0.3f;
	float zoom = 1.0f;
	float fov = 45.0f;
	float distance = 20.0f;

	glm::quat targetRotation;
	glm::mat4 model, view, projection;
	glm::vec3 position = glm::vec3(0.0f, 0.0f, distance);

	// go back to starting camera position
	void home();

	// smoothly move the camera to the given coordinate
	void updateTargetRotation(glm::vec3& axis);

	// set width and height of scene
	void setDimensions(int w, int h, ImVec2 pos);

private:
	int width, height;
	ImVec2 rectPos;

	bool snapping = false;
	void snapCamera();

	// camera snapping movement speed
	float dt = 0.05f;
	float maxStep = glm::radians(180.0f) * dt; // example: 180 deg/s

	// update model, view, and projection matrix
	void updateTransformationMatrix();

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
