#pragma once
#include <vector>
#include "buffer_manager.h"
#include "graphics_struct.h"
#include "field_manager.h"
#include <glm/mat4x4.hpp>

class Colormap;
class Shader;
class Console;
class Mesh;
class Solver;

class Results {
public:

	int colFront;
	int colBack;
	int rowTop;
	int rowBot;

	float currentOuter;
	float currentFront;
	float currentBack;
	float currentInner;

	int currentItem = 0;
	CompareType currentCompareType = CompareType::GreaterThan;
	const char* fieldType[3] = { "Axial Velocity", "Radial Velocity", "Pressure" };
	const char* compareType[3] = { "Less Than", "Equal To", "Greater Than" };
	
	Results(Mesh& mesh, Solver& solver, Colormap& colormap, Shader& shader);

	// get all vertices for the outline
	void createOutlineVertices();

	// create buffer for the cylinder outline
	void createOutlineBuffer();

	void render(Shader& shaderLine, Shader& shaderEdge);

	// generate results using mesh and solution
	void generate();

	void updateModel();

	// upload colormap to shader
	void uploadUniforms();

	// update the results after loading a file
	void updateAfterLoadingFile();

	// update all relevant variables
	void updateCurrentField();

	// update instances for instanced rendering
	void updateSelectedInstances();

	std::vector<Vertex> vertices;
	std::vector<CylinderTemplateVertex> verticesCV;
	std::vector<unsigned int> indicesCV;
	std::vector<VertexEdge> verticesEdge;
	std::vector<VertexLine> verticesCap;
	std::vector<ControlVolume> cv;
	std::vector<CylinderInstance> allInstances;
	std::vector<CylinderInstance> selectedInstances;

	glm::vec3 cylinderDirection = { 1.0f, 0.0f, 0.0f };
	glm::mat4 modelOutline = glm::mat4(1.0f);
	glm::mat4 modelOutlineInner = glm::mat4(1.0f);
	bool show = true;
	bool showOutline = false;
	bool isReady = false;
	bool isMultipleInstancing = false;

	Field* currentField;
	Console* console = nullptr;

	Shader& shader;
	Colormap& colormap;

private:

	int nseg;
	double dz;
	double dr;

	//Mesh mesh;
	//Solver solver;
	GridConfig g;


	VertexBuffer capBuffer;
	VertexBuffer edgeBuffer;
	VertexBuffer cvBuffer;
	VertexBuffer cvInstanceBuffer;
	ElementBuffer cvElementBuffer;

	Field uField;
	Field vField;
	Field pField;
	Field concField;
	Solver& solver;
	Mesh& mesh;

	// updates the outline model matrix so it can change with the mesh
	void updateOutlineModel();



	// create instances for all control volumes
	void createAllCVInstances();

	// generate all fields
	void createFields();

	// copy buffer from the mesh class
	void createBuffer();
	
	// copy the cv variables from the mesh class
	void copyMeshData();


	void drawCap();
	void drawEdge();

};