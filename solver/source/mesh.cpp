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
	verticesCV.clear();
	indices.clear();
	indicesCV.clear();
	cv.clear();
}

void Mesh::generate() {

	Clock::time_point startTime = startTimer();

	ntheta = 360.0f / (float)nseg;
	clearAll();

	createVertices();
	createCVIndex();
	console->addCompletionMessage("Completed generating " + std::to_string(verticesCV.size()) + " vertices and " + std::to_string(indicesCV.size()) + " indices");

	createBuffer();

	console->addCompletionMessage("Completed generating buffers");

	isReady = true;

	float endTime = endTimer(startTime);
	console->addCompletionTime("Mesh", endTime);
}


void Mesh::updateAfterLoadingFile() {

	createBuffer();
	isReady = true;
	//console->addLine("Successfully loaded mesh");

}

void Mesh::createBuffer() {

	// ------------- Control Volume buffers ---------------------
	cvBuffer.createBuffer(verticesCV.size() * sizeof(Vertex), &verticesCV[0]);
	cvBuffer.bind();
	cvBuffer.enableAttribute(0, 3, GL_FLOAT, sizeof(Vertex), (void*)0);
	cvElementBuffer.createBuffer(indicesCV.size() * sizeof(unsigned int), &indicesCV[0]);
	cvBuffer.unbind();

}

void Mesh::createVertices() {

	int nr = g.nr;
	int nz = g.nz;

	double dr = g.dr;
	double dz = g.dz;
	float val = 0.0f;

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

	// ------------------------------- 3D -------------------------------------
	// get all vertices and color for 3D concentration field
	// generate and store the face triangles first, then the outer walls
	int index1, index2, index3, index4, index5, index6, index7, index8;

	// first layer control volume vertices
	for (int j = 0; j < nz; j++) {
		int ifront = 1 * (nz + 1) + j;		// index of 2d concentration field
		int iback = 1 * (nz + 1) + j + 1;
	
		// front face
		coord = { (float)(j * dz), 0.0f, 0.0f };
		verticesCV.push_back({ coord });	// center point
		for (int n = 0; n < nseg; n++) {
			coord = { (float)(j * dz), dr * cos(glm::radians(ntheta * n)), dr * sin(glm::radians(ntheta * n)) };
			verticesCV.push_back({ coord });
		}

		// outer wall
		for (int n = 0; n < nseg; n++) {
			coord = { (float)(j * dz), dr * cos(glm::radians(ntheta * n)), dr * sin(glm::radians(ntheta * n)) };
			verticesCV.push_back({ coord });
		}
		for (int n = 0; n < nseg; n++) {
			coord = { (float)((j + 1) * dz), dr * cos(glm::radians(ntheta * n)), dr * sin(glm::radians(ntheta * n)) };
			verticesCV.push_back({ coord });
		}

		// back face
		coord = { (float)((j + 1) * dz), 0.0f, 0.0f };
		verticesCV.push_back({ coord  });	// center point
		for (int n = 0; n < nseg; n++) {
			coord = { (float)((j + 1) * dz), dr * cos(glm::radians(ntheta * n)), dr * sin(glm::radians(ntheta * n)) };
			verticesCV.push_back({ coord });
		}
	}

	// first layer indices. make sure to store them in a counter clock wise direction
	for (int j = 0; j < nz; j++) {

		// front face
		for (int n = 0; n < nseg; n++) {

			index1 = j * (4 * nseg + 2);								// center
			index2 = index1 + n + 1;									// top left
			index3 = index2 - n + ((n + 1) % nseg);						// top right

			// store face into cv
			indicesCV.push_back(index1);
			indicesCV.push_back(index3);
			indicesCV.push_back(index2);
		}

		// outer wall
		for (int n = 0; n < nseg; n++) {
			index2 = j * (4 * nseg + 2) + nseg + 1 + n;			// top left
			index3 = index2 - n  + ((n + 1) % nseg);	// top right
			index4 = index2 + nseg;					// next top left
			index5 = index3 + nseg;					// next top right

			// store outer wall into cv
			indicesCV.push_back(index3);
			indicesCV.push_back(index4);
			indicesCV.push_back(index2);

			indicesCV.push_back(index4);
			indicesCV.push_back(index3);
			indicesCV.push_back(index5);
		}

		// back wall
		for (int n = 0; n < nseg; n++) {

			index1 = j * (4 * nseg + 2) + 3 * nseg + 1;							// center
			index2 = index1 + n + 1;									// top left
			index3 = index2 - n + ((n + 1) % nseg);						// top right

			// store face into cv
			indicesCV.push_back(index1);
			indicesCV.push_back(index2);
			indicesCV.push_back(index3);
		}
	}
	
	// vertices for rest of the layer
	for (int i = 1; i < nr; i++) {
		for (int j = 0; j < nz; j++) {

			int ifront = i * (nz + 1) + j;		// index of 2d concentration field
			int iback = i * (nz + 1) + j + 1;

			// front face
			for (int n = 0; n < nseg; n++) {
				coord = { (float)(j * dz), (i + 1) * dr * cos(glm::radians(ntheta * n)), (i + 1) * dr * sin(glm::radians(ntheta * n)) };
				verticesCV.push_back({ coord });
			}

			// outer wall
			for (int n = 0; n < nseg; n++) {
				coord = { (float)(j * dz), (i + 1) * dr * cos(glm::radians(ntheta * n)), (i + 1) * dr * sin(glm::radians(ntheta * n)) };
				verticesCV.push_back({ coord});
			}
			for (int n = 0; n < nseg; n++) {
				coord = { (float)((j + 1) * dz), (i + 1) * dr * cos(glm::radians(ntheta * n)), (i + 1) * dr * sin(glm::radians(ntheta * n)) };
				verticesCV.push_back({ coord });
			}

			// inner wall
			for (int n = 0; n < nseg; n++) {
				coord = { (float)(j * dz), -i * dr * cos(glm::radians(ntheta * n)), -i * dr * sin(glm::radians(ntheta * n)) };
				verticesCV.push_back({ coord });
			}
			for (int n = 0; n < nseg; n++) {
				coord = { (float)((j + 1) * dz), -i * dr * cos(glm::radians(ntheta * n)), -i * dr * sin(glm::radians(ntheta * n)) };
				verticesCV.push_back({ coord });
			}

			// back face
			for (int n = 0; n < nseg; n++) {
				coord = { (float)((j + 1) * dz), (i + 1) * dr * cos(glm::radians(ntheta * n)), (i + 1) * dr * sin(glm::radians(ntheta * n)) };
				verticesCV.push_back({ coord });
			}
		}
	}

	// indicies for rest of the layer. requires rectangles for both face and wall
	int firstRowSize = nz * (4 * nseg + 2);	// equal to index of last vertex of first row
	int rowSize = nz * (6 * nseg);
	for (int i = 1; i < nr; i++) {

		for (int j = 0; j < nz; j++) {

			// front face
			for (int n = 0; n < nseg; n++) {

				// face triangles
				index1 = firstRowSize + (i - 1) * rowSize + j * (6 * nseg) + n;	// top left
				index2 = index1 - n + ((n + 1) % nseg);								// top right
				if (i == 1) {
					index3 = j * (4 * nseg + 2) + n + 1;						// bot left
				}
				else {
					index3 = firstRowSize + (i - 2) * rowSize + j * (6 * nseg) + n;		// bot left
				}
				index4 = index3 - n + ((n + 1) % nseg);									// bot right

				// store control volumes
				indicesCV.push_back(index3);
				indicesCV.push_back(index2);
				indicesCV.push_back(index1);

				indicesCV.push_back(index2);
				indicesCV.push_back(index3);
				indicesCV.push_back(index4);
			}

			// outer wall
			for (int n = 0; n < nseg; n++) {
					
				index1 = firstRowSize + (i - 1) * rowSize + j * (6 * nseg) + n + nseg;			// top left
				index2 = index1 - n + ((n + 1) % nseg);						// top right
				index3 = index1 + nseg;										// next top left
				index4 = index2 + nseg;										// next top right

				// outer wall
				indicesCV.push_back(index2);
				indicesCV.push_back(index3);
				indicesCV.push_back(index1);

				indicesCV.push_back(index3);
				indicesCV.push_back(index2);
				indicesCV.push_back(index4);
			}

			// inner wall. winding order is opposite
			for (int n = 0; n < nseg; n++) {

				index1 = firstRowSize + (i - 1) * rowSize + j * (6 * nseg) + n + 3 * nseg;			// bot left
				index2 = index1 - n + ((n + 1) % nseg);						// bot right
				index3 = index1 + nseg;										// next bot left
				index4 = index2 + nseg;										// next bot right

				// outer wall
				indicesCV.push_back(index1);
				indicesCV.push_back(index3);
				indicesCV.push_back(index2);

				indicesCV.push_back(index4);
				indicesCV.push_back(index2);
				indicesCV.push_back(index3);
			}

			// back face
			for (int n = 0; n < nseg; n++) {

				index1 = firstRowSize + (i - 1) * rowSize + j * (6 * nseg) + n + 5 * nseg;	// top left
				index2 = index1 - n + ((n + 1) % nseg);								// top right
				if (i == 1) {
					index3 = j * (4 * nseg + 2) + 3 * nseg + 2 + n;			// bot left
				}
				else {
					index3 = firstRowSize + (i - 2) * rowSize + j * (6 * nseg) + n + 5 * nseg;		// bot left
				}
				index4 = index3 - n + ((n + 1) % nseg);									// bot right

				// store control volumes
				indicesCV.push_back(index3);
				indicesCV.push_back(index2);
				indicesCV.push_back(index4);

				indicesCV.push_back(index2);
				indicesCV.push_back(index3);
				indicesCV.push_back(index1);
			}
		}
	}
}

// get data needed to draw a control volume
// need to obtain the face, outer wall, and inner wall triangles
void Mesh::createCVIndex() {
	
	int nr = g.nr;
	int nz = g.nz;

	int start = 0;
	int size;
	
	int frontStart;
	int outerStart;
	int innerStart;
	int backStart;

	int frontCount;
	int outerCount;
	int innerCount;
	int backCount;

	for (int i = 0; i < nr; i++) {

		if (i == 0) {
			size = 3 * nseg + 6 * nseg + 3 * nseg;
			frontCount = 3 * nseg;
			outerCount = 6 * nseg;
			innerCount = 0;
			backCount = 3 * nseg;
		}
		else {
			size = 6 * nseg + 12 * nseg + 6 * nseg;
			frontCount = 6 * nseg;
			outerCount = 6 * nseg;
			innerCount = 6 * nseg;
			backCount = 6 * nseg;
		}

		for (int j = 0; j < nz; j++) {

			frontStart = start;
			outerStart = start + frontCount;
			innerStart = start + frontCount + outerCount;
			backStart = start + frontCount + outerCount + innerCount;

			cv.push_back({ size, frontStart, outerStart, innerStart, backStart, frontCount, outerCount, innerCount, backCount});
			start += size;

		}
	}
}

void Mesh::drawMesh() {

	if (showMesh) {
		cvBuffer.bind();

		//glDrawElements(GL_TRIANGLES, 12, GL_UNSIGNED_INT, (void*)(0 * sizeof(unsigned int)));
		for (int i = rowBot; i < rowTop; i++) {
			for (int j = colFront; j < colBack; j++) {
				int n = i * g.nz + j;
				if (i == rowTop - 1) {
					glDrawElements(GL_TRIANGLES, cv[n].outerCount, GL_UNSIGNED_INT, (void*)(cv[n].outerStart * sizeof(unsigned int)));
				}
				if (j == colFront) {
					glDrawElements(GL_TRIANGLES, cv[n].frontCount, GL_UNSIGNED_INT, (void*)(cv[n].frontStart * sizeof(unsigned int)));
				}
				if (j == colBack - 1) {
					glDrawElements(GL_TRIANGLES, cv[n].backCount, GL_UNSIGNED_INT, (void*)(cv[n].backStart * sizeof(unsigned int)));
				}
				if (i == rowBot) {
					glDrawElements(GL_TRIANGLES, cv[n].innerCount, GL_UNSIGNED_INT, (void*)(cv[n].innerStart * sizeof(unsigned int)));
				}
			}
		}
		cvBuffer.unbind();
	}
}


void Mesh::render() {
	if (!isReady) return;

	shader.use();
	shader.SetBool("white", false);

	if (showFill) {
		shader.SetBool("white", true);
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		drawMesh();
		shader.SetBool("white", false);
	}

	glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	glLineWidth(3.0f);
	drawMesh();
	glLineWidth(1.0f);

}
