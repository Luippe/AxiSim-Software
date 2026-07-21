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

	// One instant of a transient run, ready to display. Both halves are needed
	// because the two results views read different data: the 2D inspector colors
	// real mesh cells from solutions[].field, while the 3D scene samples the
	// raster texture built from Field::vertexValues.
	struct AnimationFrame {
		double time = 0.0;
		std::unordered_map<std::string, SolutionField> solutions;
		std::unordered_map<std::string, Field> fields;	// no GL texture of their own
	};

	// Value range of one field across the WHOLE run. Playback pins the colorbar to
	// this instead of each frame's own range, so colors mean the same thing in
	// every frame and the legend does not jitter as the animation plays.
	struct FieldRange {
		float vmin = 0.0f;
		float vmax = 0.0f;
	};

	std::vector<AnimationFrame> animationFrames;
	std::unordered_map<std::string, FieldRange> animationRanges;

	// index of the frame currently pushed into solutions/fields
	int currentAnimationFrame = 0;

	// true once a transient run produced something worth playing
	bool hasAnimation() const { return animationFrames.size() > 1; }

	// Whole-run range for a field, when one exists. Views use this during playback
	// instead of recomputing per frame. Returns false in Local color-range mode, so
	// callers fall back to the per-frame range they already computed.
	bool animationRangeFor(const std::string& name, float& vmin, float& vmax) const;

	ColorRangeMode currentColorRangeMode = ColorRangeMode::Global;

	// build the animation frames from the solver's captured time frames. rasterToCell
	// is the shared multiblock raster map (see rasterMap).
	void createAnimationFrames(const Mesh& mesh, const Solver& solver, const std::vector<int>& rasterToCell);

	// push one frame's data into solutions/fields so every results view shows that
	// instant. No-op on an out-of-range index.
	void showAnimationFrame(int index);

	CompareType currentCompareType = CompareType::None;
	FilterValues filterValues;

	int nseg = 0;
	std::vector<double> dz;
	std::vector<double> dr;
	int nr = 0, nz = 0;

	int currentItem = 0;

	std::vector<std::string> fieldType;

	// fields shown as tabs in the inspector (a subset of fieldType, in tab order).
	// edited from the Results panel's Graphics picker via drag and drop.
	std::vector<std::string> shownFields;

	ShadingType currentShadingType = ShadingType::Interp;
	const char* compareType[6] = { "None", "Less Than", "Equal To", "Greater Than", "Between", "Exclude"};
	const char* shadingType[2] = { "Interp", "Flat" };
	// order must match enum ColorRangeMode
	const char* colorRangeModeType[2] = { "Global (whole run)", "Local (per frame)" };

	void render();

	// upload colormap to shader
	void uploadUniforms();

	// Set by loadFromPathResults when a project carried saved results. The load runs
	// off the GL thread (loadAtLaunch happens before the window exists), and rebuilding
	// fields allocates textures, so the GUI consumes this flag on its next frame and
	// calls rebuildAfterLoad there instead.
	bool pendingRebuild = false;

	// Rebuild everything that generate() derives from solutions/animationFrames but
	// that is NOT serialized: the grid taken from the live mesh, the cylinder template,
	// the per-field CPU values and GL textures, and each animation frame's Field.
	// Shares copyGrid/rebuildRenderData with generate() so a loaded project reaches the
	// same state a freshly generated one is in.
	// solver.fvMesh is reconstructed from the mesh first when it is empty (a load
	// restores the solution values but never ran a solve).
	void rebuildAfterLoad(const Mesh& mesh, Solver& solver);

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

	// Drop stale names after fieldType changes and ensure every generated result
	// shows the available default flow/scalar fields while preserving extra tabs.
	void syncShownFields();

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

	// generate all fields. rasterToCell is the shared multiblock raster map (see
	// rasterMap); passing it in lets a caller that already built one avoid rebuilding it.
	void createFields(const Mesh& mesh, const Solver& solver, const std::vector<int>& rasterToCell);

	void generate(Mesh& mesh, Solver& solver);

private:

	// Take the grid and everything sized by it from the live mesh. Shared by copyData
	// and rebuildAfterLoad, which must agree: buildField always resamples against
	// mesh.g, so these have to describe the CURRENT raster, never a saved one.
	void copyGrid(const Mesh& mesh);

	// Rebuild the render-side data derived from solutions: the cylinder template and
	// every Field (values + GL texture). Shared tail of generate and rebuildAfterLoad.
	void rebuildRenderData(const Mesh& mesh, const Solver& solver, const std::vector<int>& rasterToCell);

	// Raster -> multiblock-cell map for this mesh, or empty on the non-multiblock path
	// where buildField never reads it. Built once per generate/rebuild and shared by
	// every buildField call, since it is O(nr*nz) plus a search per block sub-cell.
	std::vector<int> rasterMap(const Mesh& mesh) const;

	// Build one Field from a cell-centered solution. A multiblock mesh has no single
	// nr x nz raster -- its FVMesh cells are numbered per block and fvMesh.nr/nz are
	// 0 -- so its solution is first resampled onto the raster grid (mesh.g) that the
	// raster-based renderers index as i*nz+j. rasterToCell is only read on that path
	// and may be empty otherwise.
	Field buildField(
		const Mesh& mesh,
		const Solver& solver,
		const SolutionField& solution,
		const std::vector<int>& rasterToCell,
		bool createTexture
	) const;

	//Field uField;
	//Field vField;
	//Field pField;
	//Field concField;
	//Field tempField;

};
