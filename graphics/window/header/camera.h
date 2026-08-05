#pragma once
#include "pch.h"
#include "math_func.h"


enum class ProjectionType : uint8_t{
	Perspective,
	Orthographic
};


// How a mouse drag becomes a rotation. Neither one clamps anything, so both
// reach every orientation -- they differ in whether roll is reachable at all.
enum class RotationType : uint8_t {

	// yaw about world up, pitch about the camera's own right vector. Cannot
	// roll, so the horizon never tips and the same drag always does the same
	// thing. Rides over the pole and keeps going.
	Turntable,

	// virtual trackball, as in Fluent: the drag direction AND where in the
	// viewport it happens both matter, and a drag near the border rolls the
	// view. Reaches rolled orientations a turntable cannot.
	Arcball
};


// Orbit camera for the 3D scene, with two ways to rotate it -- see
// RotationType. Both are applied the same way, as an incremental world-space
// turn about `pivot`, so everything downstream is shared.
//
// `rotation` IS the orientation, a free quaternion, and the only state a drag
// touches. No yaw/pitch angles anywhere: the arcball needs orientations a
// two-angle state cannot express, and the turntable does not need them -- it
// keeps its no-roll guarantee through an invariant on the axes it turns about
// (see turntableDrag) rather than by storing angles. That is also why nothing
// is clamped: the usual "stop just short of vertical" exists to keep an
// up-vector from flipping, and neither style here has an up-vector to flip.
//
// Consequence worth knowing: switching style mid-session is free and jump-free,
// but the turntable inherits whatever roll the arcball left behind -- it never
// adds roll, and it never takes it away either. What clears it is Home, or any
// snap made while the turntable is the live style: those land only on
// orientations the turntable can go on driving (see snapToAxis).
class Camera3D {

public:
	Camera3D();

	// ------------------------- input -------------------------

	// orbit by a mouse drag, in whichever style `rotationType` selects. Both
	// positions are in pixels from the TOP-LEFT of the viewport, y pointing
	// down. The endpoints are passed rather than the delta because the arcball
	// needs them -- the same delta turns the view differently depending on
	// where in the viewport it happens. The turntable only reads the delta.
	void calculateRotation(const glm::vec2& prevPx, const glm::vec2& curPx);

	// same drag, but locked to one world axis (Fluent's X/Y/Z modifier keys):
	// the part of the turn that would spin the scene about `worldAxis` is kept
	// and the rest is dropped, so a drag across the axis does nothing.
	void calculateRotationAbout(const glm::vec3& worldAxis, const glm::vec2& prevPx, const glm::vec2& curPx);

	// slide the view centre across the view plane. dx is pixels to the right,
	// dy is pixels UP (callers pass -MouseDelta.y). The scene tracks the cursor
	// one-to-one because the step is derived from the actual view size, not a
	// magic constant.
	//
	// This deliberately leaves `pivot` alone: panning changes what you are
	// looking at, not what the view turns about.
	void calculatePan(float dx, float dy);

	// dolly in/out along the view direction
	void calculateZoom(double yoffset);

	// ------------------------- framing -------------------------

	// begin a smooth move to look down `axis`, a unit world direction pointing
	// from the orbit target toward where the camera should end up. The roll it
	// lands with is whichever quarter turn about `axis` is the shortest move
	// from here -- and, on a turntable, one the turntable can go on driving.
	void snapToAxis(const glm::vec3& axis);

	// advance an in-flight snap. `dt` is seconds since the last frame, so the
	// move takes the same wall-clock time regardless of frame rate.
	void snapCamera(float dt);

	bool isSnapping() const { return snapping; }

	// Back to the starting view: square on to the x-y plane, looking straight
	// down -z at the pivot. This is the one place the orientation is reset
	// outright, so it is also what clears accumulated roll.
	void home();

	// look at `centre` from far enough away that a sphere of `radius` about it
	// fits the viewport. Moves the pivot there too: framing something says both
	// what you are looking at and what you expect to turn about.
	void frameTo(const glm::vec3& centre, float radius);

	// Follow a pivot that has moved -- the model was re-meshed, or the display
	// length unit changed under it. The view centre travels the same step, so
	// nothing jumps and the pivot keeps the place in the view the user last put
	// it: this tracks a model, it does not re-aim the camera (frameTo does).
	void movePivot(const glm::vec3& newPivot);

	// ------------------------- state -------------------------

	// orthographic by default: this is a measurement view, and parallel lines
	// staying parallel is worth more here than the depth cue. Switchable from
	// View -> Projection.
	ProjectionType projectionType = ProjectionType::Orthographic;

	// World point every turn happens about -- a drag, an axis snap, all of it.
	// Callers park it on the middle of the model and leave it there; panning
	// does NOT drag it along, so the scene keeps spinning about the model
	// however far the view has slid off it. That is what Fluent and
	// DesignModeler do, and it is why this is separate from the view centre:
	// pan moves the centre, nothing moves the pivot but the caller.
	glm::vec3 pivot = glm::vec3(0.0f, 0.0f, 0.0f);

	glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
	glm::vec3 position = glm::vec3(0.0f, 0.0f, 1.0f);
	glm::mat4 model = glm::mat4(1.0f);
	glm::mat4 view = glm::mat4(1.0f);
	glm::mat4 projection = glm::mat4(1.0f);

	float fov = 45.0f;
	float distance = 1.0f;

	RotationType rotationType = RotationType::Turntable;

	// Which way is up for a turntable yaw. NOT the model's axis of revolution
	// (that is world X) -- this is which way is up on screen, and it matches
	// both the starting view and the navigation triad.
	glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f);

	// TURNTABLE: degrees of orbit per pixel dragged
	float rotateSensitivity = 0.22f;

	// ARCBALL: how much of the trackball turn to apply. 1.0 would be
	// one-to-one, keeping the point you grabbed under the cursor; below that
	// the scene lags the cursor, which is easier to aim with.
	float rotateGain = 0.55f;

	// ARCBALL: size of the virtual sphere, as a fraction of half the SHORTER
	// viewport side. Below 1.0 the sphere's rim sits inside the viewport,
	// leaving a band around the edge that rolls the view.
	float trackballRadius = 0.85f;

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

	static constexpr float minDistance = 0.01f;
	static constexpr float maxDistance = 500.0f;

	// snap interpolation, slerped between two orientations. `startTarget` is
	// where the view centre was when the snap began -- the whole snap is one
	// turn about the pivot measured from there, so the centre cannot drift as
	// it interpolates.
	bool snapping = false;
	float snapT = 0.0f;
	glm::quat startRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
	glm::quat targetRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
	glm::vec3 startTarget = glm::vec3(0.0f, 0.0f, 0.0f);

	// Signed turn about `axis` that takes orientation `from` to orientation
	// `to`, taking the short way round. Only meaningful when the two differ by a
	// turn about `axis` alone; anything off that axis is dropped.
	static float rollBetween(const glm::quat& from, const glm::quat& to, const glm::vec3& axis);

	// initialize position and angle of camera when constructing class
	void initPositionAndAngle();

	// turn the camera by a world-space rotation, swinging the view centre --
	// and so the eye with it -- around `pivot`. Every rotation goes through
	// here, which is what keeps "turns about the model, not about the middle of
	// the screen" true for drags and snaps alike.
	void applyTurn(const glm::quat& worldTurn);

	// the two drag styles. `delta` is in pixels, y pointing down.
	void turntableDrag(const glm::vec2& delta);
	void arcballDrag(const glm::vec2& prevPx, const glm::vec2& curPx);

	// map a viewport pixel onto the virtual trackball, in camera space (x right,
	// y up, z out of the screen)
	glm::vec3 trackballPoint(const glm::vec2& px) const;

	// turn a drag into the rotation it asks the SCENE for, in camera space.
	// False when the drag is too small to mean anything.
	bool trackballDelta(const glm::vec2& prevPx, const glm::vec2& curPx, glm::vec3& axisCam, float& angle) const;

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
