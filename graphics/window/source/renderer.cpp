#include "renderer.h"
#include "shader.h"
#include "graphics_struct.h"


Renderer::Renderer() {
	createAxisBuffer();
}

void Renderer::createAxisBuffer() {

	const std::vector<VertexLine> vertices = { {glm::vec3(-axis_length, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f)},
										 {glm::vec3(axis_length, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f)},
										 {glm::vec3(0.0f, axis_length, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)},
										 {glm::vec3(0.0f, -axis_length, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)},
										 {glm::vec3(0.0f, 0.0f, axis_length), glm::vec3(0.0f, 0.0f, 1.0f)},
										 {glm::vec3(0.0f, 0.0f, -axis_length), glm::vec3(0.0f, 0.0f, 1.0f)} };
	axisBuffer.createBuffer(vertices.size() * sizeof(VertexLine), &vertices[0]);
	axisBuffer.bind();
	axisBuffer.enableAttribute(0, 3, GL_FLOAT, sizeof(VertexLine), (void*)0);
	axisBuffer.enableAttribute(1, 3, GL_FLOAT, sizeof(VertexLine), (void*)(3 * sizeof(float)));
	axisBuffer.unbind();
}

void Renderer::renderAxis(Shader& shaderLine) {
	shaderLine.use();
	glLineWidth(lineWidth);
	axisBuffer.bind();
	glDrawArrays(GL_LINES, 0, 6);
	axisBuffer.unbind();
	glLineWidth(1.0f);
}