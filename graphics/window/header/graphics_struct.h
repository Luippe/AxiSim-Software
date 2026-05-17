#pragma once
#include <glm/vec3.hpp>
#include <glm/vec2.hpp>
#include "imgui.h"
#include "buffer_manager.h"

enum class CompareType {
	LessThan,
	EqualTo,
	GreaterThan
};

struct InspectorPoint {
	ImVec2 mousePos;
	ImVec2 dataPos;
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

struct AppAssets {
	TextureBuffer houseIcon;

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
