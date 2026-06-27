#pragma once
#include <vector>
#include <unordered_map>
#include <string>
#include <glm/mat4x4.hpp>

#include "buffer_manager.h"
#include "graphics_struct.h"
#include "field_manager.h"

class Colormap;
class Shader;
class Console;
class Mesh;
class Solver;

class Results {
public:

	struct SceneScale {
		uint8_t index = 0;
		float value = 1.0f;
	};

	SceneScale sceneScale;

	CompareType currentCompareType = CompareType::GreaterThan;
	FilterValues filterValues;

	int nseg;
	std::vector<double> dz;
	std::vector<double> dr;
	int nr, nz;

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

	ShadingType currentShadingType = ShadingType::Interp;
	const char* compareType[5] = { "Less Than", "Equal To", "Greater Than", "Between", "Exclude"};
	const char* shadingType[2] = { "Interp", "Flat" };

	Results(Config& config);

	void render();

	// upload colormap to shader
	void uploadUniforms();

	// update the results after loading a file
	void updateAfterLoadingFile();

	// update all relevant variables
	void updateCurrentField();

	// update current buffer with new data, as texture buffers cannot be copied over, they must be updated instead
	void updateTextureBuffer(const void* data);

	// set shading mode for all fields
	void setTextureShadingAllField(GLint shadingMode);

	bool show = true;
	bool showOutline = false;
	bool isReady = false;
	bool isMultipleInstancing = false;

	Field* currentField = nullptr;
	Console* console = nullptr;

	GridConfig g;

	// results variables
	std::unordered_map<std::string, Field> fields;
	std::unordered_map<std::string, SolutionField> solutions;
	std::vector<CylinderTemplateVertex> verticesCV;
	std::vector<unsigned int> indicesCV;

	// copy variables from mesh and solver class
	void copyData(const Mesh& mesh, const Solver& solver);

	// generate all fields
	void createFields(const Mesh& mesh, const Solver& solver);

	void generate(Mesh& mesh, Solver& solver);

private:

	//Field uField;
	//Field vField;
	//Field pField;
	//Field concField;
	//Field tempField;
	Config& config;

};