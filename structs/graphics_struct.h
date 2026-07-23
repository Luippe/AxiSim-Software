#pragma once
#include <glm/vec3.hpp>
#include <glm/vec2.hpp>
#include "imgui.h"		// ImVec2 used by SurfacePoint below


enum class CompareType {
	None,
	LessThan,
	EqualTo,
	GreaterThan,
	Between,
	Exclude
};

enum class ShadingType {
	Interp,
	Flat
};

// Which value range the colormap and colorbar are normalized against during
// transient playback.
//   Global -- min/max over the WHOLE run. Colors mean the same thing in every
//             frame and the colorbar is static, so frames are comparable.
//   Local  -- each frame's own min/max. Maximizes contrast within a frame, at
//             the cost of the colorbar rescaling as the animation plays.
// Only meaningful while an animation exists; a steady result has one range.
enum class ColorRangeMode {
	Global,
	Local
};

struct LengthScale {
	uint8_t index = 0;
	float value = 1.0f;
};

// How the results scene is viewed, saved with the project so a reopened one
// looks the way it was left.
//
// These mirror the scene camera's ProjectionType and RotationStyle (camera.h)
// but are held as plain bytes on purpose: this struct is written to disk, and
// the values must keep meaning the same thing even if those enums later gain a
// member in the middle. The camera owns the live setting -- View > Results
// writes both it and the mirror, and a project load pushes the mirror back out
// (see Project::applySceneViewSettings).
struct SceneViewSettings {

	enum : uint8_t { Perspective = 0, Orthographic = 1 };
	enum : uint8_t { Turntable = 0, Arcball = 1 };

	uint8_t projection = Orthographic;
	uint8_t rotationStyle = Turntable;
};

struct SurfacePoint {

	// i and j indices
	ImVec2 dataPos;

	// optional data
	ImVec2 vecValue;
	float value;
};

struct Vertex {
	glm::vec3 position;
};

struct VertexEdge {
	glm::vec3 p0;
	glm::vec3 p1;
	glm::vec3 c0;
	glm::vec3 c1;
	glm::vec3 color;
};

struct VertexLine {
	glm::vec3 position;
	glm::vec3 color;
};

// solid geometry that carries its own color and is lit in the shader
// (axis gizmo arms, hub)
struct VertexShaded {
	glm::vec3 position;
	glm::vec3 normal;
	glm::vec3 color;

	// caller-defined group id, so one draw call can highlight a subset of the
	// mesh from a uniform instead of re-uploading colors (gizmo hover)
	float id;
};

enum class ViewTab {
	TAB_GEOMETRY = 0,
	TAB_MESH = 1,
	TAB_SOLVER = 2,
	TAB_RESULTS = 3,
	TAB_COUNT = 4
};

struct CylinderInstance {
	float x0;
	float x1;
	float innerR;
	float outerR;
};

struct CylinderTemplateVertex {
	glm::vec3 dir;          // (0, cos(theta), sin(theta))
	float xCoord;           // 0 = front, 1 = back
	float radialCoord;      // 0 = inner, 1 = outer
};

struct FilterValues {
	float valueAt = 0.0;
	float valueLower = 0.0;
	float valueUpper = 0.0;
};