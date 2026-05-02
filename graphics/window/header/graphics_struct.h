#pragma once
#include <glm/vec3.hpp>

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
