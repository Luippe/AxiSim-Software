#pragma once
#include "pch.h"
#include "math_func.h"


enum class ProjectionType {
	Perspective,
	Orthographic
};


// Orbit ("turntable") camera for the 3D scene.
//
// The orientation is two angles, not a free quaternion: yaw about world +Y and
// pitch about the camera's own right vector, with pitch clamped just short of
// vertical. That is the whole reason the view stays predictable -- a trackball
// accumulates roll as you drag in circles, so the horizon slowly tips over and
// the same drag stops producing the same motion. Two angles cannot roll, and
// every drag is reproducible.
//
// `rotation` is derived from those angles and kept up to date for anything that
// wants a quaternion.
class Camera3D {

public:
	Camera3D();

	// ------------------------- input -------------------------

	// orbit by a mouse drag, in screen pixels with y pointing down
	void calculateRotation(float dx, float dy);

	// slide the orbit target across the view plane. dx is pixels to the right,
	// dy is pixels UP (callers pass -MouseDelta.y). The scene tracks the cursor
	// one-to-one because the step is derived from the actual view size, not a
	// magic constant.
	void calculatePan(float dx, float dy);

	// dolly in/out along the view direction
	void calculateZoom(double yoffset);

	// ------------------------- framing -------------------------

	// begin a smooth move to look down `axis`, a unit world direction pointing
	// from the orbit target toward where the camera should end up
	void snapToAxis(const glm::vec3& axis);

	// advance an in-flight snap. `dt` is seconds since the last frame, so the
	// move takes the same wall-clock time regardless of frame rate.
	void snapCamera(float dt);

	bool isSnapping() const { return snapping; }

	// go back to the starting three-quarter view
	void home();

	// ------------------------- state -------------------------

	ProjectionType projectionType = ProjectionType::Perspective;

	glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
	glm::vec3 position = glm::vec3(0.0f, 0.0f, 1.0f);
	glm::mat4 model = glm::mat4(1.0f);
	glm::mat4 view = glm::mat4(1.0f);
	glm::mat4 projection = glm::mat4(1.0f);

	float fov = 45.0f;
	float distance = 1.0f;

	// degrees of orbit per pixel dragged
	float rotateSensitivity = 0.4f;

	// wall-clock seconds an axis snap takes
	float snapSeconds = 0.28f;

	// set width and height of scene
	void setDimensions(int w, int h, ImVec2 pos);

	// update model, view, and projection matrix
	void updateTransformationMatrix();

	glm::vec3 getPosition() const;
	glm::vec3 getRight() const;
	glm::vec3 getUp() const;
	glm::vec3 getFront() const;

	// half-height of the visible region at the orbit target, in world units.
	// Both projections are built from this, so switching between them does not
	// change how big anything looks.
	float viewHalfHeight() const;

	// world units covered by one screen pixel at the orbit target
	float worldPerPixel() const;

private:

	int width = 1;
	int height = 1;
	ImVec2 rectPos = ImVec2(0.0f, 0.0f);

	// turntable angles, radians
	float yaw = 0.0f;
	float pitch = 0.0f;

	// just short of straight up/down, so the up vector never flips and lookAt
	// never degenerates
	static constexpr float maxPitch = 1.56905099f;	// 89.9 degrees

	static constexpr float minDistance = 0.01f;
	static constexpr float maxDistance = 500.0f;

	// snap interpolation, in angle space rather than quaternion space so it
	// cannot introduce roll on the way
	bool snapping = false;
	float snapT = 0.0f;
	float startYaw = 0.0f;
	float startPitch = 0.0f;
	float targetYaw = 0.0f;
	float targetPitch = 0.0f;

	// initialize position and angle of camera when constructing class
	void initPositionAndAngle();

	// rebuild `rotation` after yaw or pitch changes
	void updateRotation();

	glm::vec3 target = glm::vec3(0.0f, 0.0f, 0.0f);
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

	// set an absolute zoom level (world units per pixel), clamped to the valid
	// range. Used to snap to a specific zoom, e.g. a 1-unit grid spacing.
	void setZoom(double newUnitsPerPixel);

	// set width, height, and top-left position of the 2D viewport
	void setDimensions(int w, int h, ImVec2 pos);

	Vec2 center = Vec2{ 0.0, 0.0 };
	double unitsPerPixel = 0.001;

	// initialize position and angle of camera when constructing class
	void initPosition();

private:

	int width = 1;
	int height = 1;
	ImVec2 rectPos = ImVec2(0.0f, 0.0f);

	double minUnitsPerPixel = 1e-9;
	double maxUnitsPerPixel = 1.0;



};
