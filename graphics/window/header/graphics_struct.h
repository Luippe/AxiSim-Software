#pragma once
#include <glm/vec3.hpp>
#include <glm/vec2.hpp>

enum class CompareType {
	LessThan,
	EqualTo,
	GreaterThan
};

struct InspectorPoint {
	glm::vec2 position;
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

struct ControlVolume {

	// size of each control volume
	int indexCount;

	// starting index
	int frontStart;
	int outerStart;
	int innerStart;
	int backStart;

	// number of indices
	int frontCount;
	int outerCount;
	int innerCount;
	int backCount;
};

enum ViewTab {
	TAB_MESH = 0,
	TAB_SOLVER = 1,
	TAB_RESULTS = 2
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
