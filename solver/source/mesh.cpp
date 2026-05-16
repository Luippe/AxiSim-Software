#include "mesh.h"
#include "shader.h"
#include "time_manager.h"
#include "console.h"
#include "solver_struct.h"
#include "printer.h"
#include <glm/trigonometric.hpp>


Mesh::Mesh(Shader& shader, Config& config) : shader(shader), g(config.g) {
	colFront = 0;
	colBack = g.nz;
	rowTop = g.nr;
	rowBot = 0;
}

void Mesh::clearAll() {
	vertices.clear();
	indices.clear();
}

void Mesh::generate() {

	Clock::time_point startTime = startTimer();

	ntheta = 360.0f / (float)nseg;
	clearAll();

	createVertices();


	console->addCompletionMessage("Completed generating buffers");

	isReady = true;

	float endTime = endTimer(startTime);
	console->addCompletionTime("Mesh", endTime);
}

void Mesh::updateAfterLoadingFile() {

	isReady = true;
	//console->addLine("Successfully loaded mesh");

}



void Mesh::createVertices() {

	int nr = g.nr;
	int nz = g.nz;

	double dr = g.dr;
	double dz = g.dz;

	vertices.clear();
	indices.clear();

	// get all vertices and colors for 2D concentration field
	glm::vec3 coord;
	for (int i = 0; i < nr + 1; i++) {
		for (int j = 0; j < nz + 1; j++) {

			float x = j * dz;
			float y = i * dr;

			coord = { x, y, 0.0f };
			vertices.push_back({ coord });
		}
	}

	// get all indices for 2D concentration field
	for (int i = 0; i < nr; i++) {
		for (int j = 0; j < nz; j++) {

			int botLeft = i * (nz + 1) + j;
			int botRight = botLeft + 1;
			int topLeft = (i + 1) * (nz + 1) + j;
			int topRight = topLeft + 1;

			indices.push_back(topLeft);
			indices.push_back(botLeft);
			indices.push_back(botRight);

			indices.push_back(botRight);
			indices.push_back(topRight);
			indices.push_back(topLeft);

		}
	}
}



void Mesh::drawMesh() {

	if (showMesh) {

	}
}


void Mesh::render() {


	if (!isReady) return;

	//shader.use();
	//shader.SetBool("white", false);

	////if (showFill) {
	////	shader.SetBool("white", true);
	////	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	////	drawMesh();
	////	shader.SetBool("white", false);
	////}

	//glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	//glLineWidth(3.0f);
	//drawMesh();
	//glLineWidth(1.0f);

}
