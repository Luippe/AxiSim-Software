#include "results.h"

#include <algorithm>
#include <cmath>

#include <glm/vec2.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "solver.h"
#include "mesh.h"

#include "console.h"

#include "printer.h"
#include "time_manager.h"


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


void Results::rebuildAfterLoad(const Mesh& mesh, Solver& solver) {

	pendingRebuild = false;

	if (fieldType.empty()) {
		isReady = false;
		return;
	}

	// A load restores the solved values but never ran a solve, so the solver holds no
	// fvMesh. buildField needs one on the non-multiblock path (Field::generate reads
	// its cells/faces); rebuild it from the loaded mesh, which is deterministic.
	if (solver.fvMesh.numCells() == 0) {
		solver.fvMesh = mesh.isMultiBlock
			? mesh.createMultiBlockFVMesh()
			: mesh.createFVMesh(mesh.g.activeCell);
	}

	// The grid is NOT restored from the file -- it is re-taken from the live mesh,
	// because buildField resamples against mesh.g. If the mesh was edited between
	// saving and loading, the rebuilt Fields are sized to the CURRENT raster; a saved
	// grid would leave scene_view looping nr x nz over a differently-sized cellValues.
	copyGrid(mesh);

	const std::vector<int> rasterToCell = rasterMap(mesh);

	// Fields carry GL textures and are rebuilt, not saved. animationRanges IS saved,
	// so it is left alone here -- recomputing it from the frames would give the same
	// answer, but only for the fields this session happens to rebuild.
	fields.clear();
	rebuildRenderData(mesh, solver, rasterToCell);

	for (AnimationFrame& frame : animationFrames) {
		frame.fields.clear();
		for (const auto& [name, sol] : frame.solutions) {
			frame.fields[name] = buildField(mesh, solver, sol, rasterToCell, false);
		}
	}

	// shownFields came out of the same file as fieldType, so it cannot hold stale
	// names and needs no pruning. Only seed defaults when the save carried none --
	// syncShownFields re-adds every default tab, which would undo tabs the user
	// deliberately removed before saving.
	if (shownFields.empty()) {
		syncShownFields();
	}

	isReady = true;

	// Saved frame index is only meaningful against the frames we actually loaded.
	if (currentAnimationFrame < 0 || currentAnimationFrame >= (int)animationFrames.size()) {
		currentAnimationFrame = 0;
	}

	if (!animationFrames.empty()) {
		showAnimationFrame(currentAnimationFrame);
	}
}

void Results::reset() {

	// clearing the maps frees any GL texture buffers the fields own (RAII).
	fields.clear();
	solutions.clear();
	shownFields.clear();
	fieldType.clear();

	animationFrames.clear();
	animationRanges.clear();
	currentAnimationFrame = 0;
	pendingRebuild = false;

	currentField = nullptr;
	currentItem = 0;

	isReady = false;
	show = true;
	showOutline = false;
	isMultipleInstancing = false;

	currentCompareType = CompareType::None;
	currentShadingType = ShadingType::Interp;
	filterValues = FilterValues{};
}

void Results::setTextureShadingAllField(GLint shadingMode) {

	//results.currentField->textureBuffer.setTextureShading(shadingMode);
}

void Results::copyGrid(const Mesh& mesh) {

	g = mesh.g;
	nseg = mesh.nseg;
	nr = g.nr;
	nz = g.nz;
	dr = g.dr;
	dz = g.dz;
}

void Results::rebuildRenderData(const Mesh& mesh, const Solver& solver, const std::vector<int>& rasterToCell) {

	// create instances and create buffer
	verticesCV.clear();
	indicesCV.clear();
	createCylinderTemplate(verticesCV, indicesCV, nseg);

	// generate all fields (values and buffers)
	createFields(mesh, solver, rasterToCell);
	updateCurrentField();
}

void Results::copyData(const Mesh& mesh, const Solver& solver) {

	// copy variables and structs
	copyGrid(mesh);

	fieldType = solver.fieldType;
	solutions = solver.solutions;

}

void Results::generate(Mesh& mesh, Solver& solver) {

	Clock::time_point startTime = startTimer();

	// copy all relevant data from mesh class
	copyData(mesh, solver);

	// One raster -> multiblock-cell map, shared by every buildField below.
	const std::vector<int> rasterToCell = rasterMap(mesh);

	rebuildRenderData(mesh, solver, rasterToCell);

	// transient snapshots, if the run captured any. Built after createFields so the
	// frames can reuse each solution's geometry/metadata.
	createAnimationFrames(mesh, solver, rasterToCell);

	// prune stale shown-field names and seed a default so the inspector has a tab
	syncShownFields();

	console->addCompletionMessage("Completed generating field variables");

	isReady = true;

	float endTime = endTimer(startTime);
	console->addCompletionTime("Results", endTime);

}

Field Results::buildField(
	const Mesh& mesh,
	const Solver& solver,
	const SolutionField& solution,
	const std::vector<int>& rasterToCell,
	bool createTexture
) const {

	Field newField;

	if (!mesh.isMultiBlock) {
		newField.generate(solution, solver.fvMesh, mesh.boundaryGroups, createTexture);
		return newField;
	}

	const GridConfig& grid = mesh.g;
	const int nrRaster = grid.nr;
	const int nzRaster = grid.nz;
	const int nRasterCells = std::max(nrRaster * nzRaster, 0);

	// Resample the multiblock solution onto the raster grid for the 3D cylinder
	// view only, which is raster-based (Field::generateRaster indexes i*nz+j).
	// Build it into a LOCAL copy and DO NOT write it back to solutions[] -- the 2D
	// inspector renders the real block cells and indexes solutions[].field in
	// block/cellGlobal order (the exact solver values). Cells with no covering
	// block (obstacles / outside the domain) read 0 in the raster.
	// Only boundaryVariable carries over -- field/dr/dz are all replaced below, so
	// copying the source solution here would allocate and discard the whole field.
	SolutionField rasterSol;
	rasterSol.boundaryVariable = solution.boundaryVariable;

	std::vector<double> raster(static_cast<size_t>(nRasterCells), 0.0);

	for (int n = 0; n < nRasterCells && n < (int)rasterToCell.size(); n++) {
		const int c = rasterToCell[n];
		if (c >= 0 && c < (int)solution.field.size()) {
			raster[n] = solution.field[c];
		}
	}

	rasterSol.field = std::move(raster);
	rasterSol.dr = grid.dr;
	rasterSol.dz = grid.dz;

	newField.generateRaster(rasterSol, nrRaster, nzRaster, createTexture);
	return newField;
}

std::vector<int> Results::rasterMap(const Mesh& mesh) const {

	return mesh.isMultiBlock ? mesh.buildMultiBlockRasterMap() : std::vector<int>{};
}

void Results::createFields(const Mesh& mesh, const Solver& solver, const std::vector<int>& rasterToCell) {

	for (const std::string& name : fieldType) {
		fields[name] = buildField(mesh, solver, solutions[name], rasterToCell, true);
	}
}

bool Results::animationRangeFor(const std::string& name, float& vmin, float& vmax) const {

	// Local mode: decline the whole-run range and leave vmin/vmax untouched. Every
	// caller seeds them with the current frame's own range before asking, so
	// refusing here is all it takes to switch the colormap to per-frame scaling.
	if (currentColorRangeMode == ColorRangeMode::Local) {
		return false;
	}

	auto it = animationRanges.find(name);
	if (it == animationRanges.end()) {
		return false;
	}

	vmin = it->second.vmin;
	vmax = it->second.vmax;
	return true;
}

void Results::createAnimationFrames(const Mesh& mesh, const Solver& solver, const std::vector<int>& rasterToCell) {

	animationFrames.clear();
	animationRanges.clear();
	currentAnimationFrame = 0;

	if (solver.timeFrames.empty()) {
		return;
	}

	animationFrames.reserve(solver.timeFrames.size());

	for (const Solver::TimeFrame& timeFrame : solver.timeFrames) {

		AnimationFrame frame;
		frame.time = timeFrame.time;

		for (const auto& [name, values] : timeFrame.fields) {

			// The solver only captures primary fields; anything else (gradients,
			// continuity) has no per-step data and stays at its final-state value.
			auto solIt = solutions.find(name);
			if (solIt == solutions.end()) {
				continue;
			}

			// Reuse the final solution's geometry/metadata and swap in this
			// instant's values, so the frame builds through the identical path.
			SolutionField sol = solIt->second;
			sol.field = values;

			Field field = buildField(mesh, solver, sol, rasterToCell, false);

			// Seed on the first frame that carries this field, then widen. Starting
			// from a default-constructed 0/0 would drag every range towards zero.
			auto rangeIt = animationRanges.find(name);
			if (rangeIt == animationRanges.end()) {
				animationRanges[name] = FieldRange{ field.vmin, field.vmax };
			}
			else {
				rangeIt->second.vmin = std::min(rangeIt->second.vmin, field.vmin);
				rangeIt->second.vmax = std::max(rangeIt->second.vmax, field.vmax);
			}

			frame.solutions[name] = std::move(sol);
			frame.fields[name] = std::move(field);
		}

		animationFrames.push_back(std::move(frame));
	}
}

void Results::showAnimationFrame(int index) {

	if (index < 0 || index >= (int)animationFrames.size()) {
		return;
	}

	const AnimationFrame& frame = animationFrames[index];

	// The 2D inspector reads solutions[].field live every draw, so swapping the
	// values here is all it needs.
	for (const auto& [name, sol] : frame.solutions) {
		auto it = solutions.find(name);
		if (it != solutions.end()) {
			it->second.field = sol.field;
		}
	}

	// The 3D scene samples each field's GL texture and its cellValues, both of
	// which live on the persistent Field -- frame fields carry no texture, so the
	// values are copied in and re-uploaded rather than the pointer being swapped.
	for (const auto& [name, frameField] : frame.fields) {

		auto it = fields.find(name);
		if (it == fields.end()) {
			continue;
		}

		Field& live = it->second;

		live.vertexValues = frameField.vertexValues;
		live.cellValues = frameField.cellValues;

		float vmin = frameField.vmin;
		float vmax = frameField.vmax;
		animationRangeFor(name, vmin, vmax);
		live.setMinMax(vmin, vmax);

		// nr/nz come from the Field itself, matching what createBuffer allocated.
		live.textureBuffer.updateBuffer(
			live.nz + 1,
			live.nr + 1,
			GL_RED,
			GL_FLOAT,
			live.vertexValues.data()
		);
	}

	currentAnimationFrame = index;
}

void Results::updateCurrentField() {

	if (fieldType.empty()) return;

	std::string name = fieldType[currentItem];
	currentField = &fields[name];

}

int Results::indexOfField(const std::string& name) const {

	for (int i = 0; i < (int)fieldType.size(); i++) {
		if (fieldType[i] == name) {
			return i;
		}
	}
	return -1;
}

bool Results::isShown(const std::string& name) const {
	return std::find(shownFields.begin(), shownFields.end(), name) != shownFields.end();
}

void Results::addShownField(const std::string& name) {

	if (indexOfField(name) < 0) {
		return;					// not a real field
	}

	if (isShown(name)) {
		return;					// already shown
	}

	shownFields.push_back(name);

	// activate the freshly added field so the inspector jumps to its new tab
	currentItem = indexOfField(name);
	updateCurrentField();
}

void Results::removeShownField(const std::string& name) {

	auto it = std::find(shownFields.begin(), shownFields.end(), name);
	if (it == shownFields.end()) {
		return;
	}
	shownFields.erase(it);

	// if the active field is no longer shown, fall back to the first shown field
	bool activeStillShown =
		currentItem >= 0 &&
		currentItem < (int)fieldType.size() &&
		isShown(fieldType[currentItem]);

	if (!activeStillShown && !shownFields.empty()) {
		currentItem = indexOfField(shownFields.front());
		updateCurrentField();
	}
}

void Results::syncShownFields() {

	// drop any shown names that are no longer part of fieldType (e.g. after
	// switching between temperature and concentration modes)
	shownFields.erase(
		std::remove_if(
			shownFields.begin(),
			shownFields.end(),
			[&](const std::string& name) { return indexOfField(name) < 0; }
		),
		shownFields.end()
	);

	// Ensure every generated result shows the core flow fields by default, while
	// preserving any extra tabs the user added. Temperature and Concentration only
	// exist in fieldType when their corresponding solver is enabled, so they are
	// added automatically as soon as results are regenerated with that solver.
	const char* defaults[] = {
		"Axial Velocity", "Radial Velocity", "Velocity Magnitude",
		"Continuity", "Pressure", "Cell Reynolds Number",
		"Temperature", "Concentration"
	};

	for (const char* name : defaults) {
		if (indexOfField(name) >= 0 && !isShown(name)) {
			shownFields.push_back(name);
		}
	}

	// Nothing matched (unexpected) — fall back to the current field so the
	// inspector still has a tab.
	if (shownFields.empty() && !fieldType.empty()) {
		int seed = std::clamp(currentItem, 0, (int)fieldType.size() - 1);
		shownFields.push_back(fieldType[seed]);
	}
}
