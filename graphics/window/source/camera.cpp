#include "camera.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include "unit_manager.h"
#include "printer.h"

// ======================================================================
// -----------------------CAMERA 3D--------------------------------------
// ======================================================================
namespace {

	// ease in and out, so a snap starts and lands gently instead of stopping dead
	float smoothStep(float t) {
		return t * t * (3.0f - 2.0f * t);
	}

}


Camera3D::Camera3D() {
	initPositionAndAngle();
}

void Camera3D::initPositionAndAngle() {

	// the usual three-quarter view: down the (-1, 1, 1) diagonal, upright
	rotation = glm::normalize(
		glm::angleAxis(glm::radians(-45.0f), glm::vec3(0.0f, 1.0f, 0.0f)) *
		glm::angleAxis(glm::radians(-35.264f), glm::vec3(1.0f, 0.0f, 0.0f))
	);

	snapping = false;
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


glm::vec3 Camera3D::trackballPoint(const glm::vec2& px) const {

	const float w = (float)std::max(width, 1);
	const float h = (float)std::max(height, 1);

	const float radius = 0.5f * std::min(w, h) * std::max(trackballRadius, 0.1f);

	// measured from the middle of the viewport, y flipped to point up
	const glm::vec2 v((px.x - 0.5f * w) / radius, (0.5f * h - px.y) / radius);

	const float d2 = v.x * v.x + v.y * v.y;

	// a sphere near the middle, a hyperbolic sheet outside it. The two meet at
	// d2 == 0.5 with the same value AND the same slope, so a drag that crosses
	// the rim does not kink, and the sheet never runs out however far the cursor
	// goes. Out there the point lies almost flat against the screen, so the axis
	// through two of them points at the eye -- that is the border roll, and it
	// needs no special case.
	const float z = (d2 <= 0.5f) ? std::sqrt(1.0f - d2) : (0.5f / std::sqrt(d2));

	return glm::normalize(glm::vec3(v.x, v.y, z));
}


void Camera3D::applyTurn(const glm::quat& worldTurn) {

	// the eye is target + rotation * (0, 0, distance), so turning both the
	// centre and the orientation by the same rotation carries the eye around
	// the pivot rigidly -- the distance to the centre never changes
	rotation = glm::normalize(worldTurn * rotation);
	target = pivot + worldTurn * (target - pivot);
}


bool Camera3D::trackballDelta(const glm::vec2& prevPx, const glm::vec2& curPx, glm::vec3& axisCam, float& angle) const {

	const glm::vec3 p0 = trackballPoint(prevPx);
	const glm::vec3 p1 = trackballPoint(curPx);

	const glm::vec3 axis = glm::cross(p0, p1);
	const float len = glm::length(axis);

	// parallel points mean no drag, or one straight through the middle of the
	// ball -- either way there is no turn to make
	if (len < 1.0e-7f) return false;

	axisCam = axis / len;

	// atan2 rather than asin, so the angle stays right past a quarter turn
	angle = std::atan2(len, glm::dot(p0, p1)) * rotateGain;

	return std::abs(angle) > 1.0e-7f;
}


void Camera3D::calculateRotation(const glm::vec2& prevPx, const glm::vec2& curPx) {

	if (rotationStyle == RotationStyle::Arcball) {
		arcballDrag(prevPx, curPx);
	}
	else {
		turntableDrag(curPx - prevPx);
	}
}


void Camera3D::turntableDrag(const glm::vec2& delta) {

	if (delta.x == 0.0f && delta.y == 0.0f) return;

	const float upLen = glm::length(worldUp);
	if (upLen < 1.0e-6f) return;

	const glm::vec3 up = worldUp / upLen;

	const float step = glm::radians(rotateSensitivity);

	// Once the view has ridden over the pole the horizon is upside down, and a
	// yaw about world up reads backwards on screen. Flipping it there is what
	// lets the turntable go over the top at all and still track the cursor --
	// the alternative, and the reason most turntables stop just short of
	// vertical, is that the controls silently invert up there.
	const float yawSign = (glm::dot(getUp(), up) < 0.0f) ? -1.0f : 1.0f;

	// The no-roll guarantee is an invariant, not a clamp: the camera's right
	// vector starts perpendicular to world up and both of these turns preserve
	// that. Turning about world up keeps right perpendicular to it, and turning
	// about right leaves right alone. So roll can never creep in, however the
	// drag wanders -- and nothing has to be forbidden to keep it that way.
	const glm::quat yaw = glm::angleAxis(-delta.x * step * yawSign, up);
	const glm::quat pitch = glm::angleAxis(-delta.y * step, getRight());

	// pitch about where the camera is now, then yaw about world up
	applyTurn(yaw * pitch);

	// a manual drag wins over an in-flight snap rather than fighting it
	snapping = false;
}


void Camera3D::arcballDrag(const glm::vec2& prevPx, const glm::vec2& curPx) {

	glm::vec3 axisCam;
	float angle;

	if (!trackballDelta(prevPx, curPx, axisCam, angle)) return;

	// the drag turns the SCENE about axisCam, so the camera turns the other way.
	// The axis comes out of the trackball in camera space and the turn has to
	// happen about the pivot, which is a world point, so push it out to world.
	applyTurn(glm::angleAxis(-angle, rotation * axisCam));

	// a manual drag wins over an in-flight snap rather than fighting it
	snapping = false;
}


void Camera3D::calculateRotationAbout(const glm::vec3& worldAxis, const glm::vec2& prevPx, const glm::vec2& curPx) {

	const float axisLen = glm::length(worldAxis);
	if (axisLen < 1.0e-6f) return;

	const glm::vec3 a = worldAxis / axisLen;

	glm::vec3 axisCam;
	float angle;

	if (!trackballDelta(prevPx, curPx, axisCam, angle)) return;

	// keep only what the free turn asked for about `a`: drags that would spin
	// the scene around the locked axis still do, drags across it do nothing.
	// One frame's turn is a fraction of a degree, so treating it as a plain
	// rotation vector and projecting is exact enough to feel right.
	const float turn = angle * glm::dot(rotation * axisCam, a);
	if (std::abs(turn) < 1.0e-7f) return;

	applyTurn(glm::angleAxis(-turn, a));

	snapping = false;
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

	// where the camera's local +Z has to end up (see getPosition)
	const glm::vec3 d = axis / len;

	// land upright, which is also how accumulated trackball roll gets undone.
	// Looking straight down the world up leaves "upright" undefined, so a top
	// view keeps the spin the camera already has instead of picking one.
	glm::vec3 ref = glm::vec3(0.0f, 1.0f, 0.0f);
	if (std::abs(glm::dot(d, ref)) > 0.9999f) ref = getUp();

	glm::vec3 right = glm::cross(ref, d);
	float rightLen = glm::length(right);

	// the fallback up is perpendicular to d by construction, so this only fires
	// if a caller hands in something degenerate
	if (rightLen < 1.0e-6f) {
		right = glm::cross(glm::vec3(0.0f, 0.0f, 1.0f), d);
		rightLen = glm::length(right);
		if (rightLen < 1.0e-6f) return;
	}

	right /= rightLen;

	// columns are where the camera's local axes land, so this is exactly the
	// orientation getRight/getUp/getPosition read back out
	startRotation = rotation;
	startTarget = target;
	targetRotation = glm::normalize(glm::quat_cast(glm::mat3(right, glm::cross(d, right), d)));

	// q and -q are the same orientation but opposite ways round the sphere;
	// flipping the sign here is what makes the slerp take the short way
	if (glm::dot(startRotation, targetRotation) < 0.0f) targetRotation = -targetRotation;

	snapT = 0.0f;
	snapping = true;
}


void Camera3D::snapCamera(float dt) {

	if (!snapping) return;

	glm::quat next = targetRotation;

	if (snapSeconds <= 0.0f) {
		snapping = false;
	}
	else {

		// a stall (a slow frame, a dragged window) must not teleport the camera
		snapT += glm::clamp(dt, 0.0f, 0.1f) / snapSeconds;

		if (snapT >= 1.0f) {
			snapping = false;
		}
		else {
			next = glm::normalize(glm::slerp(startRotation, targetRotation, smoothStep(snapT)));
		}
	}

	// measured from where the snap began rather than from last frame, so
	// rounding cannot accumulate into a drifting view centre partway through
	rotation = next;
	target = pivot + (next * glm::inverse(startRotation)) * (startTarget - pivot);
}


void Camera3D::home() {

	// back onto the model, not onto the world origin -- the two are only the
	// same when the caller never set a pivot
	target = pivot;
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
