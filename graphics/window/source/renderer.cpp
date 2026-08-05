#include "renderer.h"
#include "shader.h"
#include "graphics_struct.h"

#include <vector>


Renderer::Renderer() {
	createDottedAxisBuffer();
}

void Renderer::createDottedAxisBuffer() {

	std::vector<VertexLine> vertices;

	const glm::vec3 black(0.0f, 0.0f, 0.0f);

	const glm::vec3 axes[3] = {
		glm::vec3(1.0f, 0.0f, 0.0f),
		glm::vec3(0.0f, 1.0f, 0.0f),
		glm::vec3(0.0f, 0.0f, 1.0f)
	};

	const float step = dashLength + gapLength;

	// each axis is a run of dashes from the origin out to +-1 either way; a
	// caller scales unit space to the length it wants
	for (const glm::vec3& dir : axes) {
		for (float sign = 1.0f; sign >= -1.0f; sign -= 2.0f) {
			for (float t = 0.0f; t < 1.0f; t += step) {
				const float t1 = std::min(t + dashLength, 1.0f);
				vertices.push_back({ sign * t * dir, black });
				vertices.push_back({ sign * t1 * dir, black });
			}
		}
	}

	dottedVertexCount = (int)vertices.size();

	dottedBuffer.createBuffer(vertices.size() * sizeof(VertexLine), vertices.data());
	dottedBuffer.bind();
	dottedBuffer.enableAttribute(0, 3, GL_FLOAT, sizeof(VertexLine), (void*)0);
	dottedBuffer.enableAttribute(1, 3, GL_FLOAT, sizeof(VertexLine), (void*)(3 * sizeof(float)));
	dottedBuffer.unbind();
}

void Renderer::renderDottedAxes(Shader& shaderLine) {

	if (dottedVertexCount == 0) return;

	shaderLine.use();
	glLineWidth(lineWidth);
	dottedBuffer.bind();
	glDrawArrays(GL_LINES, 0, dottedVertexCount);
	dottedBuffer.unbind();
	glLineWidth(1.0f);
}
