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

	std::vector<std::string> fieldType;
	CompareType currentCompareType = CompareType::GreaterThan;
	ShadingType currentShadingType = ShadingType::Interp;
	const char* compareType[5] = { "Less Than", "Equal To", "Greater Than", "Between", "Exclude"};
	const char* shadingType[2] = { "Interp", "Flat" };
	Results(Mesh& mesh, Solver& solver, Colormap& colormap, Shader& shader);

	// create buffer for the cylinder outline
	void createOutlineBuffer();

	void render();

	// generate results using mesh and solution
	void generate();

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
	std::vector<double> dz;
	std::vector<double> dr;
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
	Field tempField;
	Solver& solver;
	Mesh& mesh;

	// reduce number of instances by combining control volume for rows
	std::vector<CylinderInstance> createRowMergedCylinderInstances(std::vector<float>& field, FilterValues& filterValues);

	bool compareFloat(float value, FilterValues& filterValues);

	// create instances for all control volumes
	void createAllCVInstances();

	// generate all fields
	void createFields();

	// copy buffer from the mesh class
	void createBuffer();
	
	// copy variables from mesh and solver class
	void copyData();

};