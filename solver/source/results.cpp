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


Results::Results(Config& config) :

	config(config) {
}

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


void Results::updateAfterLoadingFile() {

	isReady = true;

}

void Results::reset() {

	// clearing the maps frees any GL texture buffers the fields own (RAII).
	fields.clear();
	solutions.clear();
	shownFields.clear();
	fieldType.clear();

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

void Results::updateTextureBuffer(const void* data) {
	currentField->textureBuffer.updateBuffer(g.nz + 1, g.nr + 1, GL_RED, GL_FLOAT, data);
}

void Results::copyData(const Mesh& mesh, const Solver& solver) {

	// copy variables and structs
	g = mesh.g;
	nseg = mesh.nseg;
	nr = g.nr;
	nz = g.nz;
	dr = g.dr;
	dz = g.dz;

	fieldType = solver.fieldType;
	solutions = solver.solutions;

}

void Results::generate(Mesh& mesh, Solver& solver) {

	Clock::time_point startTime = startTimer();

	verticesCV.clear();
	indicesCV.clear();

	// copy all relevant data from mesh class
	copyData(mesh, solver);

	// create instances and create buffer
	createCylinderTemplate(verticesCV, indicesCV, nseg);

	// generate all fields (values and buffers)
	createFields(mesh, solver);
	updateCurrentField();

	// prune stale shown-field names and seed a default so the inspector has a tab
	syncShownFields();

	console->addCompletionMessage("Completed generating field variables");

	isReady = true;

	float endTime = endTimer(startTime);
	console->addCompletionTime("Results", endTime);

}

void Results::createFields(const Mesh& mesh, const Solver& solver) {

	// A multiblock mesh has no single nr x nz raster: its FVMesh cells are numbered
	// per block, and fvMesh.nr/nz are 0. The results renderer (3D cylinders + the 2D
	// inspector) is raster-based and indexes fields as i*nz+j, so the per-cell
	// multiblock solution must first be resampled onto the raster grid (mesh.g).
	if (mesh.isMultiBlock) {
		createFieldsMultiBlock(mesh);
		return;
	}

	for (const std::string& name : fieldType) {

		// generate new field
		Field newField;
		newField.generate(solutions[name], solver.fvMesh, mesh.boundaryGroups);

		// insert new field
		fields[name] = newField;

	}
}

void Results::createFieldsMultiBlock(const Mesh& mesh) {

	const GridConfig& grid = mesh.g;
	const int nrRaster = grid.nr;
	const int nzRaster = grid.nz;
	const int nRasterCells = nrRaster * nzRaster;

	// One raster -> multiblock-cell map, shared by every field this generate.
	const std::vector<int> rasterToCell = mesh.buildMultiBlockRasterMap();

	for (const std::string& name : fieldType) {

		const SolutionField& sol = solutions[name];

		// Resample the multiblock solution onto the raster grid for the 3D cylinder
		// view only, which is raster-based (Field::generateRaster indexes i*nz+j).
		// Build it into a LOCAL copy and DO NOT write it back to solutions[] -- the 2D
		// inspector renders the real block cells and indexes solutions[].field in
		// block/cellGlobal order (the exact solver values). Cells with no covering
		// block (obstacles / outside the domain) read 0 in the raster.
		SolutionField rasterSol = sol;
		std::vector<double> raster(static_cast<size_t>(std::max(nRasterCells, 0)), 0.0);
		for (int n = 0; n < nRasterCells; n++) {
			const int c = rasterToCell[n];
			if (c >= 0 && c < (int)sol.field.size()) {
				raster[n] = sol.field[c];
			}
		}

		rasterSol.field = std::move(raster);
		rasterSol.dr = grid.dr;
		rasterSol.dz = grid.dz;

		Field newField;
		newField.generateRaster(rasterSol, nrRaster, nzRaster);
		fields[name] = std::move(newField);
	}
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

	// seed a default set the first time results are generated: the core flow fields
	// plus whichever scalar solvers are active. Temperature/Concentration are only
	// present in fieldType when their solver is enabled, so membership alone gates them.
	if (shownFields.empty() && !fieldType.empty()) {
		const char* defaults[] = {
			"Axial Velocity", "Radial Velocity", "Continuity",
			"Temperature", "Concentration"
		};

		for (const char* name : defaults) {
			if (indexOfField(name) >= 0) {
				shownFields.push_back(name);
			}
		}

		// nothing matched (unexpected) — fall back to the current field so the
		// inspector still has a tab
		if (shownFields.empty()) {
			int seed = std::clamp(currentItem, 0, (int)fieldType.size() - 1);
			shownFields.push_back(fieldType[seed]);
		}
	}
}
