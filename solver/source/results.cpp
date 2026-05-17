#include "results.h"
#include "mesh.h"
#include "colormap.h"
#include "shader.h"
#include <glm/gtc/matrix_transform.hpp>
#include "printer.h"
#include "time_manager.h"
#include "console.h"
#include "solver.h"

bool compareFloat(float a, float b, CompareType type) {

	constexpr float eps = 1e-6f;

	switch (type) {

	case CompareType::LessThan:
		return a < b;

	case CompareType::EqualTo:
		return std::abs(a - b) < eps;

	case CompareType::GreaterThan:
		return a > b;

	}

	return false;
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

Results::Results(Mesh& mesh, Solver& solver, Colormap& colormap, Shader& shader) :

	colormap(colormap),
	shader(shader),
	solver(solver),
	uField(solver.config.g.nz, solver.config.g.nr),
	vField(solver.config.g.nz, solver.config.g.nr),
	pField(solver.config.g.nz, solver.config.g.nr),
	concField(solver.config.g.nz, solver.config.g.nr),
	mesh(mesh),
	currentField(&uField) {

	colFront = 0;
	colBack = solver.config.g.nz;
	rowTop = solver.config.g.nr;
	rowBot = 0;

	currentFront = (float)colFront * (float)solver.g.dz;
	currentBack = (float)colBack * (float)solver.g.dz;
	currentOuter = (float)rowTop * (float)solver.g.dr;
	currentInner = (float)rowBot * (float)solver.g.dr;

}

void Results::updateAfterLoadingFile() {

	createOutlineVertices();
	createOutlineBuffer();
	createFields();
	isReady = true;

}

void Results::createBuffer() {

	createCylinderTemplate(verticesCV, indicesCV, nseg);

	cvBuffer.createBuffer(verticesCV.size() * sizeof(CylinderTemplateVertex), verticesCV.data());

	cvBuffer.bind(); // bind the VAO you will draw with

	cvElementBuffer.createBuffer(indicesCV.size() * sizeof(unsigned int), indicesCV.data());

	// Per-vertex attributes: locations 0, 1, 2
	cvBuffer.enableAttribute(0, 3, GL_FLOAT, sizeof(CylinderTemplateVertex), (void*)offsetof(CylinderTemplateVertex, dir));
	cvBuffer.enableAttribute(1, 1, GL_FLOAT, sizeof(CylinderTemplateVertex), (void*)offsetof(CylinderTemplateVertex, xCoord));
	cvBuffer.enableAttribute(2, 1, GL_FLOAT, sizeof(CylinderTemplateVertex), (void*)offsetof(CylinderTemplateVertex, radialCoord));

	// ---------------- Instance VBO ----------------
	cvInstanceBuffer.createBuffer(g.nr * g.nz * sizeof(CylinderInstance), nullptr);

	// IMPORTANT:
	cvBuffer.bind(); // bind the VAO you draw with

	glBindBuffer(GL_ARRAY_BUFFER, cvInstanceBuffer.getVBO());
	cvBuffer.enableAttribute(3, 4, GL_FLOAT, sizeof(CylinderInstance), (void*)0);
	glVertexAttribDivisor(3, 1);

	glBindBuffer(GL_ARRAY_BUFFER, cvInstanceBuffer.getVBO());
	glBufferSubData(GL_ARRAY_BUFFER, 0, allInstances.size() * sizeof(CylinderInstance), allInstances.data());

	cvBuffer.unbind();

}

void Results::copyMeshData() {

	// copy variables and structs
	g = mesh.g;
	nseg = mesh.nseg;
	vertices = mesh.vertices;

}

void Results::generate() {

	Clock::time_point startTime = startTimer();

	verticesCV.clear();
	indicesCV.clear();

	// copy all relevant data from mesh class
	copyMeshData();

	// create instances and create buffer
	createAllCVInstances();
	createBuffer();
	console->addCompletionMessage("Completed copying data and generating control volume buffers");

	// generate all fields (values and buffers)
	createFields();
	console->addCompletionMessage("Completed generating field variables");
	updateCurrentField();

	// generate all vertices
	createOutlineVertices();
	console->addCompletionMessage("Completed generating vertices");

	// make buffer for outline
	createOutlineBuffer();
	console->addCompletionMessage("Completed generating buffers");

	// initialize outline and colormaps
	updateOutlineModel();
	uploadUniforms();
	console->addCompletionMessage("Completed initializing outlines and uploading colormaps");

	showOutline = true;
	isReady = true;

	float endTime = endTimer(startTime);
	console->addCompletionTime("Results", endTime);

}

void Results::createFields() {

	uField.generate(solver.uSol, solver.uBC);
	vField.generate(solver.vSol, solver.vBC);
	pField.generate(solver.pSol, solver.pBC);
	//concField.generate(solver.concSol, solver.concBC);
}

void Results::updateCurrentField() {

	switch (currentItem) {
	case 0:
		currentField = &uField;
		break;
	case 1:
		currentField = &vField;
		break;
	case 2:
		currentField = &pField;
		break;
	case 3:
		currentField = &concField;
		break;
	default:
		currentField = &concField;
		currentItem = 3;
		break;
	}

	updateSelectedInstances();
	uploadUniforms();
}

void Results::createOutlineBuffer() {

	// ------------- Cap outline buffers ---------------------
	capBuffer.createBuffer(verticesCap.size() * sizeof(VertexLine), &verticesCap[0]);
	capBuffer.bind();
	capBuffer.enableAttribute(0, 3, GL_FLOAT, sizeof(VertexLine), (void*)0);
	capBuffer.enableAttribute(1, 3, GL_FLOAT, sizeof(VertexLine), (void*)(3 * sizeof(float)));
	capBuffer.unbind();

	// ------------- Edge outline buffers ---------------------
	edgeBuffer.createBuffer(verticesEdge.size() * sizeof(VertexEdge), &verticesEdge[0]);
	edgeBuffer.bind();
	edgeBuffer.enableAttribute(0, 3, GL_FLOAT, sizeof(VertexEdge), (void*)0);
	edgeBuffer.enableAttribute(1, 3, GL_FLOAT, sizeof(VertexEdge), (void*)(3 * sizeof(float)));
	edgeBuffer.enableAttribute(2, 3, GL_FLOAT, sizeof(VertexEdge), (void*)(6 * sizeof(float)));
	edgeBuffer.enableAttribute(3, 3, GL_FLOAT, sizeof(VertexEdge), (void*)(9 * sizeof(float)));
	edgeBuffer.enableAttribute(4, 3, GL_FLOAT, sizeof(VertexEdge), (void*)(12 * sizeof(float)));
	edgeBuffer.unbind();

}

void Results::uploadUniforms() {

	shader.use();
	shader.SetFloat("vmin", currentField->vmin);
	shader.SetFloat("vmax", currentField->vmax);
	shader.SetFloat("R", g.R);
	shader.SetFloat("L", g.L);
	shader.SetInt("fieldTex", 0);
	shader.SetInt("uColormap", 1);

}

void Results::createOutlineVertices() {

	float ntheta = 360.0f / (float)nseg;

	glm::vec3 c0, c1, p0, p1;
	glm::vec3 color = { 0.0f, 0.0f, 0.0f };
	float dr = (float)g.dr;
	float dz = (float)g.dz;

	verticesCap.clear();
	verticesEdge.clear();

	// front cap
	for (int n = 0; n < nseg; n++) {
		p0 = { currentFront, currentOuter * cos(glm::radians(ntheta * n)),		  currentOuter * sin(glm::radians(ntheta * n)) };
		c0 = { currentFront, currentOuter * cos(glm::radians(ntheta * (n + 1))),  currentOuter * sin(glm::radians(ntheta * (n + 1))) };

		verticesCap.push_back({ p0, color });
		verticesCap.push_back({ c0, color });
	}

	// back cap
	for (int n = 0; n < nseg; n++) {
		c0 = { (float)colBack * dz, (float)rowTop * dr * cos(glm::radians(ntheta * (n + 1))), (float)rowTop * dr * sin(glm::radians(ntheta * (n + 1))) };
		p0 = { (float)colBack * dz, (float)rowTop * dr * cos(glm::radians(ntheta * n)),	   (float)rowTop * dr * sin(glm::radians(ntheta * n)) };

		verticesCap.push_back({ p0, color });
		verticesCap.push_back({ c0, color });
	}

	// edges
	for (int n = 0; n < nseg; n++) {
		c0 = { (float)colFront * dz, (float)rowTop * dr * cos(glm::radians(ntheta * (n - 1))), (float)rowTop * dr * sin(glm::radians(ntheta * (n - 1))) };
		c1 = { (float)colFront * dz, (float)rowTop * dr * cos(glm::radians(ntheta * (n + 1))), (float)rowTop * dr * sin(glm::radians(ntheta * (n + 1))) };
		p0 = { (float)colFront * dz, (float)rowTop * dr * cos(glm::radians(ntheta * n)),		(float)rowTop * dr * sin(glm::radians(ntheta * n)) };
		p1 = { (float)colBack * dz, (float)rowTop * dr * cos(glm::radians(ntheta * n)),		(float)rowTop * dr * sin(glm::radians(ntheta * n)) };

		verticesEdge.push_back({ p0, p1, c0, c1, color });

		c0 += (colBack - colFront) * dz * cylinderDirection;
		c1 += (colBack - colFront) * dz * cylinderDirection;

		verticesEdge.push_back({ p1, p0, c0, c1, color });
	}
}

void Results::updateModel() {

	updateOutlineModel();
	updateSelectedInstances();

}

void Results::updateOutlineModel() {
	float dr = (float)g.dr;
	float dz = (float)g.dz;

	float zScale = (currentBack - currentFront) / g.L;
	float rScaleOuter = (currentOuter) / g.R;
	float rScaleInner = (currentInner) / g.R;
	glm::vec3 t = glm::vec3({ currentFront, 0.0f, 0.0f });
	glm::vec3 sOuter = glm::vec3({ zScale, rScaleOuter, rScaleOuter });
	glm::vec3 sInner = glm::vec3({ zScale, rScaleInner, rScaleInner });
	modelOutline = glm::translate(glm::mat4(1.0f), t);

	modelOutlineInner = glm::scale(modelOutline, sInner);	// make sure to compute this before modelOutline
	modelOutline = glm::scale(modelOutline, sOuter);
}

void Results::createAllCVInstances() {

	allInstances.clear();

	for (int i = 0; i < g.nr; i++) {
		for (int j = 0; j < g.nz; j++) {
			int n = i * g.nz + j;

			float x0 = (float)j * (float)g.dz;
			float x1 = (float)(j + 1) * (float)g.dz;

			float r0 = (float)i * (float)g.dr;
			float r1 = (float)(i + 1) * (float)g.dr;

			allInstances.push_back({ x0, x1, r0, r1 });
		}
	}

	//printFloat(instances[0].x0, instances[0].x1, instances[0].innerR, instances[0].outerR);
}

std::vector<CylinderInstance> createRowMergedCylinderInstances(const std::vector<float>& field, int nr, int nz, double dz, double dr, float selectedValue, CompareType type) {

	std::vector<CylinderInstance> instances;

	for (int i = 0; i < nr; i++) {
		int j = 0;

		while (j < nz) {
			int n = i * nz + j;

			if (field[n] <= selectedValue) {
				j++;
				continue;
			}

			// start selected run
			int j0 = j;

			while (j < nz && field[i * nz + j] > selectedValue) {	// we dont use n here, as we want the index to update every loop
				j++;
			}

			int j1 = j;

			CylinderInstance inst{};

			inst.x0 = (float)(j0 * dz);				// front
			inst.x1 = (float)(j1 * dz);				// back

			double r = 0.5 * dr + (double)i * dr;

			inst.innerR = (float)(r - 0.5 * dr);	// inner radius
			inst.outerR = (float)(r + 0.5 * dr);	// outer radius

			//printFloat(inst.x0, inst.x1, inst.innerR, inst.outerR);
			instances.push_back(inst);
		}
	}
	//printFloat(instances[0].x0, instances[0].x1, instances[0].innerR, instances[0].outerR);
	return instances;
}

void Results::updateSelectedInstances() {

	//selectedInstances = {
	//	{
	//		currentFront,   // x0
	//		currentBack,    // x1
	//		currentInner,   // innerR
	//		currentOuter    // outerR
	//	}
	//};

	selectedInstances.clear();
	selectedInstances = createRowMergedCylinderInstances(currentField->cvValues, g.nr, g.nz, g.dz, g.dr, selectedValue, currentCompareType);

	cvInstanceBuffer.bindVBO();
	cvInstanceBuffer.bufferSubData(selectedInstances.size() * sizeof(CylinderInstance), selectedInstances.data());
	cvInstanceBuffer.unbindVBO();

}

void Results::drawCap() {

	if (showOutline) {
		glLineWidth(1.0f);
		capBuffer.bind();
		glDrawArrays(GL_LINES, 0, verticesCap.size());
		capBuffer.unbind();
		glLineWidth(1.0f);
	}
}

void Results::drawEdge() {

	if (showOutline) {
		glLineWidth(1.0f);
		edgeBuffer.bind();
		glDrawArrays(GL_LINES, 0, verticesEdge.size());
		edgeBuffer.unbind();
		glLineWidth(1.0f);
	}
}

void Results::render(Shader& shaderLine, Shader& shaderEdge) {

	if (!isReady) return;
	updateSelectedInstances();	// might be heavy on the cpu, optimize if AxiSim starts lagging

	//GLuint query;
	//glGenQueries(1, &query);
	//glBeginQuery(GL_TIME_ELAPSED, query);
	shader.use();
	uploadUniforms();

	glActiveTexture(GL_TEXTURE0);
	currentField->textureBuffer.bind();
	glActiveTexture(GL_TEXTURE1);
	colormap.bind();

	cvBuffer.bind();

	glDrawElementsInstanced(GL_TRIANGLES, (GLsizei)(indicesCV.size()), GL_UNSIGNED_INT, 0, (GLsizei)(selectedInstances.size()));

	cvBuffer.unbind();

	colormap.unbind();
	currentField->textureBuffer.unbind();

	shaderLine.use();
	shaderLine.SetMat4("model", modelOutline);
	drawCap();
	shaderLine.SetMat4("model", modelOutlineInner);
	drawCap();
	shaderLine.SetMat4("model", glm::mat4(1.0));

	shaderEdge.use();
	shaderEdge.SetMat4("model", modelOutline);
	drawEdge();
	shaderEdge.SetMat4("model", glm::mat4(1.0));


	//// End GPU timer
	//glEndQuery(GL_TIME_ELAPSED);

	//// Get result
	//GLuint64 elapsedTime = 0;
	//glGetQueryObjectui64v(query, GL_QUERY_RESULT, &elapsedTime);

	//double timeMs = elapsedTime / 1'000'000.0;

	//printf("GPU time: %f ms\n", timeMs);

}

