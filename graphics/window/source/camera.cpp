#include "camera.h"
#include "printer.h"
#include <glm/gtc/matrix_transform.hpp>

void Camera::updateTransformationMatrix() {

	float aspect = (float)width / (float)height;

	model = glm::mat4(1.0f);
	position = getPosition();
	view = glm::lookAt(position, target, getUp());
	projection = glm::perspective(glm::radians(fov), aspect, 0.1f, 100.0f);
}

void Camera::update() {

	updateTransformationMatrix();

	if (snapping) {
		snapCamera();
	}
}

void Camera::calculateRotation(const glm::vec2& prevMouse, const glm::vec2& currMouse) {

	glm::vec3 va = mapToSphere(prevMouse);
	glm::vec3 vb = mapToSphere(currMouse);

	if (va == vb) {	// if va == vb, then the vectors are on top of each other
		return;
	}

	glm::quat dq = getQuat(vb, va);
	rotation = glm::normalize(rotation * dq);
}

void Camera::calculateZoom(double yoffset) {
	// use exponential zoom to feel smoother
	zoom = std::exp((float)(-yoffset * 0.1f));
	distance *= zoom;
	distance = glm::clamp(distance, 0.1f, 50.0f);
}

void Camera::calculatePan(float dx, float dy) {
	float panSpeed = 0.001f * distance;
	target += -dx * getRight() * panSpeed;
	target += -dy * getUp()	   * panSpeed;
}

glm::vec3 Camera::mapToSphere(const glm::vec2& mouse) {
	glm::vec2 mousePos(mouse.x - rectPos.x, mouse.y - rectPos.y); // relative mouse position to the top left corner of the window
	glm::vec2 screenPos = getNormalizedDeviceCoords(mousePos.x, mousePos.y, width, height);
	float length2 = screenPos.x * screenPos.x + screenPos.y * screenPos.y;

	if (length2 < 1.0f) {
		return glm::vec3(screenPos.x, screenPos.y, std::sqrt(1.0f - length2));
	}
	else {
		//printf("ASDASDAD\n");
		return glm::vec3(screenPos, 0.0f) / std::sqrt(length2);
	}
}

void Camera::updateTargetRotation(glm::vec3& axis) {
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

void Camera::snapCamera() {
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

void Camera::home() {

}

glm::vec3 Camera::getFront() {
	return glm::normalize(target - position);
}

glm::vec3 Camera::getPosition() {
	glm::vec3 offset = rotation * glm::vec3(0.0f, 0.0f, distance);
	return target + offset;
}

glm::vec3 Camera::getRight() {
	return rotation * glm::vec3(1.0f, 0.0f, 0.0f);
}

glm::vec3 Camera::getUp() {
	return rotation * glm::vec3(0.0f, 1.0f, 0.0f);
}

void Camera::setDimensions(int w, int h, ImVec2 pos) {
	width = w;
	height = h;
	rectPos = pos;
}
