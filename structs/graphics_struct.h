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

struct LengthScale {
	uint8_t index = 0;
	float value = 1.0f;
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