#include "camera.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>
#include "unit_manager.h"
#include "printer.h"

// ======================================================================
// -----------------------CAMERA 3D--------------------------------------
// ======================================================================
namespace {

	// wrap an angle into (-pi, pi], so interpolating between two yaws always
	// takes the short way round instead of unwinding several turns
	float wrapPi(float angle) {
		constexpr float twoPi = 6.28318530718f;
		angle = std::fmod(angle + 3.14159265359f, twoPi);
		if (angle < 0.0f) angle += twoPi;
		return angle - 3.14159265359f;
	}

	// ease in and out, so a snap starts and lands gently instead of stopping dead
	float smoothStep(float t) {
		return t * t * (3.0f - 2.0f * t);
	}

}


Camera3D::Camera3D() {
	initPositionAndAngle();
}

void Camera3D::initPositionAndAngle() {

	yaw = glm::radians(-45.0f);
	pitch = glm::radians(-35.264f);

	snapping = false;

	updateRotation();
}


void Camera3D::updateRotation() {

	// yaw first, then pitch: the pitch axis rides along with the yaw, which is
	// exactly what "orbit left/right, then tilt up/down" means
	rotation = glm::normalize(
		glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f)) *
		glm::angleAxis(pitch, glm::vec3(1.0f, 0.0f, 0.0f))
	);
}


float Camera3D::viewHalfHeight() const {
	return distance * std::tan(glm::radians(fov) * 0.5f);
}


float Camera3D::worldPerPixel() const {
	return 2.0f * viewHalfHeight() / (float)std::max(height, 1);
}


void Camera3D::updateTransformationMatrix() {

	const float aspect = (float)std::max(width, 1) / (float)std::max(height, 1);

	model = glm::mat4(1.0f);

	position = getPosition();
	view = glm::lookAt(position, target, getUp());

	// the clip planes follow the orbit distance instead of sitting at a fixed
	// 0.1/100. Zooming far out used to clip the scene away and zooming in close
	// wasted depth precision on empty space in front of the near plane.
	const float farPlane = distance * 100.0f + 100.0f;

	if (projectionType == ProjectionType::Orthographic) {

		const float halfHeight = viewHalfHeight();
		const float halfWidth = halfHeight * aspect;

		// a symmetric depth range keeps geometry behind the eye plane visible;
		// an orthographic view has no reason to cull it
		projection = glm::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, -farPlane, farPlane);
	}
	else {

		const float nearPlane = std::max(1.0e-3f, distance * 0.005f);
		projection = glm::perspective(glm::radians(fov), aspect, nearPlane, farPlane);
	}
}


void Camera3D::calculateRotation(float dx, float dy) {

	if (dx == 0.0f && dy == 0.0f) return;

	// grabbing and dragging moves the scene with the cursor, so the camera
	// orbits the other way
	const float step = glm::radians(rotateSensitivity);

	yaw = wrapPi(yaw - dx * step);
	pitch = glm::clamp(pitch - dy * step, -maxPitch, maxPitch);

	// a manual drag wins over an in-flight snap rather than fighting it
	snapping = false;

	updateRotation();
}


void Camera3D::calculateZoom(double yoffset) {

	if (yoffset == 0.0) return;

	// exponential, so each notch changes the view by the same proportion
	distance *= std::exp((float)(-yoffset * 0.12));
	distance = glm::clamp(distance, minDistance, maxDistance);
}


void Camera3D::calculatePan(float dx, float dy) {

	// one pixel of drag moves the target by exactly one pixel's worth of world,
	// measured at the orbit target -- so the scene stays under the cursor
	const float step = worldPerPixel();

	target -= dx * getRight() * step;
	target -= dy * getUp() * step;
}


void Camera3D::snapToAxis(const glm::vec3& axis) {

	const float len = glm::length(axis);
	if (len < 1.0e-6f) return;

	const glm::vec3 d = axis / len;

	startYaw = yaw;
	startPitch = pitch;

	// invert getPosition's direction, which is
	//   (cos(pitch)sin(yaw), -sin(pitch), cos(pitch)cos(yaw))
	targetPitch = glm::clamp(std::asin(glm::clamp(-d.y, -1.0f, 1.0f)), -maxPitch, maxPitch);

	// straight up or down leaves the azimuth undefined; keeping the current one
	// means a top view does not also spin the scene on the way there
	targetYaw = (std::abs(d.y) > 0.9999f) ? startYaw : std::atan2(d.x, d.z);

	// take the short way round
	targetYaw = startYaw + wrapPi(targetYaw - startYaw);

	snapT = 0.0f;
	snapping = true;
}


void Camera3D::snapCamera(float dt) {

	if (!snapping) return;

	if (snapSeconds <= 0.0f) {
		yaw = wrapPi(targetYaw);
		pitch = targetPitch;
		snapping = false;
		updateRotation();
		return;
	}

	// a stall (a slow frame, a dragged window) must not teleport the camera
	snapT += glm::clamp(dt, 0.0f, 0.1f) / snapSeconds;

	if (snapT >= 1.0f) {
		yaw = wrapPi(targetYaw);
		pitch = targetPitch;
		snapping = false;
	}
	else {
		const float a = smoothStep(snapT);
		yaw = startYaw + (targetYaw - startYaw) * a;
		pitch = startPitch + (targetPitch - startPitch) * a;
	}

	updateRotation();
}


void Camera3D::home() {

	target = glm::vec3(0.0f);
	distance = 1.0f;

	initPositionAndAngle();
}

glm::vec3 Camera3D::getFront() const {
	return glm::normalize(target - position);
}

glm::vec3 Camera3D::getPosition() const {
	glm::vec3 offset = rotation * glm::vec3(0.0f, 0.0f, distance);
	return target + offset;
}

glm::vec3 Camera3D::getRight() const {
	return rotation * glm::vec3(1.0f, 0.0f, 0.0f);
}

glm::vec3 Camera3D::getUp() const {
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
