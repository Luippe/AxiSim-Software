#include "results.h"

#include <cmath>

#include <glm/vec2.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "solver.h"
#include "mesh.h"

#include "console.h"

#include "printer.h"
#include "time_manager.h"


Results::Results(Config& config) :

	config(config) {
}

void createCylinderTemplate(std::vector<CylinderTemplateVertex>& vertices, std::vector<unsigned int>& indices, int nseg) {

	vertices.clear();
	indices.clear();

	if (nseg < 3) return;

	constexpr float PI = 3.14159265359f;
	vertices.reserve(static_cast<size_t>(8 * nseg + 4));
	indices.reserve(static_cast<size_t>(24 * nseg));

	std::vector<glm::vec2> circle(static_cast<size_t>(nseg) + 1);
	for (int k = 0; k <= nseg; ++k) {
		const float theta = 2.0f * PI * static_cast<float>(k) / static_cast<float>(nseg);
		circle[k] = glm::vec2(std::cos(theta), std::sin(theta));
	}

	auto addVertex = [&](const glm::vec2& p, float xCoord, float radialCoord) {
		vertices.push_back({
			glm::vec3(0.0f, p.x, p.y),
			xCoord,
			radialCoord
			});
		};

	auto addTriangle = [&](unsigned int a, unsigned int b, unsigned int c) {
		indices.push_back(a);
		indices.push_back(b);
		indices.push_back(c);
		};

	auto addWall = [&](float radialCoord, bool reverseWinding) {
		const unsigned int base = static_cast<unsigned int>(vertices.size());

		for (int k = 0; k <= nseg; ++k) {
			addVertex(circle[k], 0.0f, radialCoord);
			addVertex(circle[k], 1.0f, radialCoord);
		}

		for (int k = 0; k < nseg; ++k) {
			const unsigned int a = base + 2 * k;
			const unsigned int b = base + 2 * k + 1;
			const unsigned int c = base + 2 * (k + 1);
			const unsigned int d = base + 2 * (k + 1) + 1;

			if (reverseWinding) {
				addTriangle(a, b, c);
				addTriangle(c, b, d);
			}
			else {
				addTriangle(a, c, b);
				addTriangle(c, d, b);
			}
		}
		};

	auto addCap = [&](float xCoord, bool frontFace) {
		const unsigned int base = static_cast<unsigned int>(vertices.size());

		for (int k = 0; k < nseg; ++k) {
			addVertex(circle[k], xCoord, 0.0f);
			addVertex(circle[k], xCoord, 1.0f);
		}

		for (int k = 0; k < nseg; ++k) {
			const unsigned int i0 = base + 2 * k;
			const unsigned int o0 = base + 2 * k + 1;
			const unsigned int i1 = base + 2 * ((k + 1) % nseg);
			const unsigned int o1 = base + 2 * ((k + 1) % nseg) + 1;

			if (frontFace) {
				addTriangle(i0, o1, o0);
				addTriangle(i0, i1, o1);
			}
			else {
				addTriangle(i0, o0, o1);
				addTriangle(i0, o1, i1);
			}
		}
		};

	addWall(1.0f, false);
	addWall(0.0f, true);
	addCap(0.0f, true);
	addCap(1.0f, false);
}


void Results::updateAfterLoadingFile() {

	isReady = true;

}

void Results::setTextureShadingAllField(GLint shadingMode) {

	//results.currentField->textureBuffer.setTextureShading(shadingMode);
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
	solutions = solver.solutions;

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

	for (const std::string& name : fieldType) {

		SolutionField& field = solutions[name];

		// generate new field
		Field newField;
		newField.generate(solutions[name], solver.fvMesh, mesh.boundaryGroups);

		// insert new field
		fields[name] = newField;

	}
}

void Results::updateCurrentField() {

	if (fieldType.empty()) return;

	std::string name = fieldType[currentItem];
	currentField = &fields[name];

}
