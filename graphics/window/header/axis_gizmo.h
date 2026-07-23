#pragma once

#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <vector>

#include "buffer_manager.h"
#include "graphics_struct.h"
#include "shader.h"

// A solid 3D coordinate triad: three arrows (cylindrical shaft + conical head)
// meeting at a sphere hub.
//
// The mesh is built ONCE in unit space -- each arm runs from the origin to
// length 1.0 along its axis -- so a caller sizes and places it purely with the
// model matrix. Two ready-made ways to draw it:
//
//   drawAtOrigin(...)  world-space triad sitting in the scene, scaled by the
//                      caller.
//   drawOverlay(...)   fixed-size navigation triad pinned to the bottom-right
//                      of a viewport, rotating with the camera but immune to
//                      pan and zoom. This one is clickable -- see pickOverlay.
//
// Style fields are plain data; change any of them and call generate() to
// rebuild. Axis colors follow the usual X=red, Y=green, Z=blue convention.
class AxisGizmo {
public:

	// arm indices, also the ids baked into the vertex data for hover highlighting
	enum Arm {
		ArmNone = -1,
		ArmPosX = 0,
		ArmPosY = 1,
		ArmPosZ = 2,
		ArmNegX = 3,
		ArmNegY = 4,
		ArmNegZ = 5,
		ArmCount = 6
	};

	AxisGizmo();

	// rebuild the mesh from the current style fields
	void generate();

	// world-space triad. `model` should already contain whatever scale/offset
	// puts the unit-length arms where they belong in the scene.
	void drawAtOrigin(const glm::mat4& model, const glm::mat4& view, const glm::mat4& projection);

	// corner navigation triad. `sceneView` is the scene's view matrix -- only its
	// rotation is used, so the triad tracks camera orientation but ignores pan
	// and zoom.
	//
	// Restores the previous viewport and clears only its own square of depth, so
	// it can be called at the end of a frame without disturbing the scene.
	void drawOverlay(const glm::mat4& sceneView, int viewportWidth, int viewportHeight);

	// Hit-test the corner triad. `localMouse` is in pixels from the TOP-LEFT of
	// the viewport image; `viewportWidth/Height` must match the drawOverlay call.
	// Returns the Arm under the cursor, or ArmNone.
	//
	// The overlay is orthographic with a rotation-only view, so an arm projects
	// to a straight 2D segment and the test is a plain point-to-segment distance
	// -- no ray casting needed.
	int pickOverlay(const glm::mat4& sceneView, const glm::vec2& localMouse, int viewportWidth, int viewportHeight) const;

	// The direction the camera should sit on to face a picked arm, ready to hand
	// to Camera3D::snapToAxis. When the camera already looks down that direction
	// the opposite is returned, so the arms reach both sides.
	glm::vec3 snapAxis(int arm, const glm::mat4& sceneView) const;

	// unit direction an arm points along
	glm::vec3 armDirection(int arm) const;

	// true when `localMouse` (pixels from the TOP-LEFT of the viewport image)
	// falls inside the overlay's square
	bool overlayContains(const glm::vec2& localMouse, int viewportWidth, int viewportHeight) const;

	// ---------------- state ----------------

	// arm drawn brightened, set from a hover test; ArmNone for no highlight.
	// Cheap to change -- it is a uniform, not a rebuild.
	int highlightArm = ArmNone;

	// The -X/-Y/-Z arms are built into the mesh but sit at the tail of the vertex
	// buffer, so revealing them on hover is a change of draw count -- no rebuild,
	// no second buffer. They are pickable exactly when they are visible.
	bool showNegativeArms = false;

	// ---------------- style (call generate() after changing) ----------------

	int segments = 24;			// radial tessellation of shafts and heads
	int hubRings = 12;			// latitude bands on the hub sphere

	// all lengths/radii are fractions of one arm (arm length == 1.0)
	float shaftRadius = 0.028f;
	float headRadius = 0.085f;
	float headLength = 0.26f;
	float hubRadius = 0.075f;

	// the -X/-Y/-Z arms are scaled copies of the positive ones, dimmed so the
	// three real axes stay dominant while they are showing
	float negativeArmLength = 0.55f;
	float negativeDim = 0.55f;

	glm::vec3 colorX = glm::vec3(0.87f, 0.20f, 0.20f);
	glm::vec3 colorY = glm::vec3(0.24f, 0.72f, 0.24f);
	glm::vec3 colorZ = glm::vec3(0.22f, 0.38f, 0.90f);
	glm::vec3 colorHub = glm::vec3(0.35f, 0.72f, 0.78f);

	// how much color survives on faces pointing away from the eye
	float ambient = 0.35f;

	// ---------------- overlay placement (no rebuild needed) ----------------

	static constexpr int minOverlaySizePx = 60;
	static constexpr int maxOverlaySizePx = 300;
	static constexpr int defaultOverlaySizePx = 200;

	// side of the square the corner triad occupies, in viewport pixels
	int overlaySizePx = defaultOverlaySizePx;

	// gap from the viewport's right and bottom edges
	int overlayMarginPx = 8;

	int vertexCount() const { return (int)vertices.size(); }

private:

	// pixel square the overlay occupies, in OpenGL (bottom-left origin) coords.
	// Returns false when the viewport is too small to place it.
	bool overlayRect(int viewportWidth, int viewportHeight, int& x, int& y, int& size) const;

	// half-width of the overlay's orthographic box, in gizmo units
	float overlayHalfExtent() const;

	// how far an arm reaches: a full arm is 1.0, a negative one is shorter
	float armLength(int arm) const;

	// vertices to draw this frame -- everything, or everything up to the
	// negative arms when they are hidden
	int drawCount() const;

	// append a truncated cone (a cylinder when r0 == r1) spanning [x0, x1] along
	// local +X, then map it into gizmo space with `basis`
	void addFrustum(const glm::mat3& basis, float x0, float x1, float r0, float r1, const glm::vec3& color, float id);

	// append a flat disk at local x, facing local -X, mapped through `basis`
	void addDisk(const glm::mat3& basis, float x, float radius, const glm::vec3& color, float id);

	// append one full arrow (shaft + head + head base) along local +X
	void addArm(const glm::mat3& basis, const glm::vec3& color, float id);

	// append the sphere at the origin
	void addHub();

	void push(const glm::vec3& position, const glm::vec3& normal, const glm::vec3& color, float id);

	void upload();

	std::vector<VertexShaded> vertices;

	// first vertex of the negative arms; everything before it is always drawn
	int negativeStart = 0;

	Shader shader;
	VertexBuffer buffer;
};
