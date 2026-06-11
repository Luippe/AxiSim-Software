#include "results.h"

#include <glm/gtc/matrix_transform.hpp>

#include "solver.h"
#include "mesh.h"

#include "console.h"

#include "printer.h"
#include "time_manager.h"


Results::Results(Config& config) :

	config(config),
	currentField(&uField) {

	colFront = 0;
	colBack = config.g.nz;
	rowTop = config.g.nr;
	rowBot = 0;

	currentFront = 0.0f;
	currentBack = 0.0f;
	currentOuter = 0.0f;
	currentInner = 0.0f;

}

void createCylinderTemplate(std::vector<CylinderTemplateVertex>& vertices, std::vector<unsigned int>& indices, int nseg) {

	vertices.clear();
	indices.clear();

	if (nseg < 3) return;

	const float PI = 3.14159265359f;

	auto addVertex = [&](float c, float s, float xCoord, float radialCoord) {
		vertices.push_back({
			glm::vec3(0.0f, c, s),
			xCoord,
			radialCoord
			});
		};

	// outer
	{
		unsigned int base = (unsigned int)(vertices.size());

		for (int k = 0; k <= nseg; ++k) {
			float theta = 2.0f * PI * float(k) / float(nseg);
			float c = std::cos(theta);
			float s = std::sin(theta);

			addVertex(c, s, 0.0f, 1.0f); // front outer
			addVertex(c, s, 1.0f, 1.0f); // back outer
		}

		for (int k = 0; k < nseg; ++k) {
			unsigned int a = base + 2 * k;
			unsigned int b = base + 2 * k + 1;
			unsigned int c = base + 2 * (k + 1);
			unsigned int d = base + 2 * (k + 1) + 1;

			// outward-facing winding
			indices.push_back(a);
			indices.push_back(c);
			indices.push_back(b);

			indices.push_back(c);
			indices.push_back(d);
			indices.push_back(b);
		}
	}

	// inner
	{
		unsigned int base = (unsigned int)(vertices.size());

		for (int k = 0; k <= nseg; ++k) {
			float theta = 2.0f * PI * float(k) / float(nseg);
			float c = std::cos(theta);
			float s = std::sin(theta);

			addVertex(c, s, 0.0f, 0.0f); // front inner
			addVertex(c, s, 1.0f, 0.0f); // back inner
		}

		for (int k = 0; k < nseg; ++k) {
			unsigned int a = base + 2 * k;
			unsigned int b = base + 2 * k + 1;
			unsigned int c = base + 2 * (k + 1);
			unsigned int d = base + 2 * (k + 1) + 1;

			// reversed winding for inner wall
			indices.push_back(a);
			indices.push_back(b);
			indices.push_back(c);

			indices.push_back(c);
			indices.push_back(b);
			indices.push_back(d);
		}
	}

	// front face
	{
		unsigned int base = (unsigned int)(vertices.size());

		for (int k = 0; k < nseg; ++k) {
			float theta = 2.0f * PI * float(k) / float(nseg);
			float c = std::cos(theta);
			float s = std::sin(theta);

			addVertex(c, s, 0.0f, 0.0f); // front inner
			addVertex(c, s, 0.0f, 1.0f); // front outer
		}

		for (int k = 0; k < nseg; ++k) {
			unsigned int i0 = base + 2 * k;
			unsigned int o0 = base + 2 * k + 1;

			unsigned int i1 = base + 2 * ((k + 1) % nseg);
			unsigned int o1 = base + 2 * ((k + 1) % nseg) + 1;

			// front cap, -x facing
			indices.push_back(i0);
			indices.push_back(o1);
			indices.push_back(o0);

			indices.push_back(i0);
			indices.push_back(i1);
			indices.push_back(o1);
		}
	}

	// back face
	{
		unsigned int base = (unsigned int)(vertices.size());

		for (int k = 0; k < nseg; ++k) {
			float theta = 2.0f * PI * float(k) / float(nseg);
			float c = std::cos(theta);
			float s = std::sin(theta);

			addVertex(c, s, 1.0f, 0.0f); // back inner
			addVertex(c, s, 1.0f, 1.0f); // back outer
		}

		for (int k = 0; k < nseg; ++k) {
			unsigned int i0 = base + 2 * k;
			unsigned int o0 = base + 2 * k + 1;

			unsigned int i1 = base + 2 * ((k + 1) % nseg);
			unsigned int o1 = base + 2 * ((k + 1) % nseg) + 1;

			// back cap, +x facing
			indices.push_back(i0);
			indices.push_back(o0);
			indices.push_back(o1);

			indices.push_back(i0);
			indices.push_back(o1);
			indices.push_back(i1);
		}
	}
}


void Results::updateAfterLoadingFile() {

	isReady = true;

}

void Results::updateTextureBuffer(const void* data) {
	currentField->textureBuffer.updateBuffer(g.nz + 1, g.nr + 1, GL_RED, GL_FLOAT, data);
}

void Results::copyData(const Mesh& mesh, const Solver& solver) {

	// copy variables and structs
	g = mesh.g;
	nseg = mesh.nseg;
	nr = g.nr;
	nz = g.nz;
	dr = g.dr;
	dz = g.dz;

	fieldType = solver.fieldType;

}

void Results::generate(Mesh& mesh, Solver& solver) {

	Clock::time_point startTime = startTimer();

	verticesCV.clear();
	indicesCV.clear();

	// copy all relevant data from mesh class
	copyData(mesh, solver);

	// create instances and create buffer
	createCylinderTemplate(verticesCV, indicesCV, nseg);

	// generate all fields (values and buffers)
	createFields(mesh, solver);
	updateCurrentField();

	console->addCompletionMessage("Completed generating field variables");

	isReady = true;

	float endTime = endTimer(startTime);
	console->addCompletionTime("Results", endTime);

}

void Results::createFields(const Mesh& mesh, const Solver& solver) {

	uField.generate(solver.uSol, solver.fvMesh, mesh.boundaryGroups, solver.boundaryGroupBCs);
	vField.generate(solver.vSol, solver.fvMesh, mesh.boundaryGroups, solver.boundaryGroupBCs);
	pField.generate(solver.pSol, solver.fvMesh, mesh.boundaryGroups, solver.boundaryGroupBCs);
	if (solver.fieldOption.solveEnergy) {
		tempField.generate(solver.tempSol, solver.fvMesh, mesh.boundaryGroups, solver.boundaryGroupBCs);
	}
}

void Results::updateCurrentField() {

	if (fieldType.empty()) return;

	std::string currentFieldChar = fieldType[currentItem];

	if (currentFieldChar == "Axial Velocity") {
		currentField = &uField;
	}
	else if (currentFieldChar == "Radial Velocity") {
		currentField = &vField;
	}
	else if (currentFieldChar == "Pressure") {
		currentField = &pField;
	}
	else if (currentFieldChar == "Temperature") {
		currentField = &tempField;
	}
	else if (currentFieldChar == "Concentration") {
		currentField = &concField;
	}
	else {
		printf("ERROR: UNIDENTIFIED FIELD");
	}
}