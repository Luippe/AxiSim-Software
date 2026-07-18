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



	CompareType currentCompareType = CompareType::None;
	FilterValues filterValues;

	int nseg;
	std::vector<double> dz;
	std::vector<double> dr;
	int nr, nz;

	int currentItem = 0;

	std::vector<std::string> fieldType;

	// fields shown as tabs in the inspector (a subset of fieldType, in tab order).
	// edited from the Results panel's Graphics picker via drag and drop.
	std::vector<std::string> shownFields;

	ShadingType currentShadingType = ShadingType::Interp;
	const char* compareType[6] = { "None", "Less Than", "Equal To", "Greater Than", "Between", "Exclude"};
	const char* shadingType[2] = { "Interp", "Flat" };

	Results(Config& config);

	void render();

	// upload colormap to shader
	void uploadUniforms();

	// update the results after loading a file
	void updateAfterLoadingFile();

	// drop all displayed fields/solutions so a new project starts with an empty
	// Results panel and inspector instead of the previous project's data.
	void reset();

	// update all relevant variables
	void updateCurrentField();

	// index of a field name within fieldType, or -1 if it is not a known field
	int indexOfField(const std::string& name) const;

	// true when the field is currently in the inspector's shown set
	bool isShown(const std::string& name) const;

	// add/remove a field from the inspector's shown set. no-op on unknown names or
	// duplicates. adding activates the field so its new inspector tab becomes current;
	// removing the active field falls back to the first remaining shown field.
	void addShownField(const std::string& name);
	void removeShownField(const std::string& name);

	// drop stale names from shownFields after fieldType changes, and seed a default
	// so the inspector isn't blank the first time results are generated.
	void syncShownFields();

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

	// resample a multiblock solution onto the raster grid (mesh.g) and build the
	// raster-based fields the results/inspector renderers expect
	void createFieldsMultiBlock(const Mesh& mesh);

	void generate(Mesh& mesh, Solver& solver);

private:

	//Field uField;
	//Field vField;
	//Field pField;
	//Field concField;
	//Field tempField;
	Config& config;

};
