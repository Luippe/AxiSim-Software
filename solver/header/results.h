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
	const char* fieldType[3] = { "Axial Velocity", "Radial Velocity", "Pressure" };

	Results(Mesh& mesh, Solver& solver, Colormap& colormap, Shader& shader);

	// get all vertices for the outline
	void createOutlineVertices();

	// create buffer for the cylinder outline
	void createOutlineBuffer();

	void render(Shader& shaderLine, Shader& shaderEdge);

	// generate results using mesh and solution
	void generate();

	// updates the outline model matrix so it can change with the mesh
	void updateOutlineModel();

	// upload colormap to shader
	void uploadColormap();

	// update the results after loading a file
	void updateAfterLoadingFile();

	std::vector<Vertex> vertices;
	std::vector<Vertex> verticesCV;
	std::vector<VertexEdge> verticesEdge;
	std::vector<VertexLine> verticesCap;
	std::vector<ControlVolume> cv;
	std::vector<unsigned int> indicesCV;

	glm::vec3 cylinderDirection = { 1.0f, 0.0f, 0.0f };
	glm::mat4 modelOutline = glm::mat4(1.0f);
	glm::mat4 modelOutlineInner = glm::mat4(1.0f);
	bool show = true;
	bool showOutline = false;
	bool isReady = false;

	Field* currentField;
	TextureBuffer currentTextureBuffer;
	Console* console = nullptr;

	// update all relevant variables
	void updateCurrentVariables();

private:

	int nseg;
	int nz;
	int nr;
	double dz;
	double dr;

	//Mesh mesh;
	//Solver solver;
	Solution sol;
	GridConfig g;

	Shader& shader;
	Colormap& colormap;

	VertexBuffer capBuffer;
	VertexBuffer edgeBuffer;
	VertexBuffer cvBuffer;
	ElementBuffer cvElementBuffer;
	Field uField;
	Field vField;
	Field pField;
	Field concField;
	Solver& solver;
	Mesh& mesh;

	// updates vmin and vmax
	void updateMinMax();

	void createFields();

	// create cv buffer using the copied data
	void createCVBuffer();
	
	// copy the cv variables from the mesh class
	void copyData();

	void draw();
	void drawCap();
	void drawEdge();

};