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

	constexpr float PI = 3.14159265358979f;

	// ease in and out, so a snap starts and lands gently instead of stopping dead
	float smoothStep(float t) {
		return t * t * (3.0f - 2.0f * t);
	}

}


Camera3D::Camera3D() {
	initPositionAndAngle();
}

void Camera3D::initPositionAndAngle() {

	// Square on to the x-y plane, looking straight down -z at the orbit target,
	// x to the right and y up. The identity is exactly that orientation: the eye
	// sits at target + rotation * (0, 0, distance) (see getPosition), so leaving
	// the rotation alone puts it on +z looking back. A three-quarter view reads
	// as more of a 3D scene, but this is the view the sketch and the inspector
	// draw -- the axis of revolution across the screen, the radial direction up
	// it -- and starting somewhere else means the model shows up rotated away
	// from the two views it is meant to be read against.
	rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

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

	// which way is up in the world. Normalized up front and kept, because the
	// turntable test in the candidate loop still needs it after `ref` below has
	// been reassigned to something else.
	const float upLen = glm::length(worldUp);
	const glm::vec3 up = (upLen > 1.0e-6f) ? (worldUp / upLen) : glm::vec3(0.0f, 1.0f, 0.0f);

	// Start from the upright frame for this direction. The reference is always a
	// WORLD axis, never where the camera happens to be pointing: looking
	// straight down world up leaves "upright" undefined, and taking the
	// camera's own up there would build the frame around whatever arbitrary
	// angle the horizon was already at -- a top view would snap the direction
	// but leave the up vector off-axis. Falling back to another world axis
	// keeps all four candidates below on axes too.
	glm::vec3 ref = up;

	if (std::abs(glm::dot(d, ref)) > 0.9999f) {

		// d is world up itself, so build from whichever remaining axis is least
		// parallel to it -- the one that gives the steadiest cross product
		const glm::vec3 z(0.0f, 0.0f, 1.0f);
		const glm::vec3 x(1.0f, 0.0f, 0.0f);

		ref = (std::abs(glm::dot(d, z)) < std::abs(glm::dot(d, x))) ? z : x;
	}

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

	// ---- the swing: where the view direction lands once it faces `d` ----
	// The slerp turns the whole orientation at once, but the LANDING still has to
	// be chosen, and choosing it is a question about roll alone -- `d` is where
	// the camera is going whichever roll wins. `afterSwing` below is the view
	// swung onto `d` carrying no extra roll, the reference the candidate rolls are
	// measured from. Its axis also settles the opposite case: which way a half
	// turn goes round is fixed here, and the slerp then follows it.
	const glm::vec3 d0 = rotation * glm::vec3(0.0f, 0.0f, 1.0f);

	glm::vec3 m = glm::cross(d0, d);
	float mLen = glm::length(m);

	float swing = std::atan2(mLen, glm::clamp(glm::dot(d0, d), -1.0f, 1.0f));

	if (mLen > 1.0e-6f) {
		m /= mLen;
	}
	else if (glm::dot(d0, d) > 0.0f) {

		// already looking down the axis, so there is nothing to swing -- only
		// the roll below, if the click was to straighten the view up
		m = up;
		swing = 0.0f;
	}
	else {

		// exactly opposite: every axis through the middle arrives, so take the
		// one that swings the camera round the side rather than up over the top
		glm::vec3 side = (std::abs(glm::dot(up, d0)) > 0.9f) ? getRight() : up;

		// square it against d0, so the half turn lands on -d0 and not near it
		side -= d0 * glm::dot(side, d0);

		const float sideLen = glm::length(side);
		if (sideLen < 1.0e-6f) return;

		m = side / sideLen;
		swing = PI;
	}

	const glm::quat afterSwing = glm::normalize(glm::angleAxis(swing, m) * rotation);

	// ---- the landing roll ----
	// The direction is settled; the roll about it is not. Landing strictly
	// upright bundles a roll correction into what was asked to be a change of
	// viewing direction, and from an upside-down view that correction is a half
	// turn the camera has to sit through for nothing. So all four quarter turns
	// are candidates and the smallest roll wins. Home is what resets the
	// orientation outright.
	const glm::vec3 upright = glm::cross(d, right);
	const glm::vec3 candidates[4] = { upright, right, -upright, -right };

	glm::quat landing = rotation;
	float roll = 0.0f;

	bool found = false;

	for (int i = 0; i < 4; i++) {

		const glm::vec3 landUp = candidates[i];
		const glm::vec3 landRight = glm::cross(landUp, d);

		// A turntable is only a turntable while the camera's right stays
		// perpendicular to world up -- that is the whole no-roll guarantee (see
		// turntableDrag), and it is load-bearing for more than tidiness: let the
		// right vector line up with world up and the yaw and pitch axes become
		// the same axis, so both drag directions do the one thing and the view
		// cannot be turned the other way at all. A quarter turn that would land
		// there is not a landing a turntable may make. Looking straight down
		// world up none of them do, and all four stay in.
		if (rotationStyle == RotationStyle::Turntable &&
			std::abs(glm::dot(landRight, up)) > 1.0e-3f) continue;

		// columns are where the camera's local axes land, so this is exactly the
		// orientation getRight/getUp/getPosition read back out
		const glm::quat q = glm::normalize(glm::quat_cast(glm::mat3(landRight, landUp, d)));

		// Measured AFTER the swing, which is the only place it means anything:
		// what is left to do once the camera has arrived is a roll and nothing
		// else, so the candidate needing least of it is the shortest landing.
		// Comparing up vectors from where the camera is now instead answers a
		// different question -- one that ignores that the view has to travel.
		const float r = rollBetween(afterSwing, q, d);

		if (!found || std::abs(r) < std::abs(roll)) {
			roll = r;
			landing = q;
			found = true;
		}
	}

	// +-upright always keeps the right vector off world up, so the loop cannot
	// come away empty -- but nothing downstream should have to know that
	if (!found) return;

	startRotation = rotation;
	startTarget = target;

	// q and -q are the same orientation but opposite ways round the sphere;
	// flipping the sign here is what makes the slerp take the short way
	targetRotation = (glm::dot(startRotation, landing) < 0.0f) ? -landing : landing;

	snapT = 0.0f;
	snapping = true;
}


float Camera3D::rollBetween(const glm::quat& from, const glm::quat& to, const glm::vec3& axis) {

	// q and -q are the same orientation but opposite ways round the sphere;
	// flipping the sign is what makes the turn come out the short way
	const glm::quat a = (glm::dot(to, from) < 0.0f) ? -to : to;

	const glm::quat r = glm::normalize(a * glm::inverse(from));

	const glm::vec3 v(r.x, r.y, r.z);
	const float s = glm::length(v);

	// the flip above leaves r.w >= 0 -- it IS dot(a, from) -- so the angle comes
	// out in [0, pi] and never the reflex way round
	const float angle = 2.0f * std::atan2(s, r.w);

	return (glm::dot(v, axis) < 0.0f) ? -angle : angle;
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

			// One slerp of the whole orientation: the geodesic, the least the
			// view can turn and still arrive. `targetRotation` is already the
			// roll-minimised landing (see snapToAxis) put in the same hemisphere
			// as the start, so the short way round is the direct interpolation.
			//
			// Where the landing carries a roll -- a turntable snapping from a
			// pole view onto a side, which can only arrive the right way up --
			// this folds that roll into the one turn instead of running it off
			// while the view swings across, so the scene lands in a single clean
			// move rather than swinging wide and rolling as it travels.
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


void Camera3D::frameTo(const glm::vec3& centre, float radius) {

	pivot = centre;
	target = centre;

	// an in-flight snap interpolates from where the view centre was when it
	// started, so leaving one running here would drag the framing back out
	snapping = false;

	if (!(radius > 0.0f)) return;

	const float tanHalf = std::tan(glm::radians(fov) * 0.5f);
	if (tanHalf < 1.0e-6f) return;

	// viewHalfHeight() is distance * tan(fov/2) and BOTH projections are built
	// from it, so solving for the distance that makes it the radius frames the
	// scene identically whichever one is active
	float d = radius / tanHalf;

	// height is only half the story: a panel taller than it is wide runs out of
	// width first, and the model has to fit across as well
	const float aspect = (float)std::max(width, 1) / (float)std::max(height, 1);
	if (aspect < 1.0f) d /= std::max(aspect, 1.0e-3f);

	// same 15% margin the inspector leaves, so the two views read as one framing
	distance = glm::clamp(d * 1.15f, minDistance, maxDistance);
}


void Camera3D::movePivot(const glm::vec3& newPivot) {

	// carrying the centre by the same step is what keeps this from being a
	// camera move: the pivot ends up in exactly the same place in the view as
	// before, so a model that grew or shrank under the camera does not drag the
	// framing sideways with it
	target += newPivot - pivot;
	pivot = newPivot;
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
