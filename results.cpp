#include "results.h"
#include "mesh.h"
#include "colormap.h"
#include "shader.h"
#include <glm/gtc/matrix_transform.hpp>
#include "printer.h"
#include "time_manager.h"
#include "console.h"
#include "solver.h"

Results::Results(Mesh& mesh, Solver& solver, Colormap& colormap, Shader& shader) :
	colormap(colormap),
	shader(shader),
	solver(solver),
	uField(solver.config),
	vField(solver.config),
	pField(solver.config),
	concField(solver.config){

	this->colFront = mesh.colFront;
	this->colBack = mesh.colBack;
	this->rowTop = mesh.rowTop;
	this->rowBot = mesh.rowBot;
	this->currentOuter = mesh.currentOuter;
	this->currentFront = mesh.currentFront;
	this->currentBack = mesh.currentBack;
	this->currentInner = mesh.currentInner;

}

void Results::copyData(Mesh& mesh, Solver& solver) {

	// copy variables and structs
	g = mesh.g;
	nseg = mesh.nseg;
	cv = mesh.cv;
	vertices = mesh.vertices;
	verticesCV = mesh.verticesCV;
	indicesCV = mesh.indicesCV;

	// deep copy buffer
	cvBuffer.createBuffer(verticesCV.size() * sizeof(Vertex), &verticesCV[0]);
	cvBuffer.bind();
	cvBuffer.enableAttribute(0, 3, GL_FLOAT, sizeof(Vertex), (void*)0);
	cvElementBuffer.createBuffer(indicesCV.size() * sizeof(unsigned int), &indicesCV[0]);
	cvBuffer.unbind();

}

void Results::generate(Mesh& mesh, Solver& solver) {

	Clock::time_point startTime = startTimer();

	copyData(mesh, solver);

	// generate all vertices
	createOutlineVertices();
	console->addCompletionMessage("Completed generating vertices");

	// generate all fields (values and buffers)
	createFields();
	console->addCompletionMessage("Completed generating field variables");
	updateCurrentVariables();

	// make buffer for outline
	createOutlineBuffer();
	console->addCompletionMessage("Completed generating buffers");

	// initialize outline and colormaps
	updateOutlineModel();
	uploadColormap();
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

void Results::updateCurrentVariables() {

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

	currentTextureBuffer = currentField->textureBuffer;
	uploadColormap();
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

void Results::uploadColormap() {

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


void Results::draw() {
	//printf("%d\n", cv.size());
	if (show) {
		cvBuffer.bind();
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
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

	glActiveTexture(GL_TEXTURE0);
	currentTextureBuffer.bind();

	glActiveTexture(GL_TEXTURE1);
	colormap.bind();

	shader.use();
	draw();

	colormap.unbind();
	currentTextureBuffer.unbind();

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

}