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

	struct FilterValues {
		float valueAt = 0.0;
		float valueLower = 0.0;
		float valueUpper = 0.0;
	};

	FilterValues filterValues;

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
	ShadingType currentShadingType = ShadingType::Interp;
	const char* fieldType[3] = { "Axial Velocity", "Radial Velocity", "Pressure" };
	const char* compareType[5] = { "Less Than", "Equal To", "Greater Than", "Between", "Exclude"};
	const char* shadingType[2] = { "Interp", "Flat" };
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

	// update current buffer with new data, as texture buffers cannot be copied over, they must be updated instead
	void updateTextureBuffer(const void* data);

	std::vector<Vertex> vertices;
	std::vector<CylinderTemplateVertex> verticesCV;
	std::vector<unsigned int> indicesCV;
	std::vector<VertexEdge> verticesEdge;
	std::vector<VertexLine> verticesCap;
	std::vector<CylinderInstance> allInstances;
	std::vector<CylinderInstance> selectedInstances;

	glm::vec3 cylinderDirection = { 1.0f, 0.0f, 0.0f };
	glm::mat4 modelOutline = glm::mat4(1.0f);
	glm::mat4 modelOutlineInner = glm::mat4(1.0f);

	bool show = true;
	bool showOutline = false;
	bool isReady = false;
	bool isMultipleInstancing = false;

	Field* currentField = nullptr;
	Console* console = nullptr;

	Shader& shader;
	Colormap& colormap;


private:




	int nseg;
	double dz;
	double dr;
	int nr, nz;

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

	// reduce number of instances by combining control volume for rows
	std::vector<CylinderInstance> createRowMergedCylinderInstances(std::vector<float>& field, FilterValues& filterValues);

	bool compareFloat(float value, FilterValues& filterValues);

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