#include "bounding.h"
#include "graphics_struct.h"
#include "shader.h"

Bounding::Bounding(Renderer& render) : render(render) {
	createAxisBB();
	for (int i = 0; i < axisBB.size(); i++) {
		unsigned int VAO;
		createBoxBuffer(axisBB[i], VAO);
		VAOBB.push_back(VAO);
	}
}

BoundingBox Bounding::createBounds(glm::vec3 pos1, glm::vec3 pos2, int ID) {
	BoundingBox box = { pos1, pos2, ID };
	return box;
}

void Bounding::createAxisBB() {
	const int axis_length = render.axis_length;
	const float side = 0.1;
	int ID = 0;

	// 6 bounding box in total. 2 for each axis
	// bounding box for negative x,y,z axis
	for (int i = 0; i < 3; i++) {
		glm::vec3 pos1(-side);
		glm::vec3 pos2(side);

		pos1[i] = -axis_length - side;
		pos2[i] = -side;

		axisBB.push_back(createBounds(pos1, pos2, ID));
		ID++;
	}

	// bounding box for positive x,y,z axis
	for (int i = 0; i < 3; i++) {
		glm::vec3 pos1(-side);
		glm::vec3 pos2(side);

		pos1[i] = side;
		pos2[i] = axis_length + side;

		axisBB.push_back(createBounds(pos1, pos2, ID));
		ID++;
	}
}

void Bounding::createBoxBuffer(BoundingBox& box, unsigned int& VAO) {
	unsigned int VBO;

	// back p0, p1, p3
	glm::vec3 p0 = box.min;
	glm::vec3 p1 = glm::vec3(box.max.x, box.min.y, box.min.z);
	glm::vec3 p2 = glm::vec3(box.max.x, box.max.y, box.min.z);
	glm::vec3 p3 = glm::vec3(box.min.x, box.max.y, box.min.z);

	glm::vec3 p4 = box.max;
	glm::vec3 p5 = glm::vec3(box.max.x, box.min.y, box.max.z);
	glm::vec3 p6 = glm::vec3(box.min.x, box.min.y, box.max.z);
	glm::vec3 p7 = glm::vec3(box.min.x, box.max.y, box.max.z);

	glm::vec3 color = { 1.0f, 0.0f, 0.0f };

	std::vector<VertexLine> vertices = { {p0, color},
									{p1, color},
									{p3, color},
									{p2, color},
									{p3, color},
									{p1, color},

									{p6, color},
									{p7, color},
									{p5, color},
									{p4, color},
									{p5, color},
									{p7, color},

									{p0, color},
									{p3, color},
									{p6, color},
									{p7, color},
									{p6, color},
									{p3, color},

									{p1, color},
									{p2, color},
									{p5, color},
									{p4, color},
									{p5, color},
									{p2, color},

									{p0, color},
									{p6, color},
									{p1, color},
									{p5, color},
									{p1, color},
									{p6, color},

									{p3, color},
									{p7, color},
									{p2, color},
									{p4, color},
									{p2, color},
									{p7, color} };

	//bbBuffer.createBuffer(vertices.size() * sizeof(Vertex), &vertices[0]);
	//bbBuffer.bind();
	//bbBuffer.enableAttribute(0, 3, GL_FLOAT, sizeof(Vertex), (void*)0);
	//bbBuffer.enableAttribute(1, 3, GL_FLOAT, sizeof(Vertex), (void*)(3 * sizeof(float)));
	//bbBuffer.enableAttribute(2, 3, GL_FLOAT, sizeof(Vertex), (void*)(6 * sizeof(float)));
	//bbBuffer.unbind();

	glGenVertexArrays(1, &VAO);
	glGenBuffers(1, &VBO);

	glBindVertexArray(VAO);

	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(VertexLine), &vertices[0], GL_STATIC_DRAW);

	// vertex positions
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(VertexLine), (void*)0);

	// vertex colors
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(VertexLine), (void*)(3 * sizeof(float)));

	glBindVertexArray(0);
}

void Bounding::renderBB(Shader& shaderLine) {

	if (showBB) {
		shaderLine.use();
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

		for (int i = 0; i < VAOBB.size(); i++) {
			glBindVertexArray(VAOBB[i]);
			glDrawArrays(GL_TRIANGLES, 0, 36);	// all bounding box has 36 vertices
			glBindVertexArray(0);
		}
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	}
}
