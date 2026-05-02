#pragma once
#include <glm/vec3.hpp>
#include "renderer.h"
#include "buffer_manager.h"

class Shader;

struct BoundingBox {	// BB
	glm::vec3 min;
	glm::vec3 max;
	int ID;
};

// classes for creating bounding box and handle events when clicked
class Bounding {
public:

	Bounding(Renderer& render);

	std::vector<BoundingBox> axisBB;
	std::vector<BoundingBox> dataBB;
	std::vector<unsigned int> VAOBB;
	void renderBB(Shader& shaderLine);

	bool showBB = false;

private:
	Renderer& render;

	VertexBuffer bbBuffer;
	void createAxisBB();

	// create and return a bounding box with opposite corners pos1 and pos2
	BoundingBox createBounds(glm::vec3 pos1, glm::vec3 pos2, int ID);

	// create buffer for bounding box
	void createBoxBuffer(BoundingBox& box, unsigned int& VAO);
};
