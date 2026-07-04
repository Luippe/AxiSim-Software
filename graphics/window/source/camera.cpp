#include "camera.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>
#include "unit_manager.h"
#include "printer.h"

// ======================================================================
// -----------------------CAMERA 3D--------------------------------------
// ======================================================================
Camera3D::Camera3D() {
	initPositionAndAngle();
}

void Camera3D::initPositionAndAngle() {

	constexpr float yaw = glm::radians(-45.0f);
	constexpr float pitch = glm::radians(-35.264f);
	//constexpr float zRot = glm::radians(45.0f);

	glm::quat quatYaw = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
	glm::quat quatPitch = glm::angleAxis(pitch, glm::vec3(1.0f, 0.0f, 0.0f));
	//glm::quat quatZ = glm::angleAxis(zRot, glm::vec3(0.0f, 0.0f, 1.0f));

	rotation = glm::normalize(quatYaw * quatPitch);

}


void Camera3D::updateTransformationMatrix() {

	float aspect = (float)width / (float)height;

	model = glm::mat4(1.0f);

	position = getPosition();
	view = glm::lookAt(position, target, getUp());
	projection = glm::perspective(glm::radians(fov), aspect, 0.1f, 100.0f);

}

void Camera3D::calculateRotation(const glm::vec2& prevMouse, const glm::vec2& currMouse) {

	glm::vec3 va = mapToSphere(prevMouse);
	glm::vec3 vb = mapToSphere(currMouse);

	if (va == vb) {	// if va == vb, then the vectors are on top of each other
		return;
	}

	glm::quat dq = getQuat(vb, va);
	rotation = glm::normalize(rotation * dq);
}

void Camera3D::calculateZoom(double yoffset) {
	// use exponential zoom to feel smoother
	zoom = std::exp((float)(-yoffset * 0.1f));
	distance *= zoom;
	distance = glm::clamp(distance, 0.1f, 50.0f);
}

void Camera3D::calculatePan(float dx, float dy) {
	float panSpeed = 0.001f * distance;
	target += -dx * getRight() * panSpeed;
	target += -dy * getUp()	   * panSpeed;
}

glm::vec3 Camera3D::mapToSphere(const glm::vec2& mouse) {
	glm::vec2 mousePos(mouse.x - rectPos.x, mouse.y - rectPos.y); // relative mouse position to the top left corner of the window
	glm::vec2 screenPos = getNormalizedDeviceCoords(mousePos.x, mousePos.y, width, height);
	float length2 = screenPos.x * screenPos.x + screenPos.y * screenPos.y;

	if (length2 < 1.0f) {
		return glm::vec3(screenPos.x, screenPos.y, std::sqrt(1.0f - length2));
	}
	else {
		return glm::vec3(screenPos, 0.0f) / std::sqrt(length2);
	}
}

void Camera3D::updateTargetRotation(glm::vec3& axis) {
	snapping = true;

	// if already looking at the axis, then dont do anything
	if (glm::normalize(position - target) == axis) return;

	// get the quaternion between the two vectors
	glm::vec3 posNorm = glm::normalize(position);
	glm::quat dq = getQuat(posNorm, axis);

	// get final camera rotation and desired up vector
	glm::quat finalRotation = glm::normalize(dq * rotation);
	glm::vec3 finalUpNorm = glm::normalize(finalRotation * glm::vec3(0.0f, 1.0f, 0.0f));
	glm::vec3 bestUp = finalUpNorm;
	float bestScore = 0.0f;
	for (const glm::vec3& axis : candidates) {
		float currentScore = glm::dot(axis, finalUpNorm);
		if (currentScore > bestScore) {
			bestScore = currentScore;
			bestUp = axis;
		}
	}

	// construct target rotation with the best up vector, target axis, and right vector
	glm::vec3 right = glm::normalize(glm::cross(bestUp, axis));
	glm::mat3 R(right, bestUp, axis);
	targetRotation = glm::normalize(glm::quat_cast(R));
}

void Camera3D::snapCamera() {

	if (!snapping) return;

	if (glm::dot(rotation, targetRotation) < 0.0f) {	// makes sure it takes the shortest path
		targetRotation = -targetRotation;
	}

	// compute the angle between the current rotation and the target rotation
	float cosTheta = glm::clamp(glm::dot(rotation, targetRotation), -1.0f, 1.0f);
	float angle = 2.0f * std::acos(cosTheta);
	
	// get rotation matrix for moving the camera
	if (angle < maxStep) {
		rotation = targetRotation;
		snapping = false;
	}
	else {
		float alpha = maxStep / angle;
		rotation = glm::normalize(glm::slerp(rotation, targetRotation, alpha));
	}
}

void Camera3D::home() {

}

glm::vec3 Camera3D::getFront() {
	return glm::normalize(target - position);
}

glm::vec3 Camera3D::getPosition() {
	glm::vec3 offset = rotation * glm::vec3(0.0f, 0.0f, distance);
	return target + offset;
}

glm::vec3 Camera3D::getRight() {
	return rotation * glm::vec3(1.0f, 0.0f, 0.0f);
}

glm::vec3 Camera3D::getUp() {
	return rotation * glm::vec3(0.0f, 1.0f, 0.0f);
}

void Camera3D::setDimensions(int w, int h, ImVec2 pos) {
	width = w;
	height = h;
	rectPos = pos;
}

// ======================================================================
// -----------------------CAMERA 2D--------------------------------------
// ======================================================================

Camera2D::Camera2D() {
	initPosition();
}

void Camera2D::initPosition() {
	center = Vec2{ 0.0, 0.0 };
	unitsPerPixel = 0.001;
}

void Camera2D::setDimensions(int w, int h, ImVec2 pos) {
	width = std::max(w, 1);
	height = std::max(h, 1);
	rectPos = pos;
}

Vec2 Camera2D::screenToWorld(const ImVec2& screen) const {
	ImVec2 centerScreen{
		rectPos.x + 0.5f * static_cast<float>(width),
		rectPos.y + 0.5f * static_cast<float>(height)
	};

	return Vec2{
		center.z + (screen.x - centerScreen.x) * unitsPerPixel,
		center.r + (centerScreen.y - screen.y) * unitsPerPixel
	};
}

ImVec2 Camera2D::worldToScreen(Vec2 world) const {
	ImVec2 centerScreen{
		rectPos.x + 0.5f * static_cast<float>(width),
		rectPos.y + 0.5f * static_cast<float>(height)
	};

	return ImVec2{
		centerScreen.x + static_cast<float>((world.z - center.z) / unitsPerPixel),
		centerScreen.y - static_cast<float>((world.r - center.r) / unitsPerPixel)
	};
}

float Camera2D::worldLengthToScreen(double length) const {
	if (unitsPerPixel <= 1e-30) {
		return 0.0f;
	}

	return static_cast<float>(length / unitsPerPixel);
}

void Camera2D::calculatePan(float dx, float dy) {
	center.z -= dx * unitsPerPixel;
	center.r += dy * unitsPerPixel;
}

void Camera2D::calculateZoom(double yoffset, const ImVec2& focusScreen) {
	if (yoffset == 0.0) {
		return;
	}

	Vec2 beforeZoom = screenToWorld(focusScreen);

	double zoomFactor = std::exp(-yoffset * 0.1);
	unitsPerPixel = std::clamp(
		unitsPerPixel * zoomFactor,
		minUnitsPerPixel,
		maxUnitsPerPixel
	);

	Vec2 afterZoom = screenToWorld(focusScreen);

	center.z += beforeZoom.z - afterZoom.z;
	center.r += beforeZoom.r - afterZoom.r;
}

void Camera2D::rescaleZoom(double ratio) {
	if (ratio <= 0.0) {
		return;
	}

	unitsPerPixel = std::clamp(
		unitsPerPixel * ratio,
		minUnitsPerPixel,
		maxUnitsPerPixel
	);
}

void Camera2D::setZoom(double newUnitsPerPixel) {
	if (!(newUnitsPerPixel > 0.0)) {
		return;
	}

	unitsPerPixel = std::clamp(
		newUnitsPerPixel,
		minUnitsPerPixel,
		maxUnitsPerPixel
	);
}

void Camera2D::home() {
	initPosition();
}
