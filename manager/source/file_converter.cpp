#include "file_converter.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <set>
#include <string_view>

#include "boundary_func.h"
#include "multiblock.h"

namespace {

// The 8 hex-local vertex labels of a block, with the axis collapse applied.
//
// A corner at r = 0 has coincident back and front points, so the front label has
// to alias the back one -- 8 distinct labels would leave the cell a zero-
// thickness edge instead of collapsing it to a prism. Only the I=0 corners can
// reach the axis (I runs +r), and they collapse INDEPENDENTLY: a curvilinear
// block may meet the axis at one end of its south edge and not the other.
//
// Everything that names a vertex of this block goes through here. A patch face
// written with the raw 0..7 numbering would reference a vertex the collapsed
// cell no longer owns, and blockMesh rejects the whole dict for it.
std::array<int, 8> hexLabels(const Block& block) {

	const MBNode onAxisCandidates[2] = { block.node(0, 0), block.node(0, block.nz) };

	// Tolerance relative to the block's own radial extent, so a corner that landed
	// on the axis at 1e-17 still reads as on-axis in a domain measured in microns.
	// makeRectBlock, the only builder at present, writes an exact 0.0 there, so the
	// tolerance is slack today -- it is what keeps this right for any builder that
	// interpolates node positions rather than laying them on a tensor product.
	const double extent = std::fabs(block.node(block.nr, 0).r);
	const double tol = 1e-12 * (extent > 0.0 ? extent : 1.0);

	std::array<int, 8> labels = { 0,1,2,3,4,5,6,7 };
	if (std::fabs(onAxisCandidates[0].r) <= tol) labels[4] = 0;
	if (std::fabs(onAxisCandidates[1].r) <= tol) labels[5] = 1;
	return labels;
}

// Face windings, hex-local, normals pointing OUT of the block -- what a
// blockMeshDict patch entry wants.
//
// verticesFromBlock lays the 8 vertices out as OpenFOAM's canonical hex: 0->1 is
// +z (dict x, axial), 1->2 is +r (dict y, radial), 0->4 is +wedge (dict z). So
// these are just that hex's canonical outward windings, with the four r-z Edges
// landing on the x/y faces and the two wedge planes on z-min/z-max.
constexpr std::array<int, 4> kBackFace  = { 0, 3, 2, 1 };   // -angle/2 plane
constexpr std::array<int, 4> kFrontFace = { 4, 5, 6, 7 };   // +angle/2 plane

std::array<int, 4> edgeFaceLabels(Edge e) {
	switch (e) {
		case Edge::East:  return { 1, 2, 6, 5 };   // J=nz, max axial
		case Edge::South: return { 0, 1, 5, 4 };   // I=0,  min radial
		case Edge::North: return { 3, 7, 6, 2 };   // I=nr, max radial
		case Edge::West:  break;                   // J=0,  min axial
	}
	return { 0, 4, 7, 3 };
}

// Resolve a hex-local face to global labels for block `base`. Returns false when
// the collapse left fewer than 3 distinct corners -- that is the face lying ON
// the axis, which has zero area and must never be emitted. A face that merely
// touches the axis keeps 3 and goes out as a triangle.
bool globalFace(const std::array<int, 4>& local, const std::array<int, 8>& labels,
                int base, std::array<int, 4>& out) {

	int distinct = 0;
	for (int k = 0; k < 4; k++) {
		const int v = labels[local[k]];
		out[k] = base + v;

		bool seen = false;
		for (int m = 0; m < k; m++)
			if (labels[local[m]] == v) { seen = true; break; }
		if (!seen) distinct++;
	}
	return distinct >= 3;
}

BoundaryFOAMType foamType(BoundaryType type) {
	switch (type) {
		case BoundaryType::WALL:     return BoundaryFOAMType::WALL;
		case BoundaryType::SYMMETRY: return BoundaryFOAMType::SYMMETRY_PLANE;
		default: break;
	}
	// VELOCITY_INLET / PRESSURE_OUTLET / FAR_FIELD all carry their condition in
	// the 0/ field files, not in the mesh -- a plain `patch` is what they get.
	return BoundaryFOAMType::PATCH;
}

// OpenFOAM patch names are `word`s: alphanumerics and '_', nothing else, and not
// leading with a digit. The GUI takes anything, e.g. "Outer wall (r=R)", so
// squeeze whatever it holds into one.
std::string foamPatchName(const BoundarySegmentGroup& group) {

	std::string name;
	for (unsigned char c : group.name) {
		if (std::isalnum(c) || c == '_') name.push_back((char)c);
		else if (!name.empty() && name.back() != '_') name.push_back('_');
	}
	while (!name.empty() && name.back() == '_') name.pop_back();

	if (name.empty() || std::isdigit((unsigned char)name.front()))
		name = "group" + std::to_string(group.id) + (name.empty() ? "" : "_" + name);
	return name;
}

// The SHORTEST spelling that still reads back as the same double.
//
// Exactness is not negotiable -- a value that round-trips to 6 digits is a
// different value, which is the whole point of writeBlockMeshDict's setprecision.
// But a flat %.17g turns a dt the user typed as 0.05 into 0.050000000000000003,
// and these dictionaries are meant to be read and edited by hand. 15 digits covers
// essentially everything anyone types; the ladder up to 17 is what guarantees the
// round trip for the values it does not.
std::string foamNumber(double value) {

	char buffer[40];

	for (int digits = 15; digits < 17; digits++) {
		std::snprintf(buffer, sizeof(buffer), "%.*g", digits, value);
		if (std::strtod(buffer, nullptr) == value) return buffer;
	}

	std::snprintf(buffer, sizeof(buffer), "%.17g", value);
	return buffer;
}

// Every Foam file opens with a FoamFile sub-dict. It is not decoration: IOobject
// reads it to decide the file's class, and a file without one is rejected before
// blockMesh ever reaches the vertices. `location` is omitted when null -- the dict
// does not need it, a 0/ field does.
void writeFoamHeader(std::ofstream& out, std::string_view className, std::string_view object,
                     const char* location = nullptr) {

	out << "/*--------------------------------*- C++ -*----------------------------------*\\\n"
	       "  Written by AxiSim. Axisymmetric wedge mesh, one cell thick in the\n"
	       "  circumferential direction -- both wedge planes are patches of type wedge.\n"
	       "\\*---------------------------------------------------------------------------*/\n"
	       "FoamFile\n"
	       "{\n"
	       "    version     2.0;\n"
	       "    format      ascii;\n"
	    << "    class       " << className << ";\n";

	if (location) out << "    location    \"" << location << "\";\n";

	out << "    object      " << object << ";\n"
	       "}\n"
	       "// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //\n\n";
}

}

const char* foamPatchTypeName(BoundaryFOAMType type) {
	switch (type) {
		case BoundaryFOAMType::WALL:           return "wall";
		case BoundaryFOAMType::WEDGE:          return "wedge";
		case BoundaryFOAMType::SYMMETRY_PLANE: return "symmetryPlane";
		case BoundaryFOAMType::PATCH:          break;
	}
	return "patch";
}

// blockMesh's expansion ratio is the LAST cell width over the FIRST; coef is the
// per-cell growth factor. generateFaces builds d_k = d0 * coef^k, so n cells span
// coef^(n-1) -- handing it coef raw understates grading by that whole exponent.
//
// The guards mirror generateFaces exactly. Everywhere it silently falls back to
// uniform, this has to too, or the dict claims a grading the mesh never had.
double zoneExpansion(const MeshZone& zone) {
	if (zone.grading != Grading::Progression) return 1.0;
	if (zone.cells < 2 || zone.coef <= 0.0 || zone.coef == 1.0) return 1.0;
	return std::pow(zone.coef, zone.cells - 1);
}

std::vector<Vec3> verticesFromBlock(const Block& block, double wedgeAngleDeg) {

	// The four (z, r) corners, ordered min -> +z -> +z+r -> +r. makeRectBlock lays
	// nodes out with I running +r and J running +z, so node(0,0) is the min corner
	// and this traversal is counter-clockwise in the r-z plane -- which is what
	// makes the hex's first face wind correctly. A builder that numbered its nodes
	// the other way round would come out inside-out here, and blockMesh rejects it
	// for negative volume.
	const MBNode corners[4] = {
		block.node(0, 0),
		block.node(0, block.nz),
		block.node(block.nr, block.nz),
		block.node(block.nr, 0)
	};

	// A single-layer wedge is symmetric about the r-z plane, so each side carries
	// half of the quoted included angle.
	const double halfAngle = 0.5 * wedgeAngleDeg * (MB_PI / 180.0);
	const double cosHalf = std::cos(halfAngle);
	const double sinHalf = std::sin(halfAngle);

	std::vector<Vec3> vertices;
	vertices.reserve(24);

	// Back plane first, then front, so the 8 labels land in hex order.
	for (int side = 0; side < 2; side++) {
		const double sign = (side == 0) ? -1.0 : 1.0;
		for (const MBNode& corner : corners) {
			vertices.push_back({
				corner.z,
				corner.r * cosHalf,
				sign * corner.r * sinHalf
			});
		}
	}

	return vertices;
}

Hex hexFromBlock(const Block& block) {

	Hex hex;

	hex.indices = hexLabels(block);
	hex.size = { block.nz, block.nr, 1 };

	hex.grading = {
		zoneExpansion(block.axialZone),
		zoneExpansion(block.radialZone),
		1.0
	};

	return hex;
}

std::unordered_map<std::string, BoundaryFOAM> boundaryFromMultiblock(
	const MultiBlockMesh& mesh,
	const std::vector<BoundarySegmentGroup>& groups
) {

	std::unordered_map<std::string, BoundaryFOAM> boundary;

	// Both wedge planes exist on every block, so seed them first. That also lets
	// them win the name-collision loop below against a user group whose name
	// sanitizes to "front" or "back". References into an unordered_map survive
	// rehashing, so holding these across the inserts underneath is safe.
	BoundaryFOAM& back  = boundary["back"];
	BoundaryFOAM& front = boundary["front"];
	back.type  = BoundaryFOAMType::WEDGE;
	front.type = BoundaryFOAMType::WEDGE;

	// Interface edges become interior faces once blockMesh fuses the coincident
	// vertices (`mergeType points;` in the dict), so they must NOT be emitted as
	// patch faces -- doing so would wall off the seam and disconnect the blocks.
	std::set<long long> claimed;
	for (const Interface& itf : mesh.interfaces) {
		claimed.insert(mbEdgeKey(itf.blockA, itf.edgeA));
		claimed.insert(mbEdgeKey(itf.blockB, itf.edgeB));
	}

	// Resolved patch name per boundary group id, so the sanitize + collision work
	// runs once per group rather than once per face.
	std::unordered_map<int, std::string> nameOfGroup;

	auto patchFor = [&](int groupID) -> BoundaryFOAM& {

		const auto known = nameOfGroup.find(groupID);
		if (known != nameOfGroup.end()) return boundary[known->second];

		const BoundarySegmentGroup* group =
			BoundaryGet::getBoundaryGroupByID(groups, groupID);

		// An untagged external edge is an upstream bug (validateBoundaryTags
		// throws on it), but silently dropping its faces would hand blockMesh a
		// mesh with a hole and no clue where. Give them a patch so it surfaces.
		const std::string stem = group ? foamPatchName(*group) : "unassigned";

		// Two groups can sanitize to the same word, and the map is keyed by name,
		// so a collision would quietly merge two physically distinct patches.
		std::string name = stem;
		for (int n = 2; boundary.count(name); n++)
			name = stem + "_" + std::to_string(n);

		nameOfGroup[groupID] = name;
		BoundaryFOAM& patch = boundary[name];
		patch.type = group ? foamType(group->type) : BoundaryFOAMType::PATCH;
		patch.groupID = group ? group->id : -1;
		return patch;
	};

	const Edge allEdges[4] = { Edge::West, Edge::East, Edge::South, Edge::North };

	for (int bi = 0; bi < (int)mesh.blocks.size(); bi++) {

		const Block& b = mesh.blocks[bi];
		const std::array<int, 8> labels = hexLabels(b);
		const int base = bi * 8;

		std::array<int, 4> face;
		if (globalFace(kBackFace,  labels, base, face)) back.faces.push_back(face);
		if (globalFace(kFrontFace, labels, base, face)) front.faces.push_back(face);

		for (Edge e : allEdges) {
			if (claimed.count(mbEdgeKey(bi, e))) continue;              // interior
			if (!globalFace(edgeFaceLabels(e), labels, base, face))     // on the axis:
				continue;                                              // zero area
			patchFor(b.edgeGroup[(int)e]).faces.push_back(face);
		}
	}

	return boundary;
}

BlockMeshDict blockMeshDictFromMultiblock(const MultiBlockMesh& mesh, const std::vector<BoundarySegmentGroup>& groups) {

	BlockMeshDict blockMeshDict;

	for (int i = 0; i < (int)mesh.blocks.size(); i++) {
		blockMeshDict.vertices.push_back(verticesFromBlock(mesh.blocks[i]));
		blockMeshDict.hexes.push_back(hexFromBlock(mesh.blocks[i]));
	}

	blockMeshDict.boundary = boundaryFromMultiblock(mesh, groups);

	return blockMeshDict;
}

bool writeBlockMeshDict(const std::filesystem::path& path, const BlockMeshDict& dict) {

	if (dict.hexes.size() != dict.vertices.size()) {
		std::cerr << "writeBlockMeshDict: " << dict.hexes.size() << " hexes for "
			<< dict.vertices.size() << " vertex blocks\n";
		return false;
	}

	// globalFace resolved every patch face against base = b*8, so a block that does
	// not contribute exactly 8 vertices slides the two numbering spaces apart and the
	// patches silently name corners of the wrong block. Catch it here rather than let
	// blockMesh report a negative-volume cell somewhere unrelated.
	for (std::size_t i = 0; i < dict.vertices.size(); i++) {
		if (dict.vertices[i].size() != 8) {
			std::cerr << "writeBlockMeshDict: block " << i << " has "
				<< dict.vertices[i].size() << " vertices, expected 8\n";
			return false;
		}
	}

	// Binary so the stream leaves '\n' alone. The dict is written on Windows and
	// routinely read by blockMesh under WSL; keeping it LF-only means both sides see
	// the same bytes.
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out) {
		std::cerr << "writeBlockMeshDict: cannot open " << path.string() << '\n';
		return false;
	}

	// blockMesh fuses coincident seam points only within magSqr(10*SMALL*bb.span()),
	// which is ~1e-14 relative to the block. ostream's default 6 significant digits
	// leaves two corners that should be the same point about 1e-7 apart, the merge
	// then does nothing, and the blocks come out disconnected with no complaint from
	// blockMesh at all. 17 digits round-trips a double exactly.
	out << std::setprecision(17);

	writeFoamHeader(out, "dictionary", "blockMeshDict");

	out << "scale   " << dict.scale << ";\n\n";

	// Flattened in block order, because that order IS the global labelling every hex
	// and every patch face below is written against.
	out << "vertices\n(\n";
	for (const std::vector<Vec3>& block : dict.vertices) {
		for (const Vec3& v : block) {
			out << "    (" << v.x << " " << v.y << " " << v.z << ")\n";
		}
	}
	out << ");\n\n";

	out << "blocks\n(\n";
	for (std::size_t i = 0; i < dict.hexes.size(); i++) {

		const Hex& hex = dict.hexes[i];
		const int base = (int)i * 8;

		out << "    hex (";
		for (int k = 0; k < 8; k++) {
			out << (k ? " " : "") << base + hex.indices[k];
		}

		out << ") (" << hex.size[0] << " " << hex.size[1] << " " << hex.size[2] << ")"
			<< " simpleGrading ("
			<< hex.grading[0] << " " << hex.grading[1] << " " << hex.grading[2] << ")\n";
	}
	out << ");\n\n";

	// No curved edges: every block edge is a straight line in the r-z plane, and the
	// wedge arc is one cell wide. Written empty rather than omitted so the dict reads
	// as deliberate.
	out << "edges\n(\n);\n\n";

	// dict.boundary is an unordered_map, so iterating it directly would order the
	// patches differently from run to run and make two exports of the same mesh
	// impossible to diff. Sort by name.
	std::vector<const std::pair<const std::string, BoundaryFOAM>*> patches;
	patches.reserve(dict.boundary.size());
	for (const auto& entry : dict.boundary) {
		patches.push_back(&entry);
	}
	std::sort(patches.begin(), patches.end(), [](const auto* a, const auto* b) {
		return a->first < b->first;
	});

	out << "boundary\n(\n";
	for (const auto* patch : patches) {

		out << "    " << patch->first << "\n"
			   "    {\n"
			<< "        type    " << foamPatchTypeName(patch->second.type) << ";\n"
			<< "        faces\n"
			   "        (\n";

		for (const std::array<int, 4>& face : patch->second.faces) {
			out << "            ("
				<< face[0] << " " << face[1] << " " << face[2] << " " << face[3] << ")\n";
		}

		out << "        );\n"
			   "    }\n";
	}
	out << ");\n\n";

	// Not a preference. verticesFromBlock gives every block its own 8 labels with no
	// cross-block dedup, and the default topological merge connects blocks only where
	// they SHARE a label -- it would find nothing and hand back one disconnected mesh
	// per block. The geometric merge is what makes the seams interior.
	out << "mergeType points;\n\n";

	out << "// ************************************************************************* //\n";

	// Everything above is buffered; a full disk or a revoked handle only surfaces on
	// flush, so the stream state is worth one last look before claiming success.
	out.flush();
	if (!out) {
		std::cerr << "writeBlockMeshDict: write failed for " << path.string() << '\n';
		return false;
	}

	return true;
}

namespace {

// The scalarTransport function object that carries one AxiSim scalar. Stock
// simpleFoam/pimpleFoam solve momentum and pressure only, so a transported scalar
// has to ride along as a function object -- this is the whole reason one run can
// produce U, T and C together instead of needing a custom solver.
void writeScalarTransport(std::ofstream& out, const char* field, double diffusivity) {

	out << "    " << field << "\n"
	       "    {\n"
	       "        type            scalarTransport;\n"
	       "        libs            (solverFunctionObjects);\n\n"
	    << "        field           " << field << ";\n"
	    << "        D               " << foamNumber(diffusivity) << ";\n\n";

	// bounded01 defaults to TRUE, which clamps the field into [0,1] for multiphase
	// work. Every AxiSim scalar is a physical value in its own units -- a
	// concentration in mol/m^3, a temperature in K -- so leaving the default would
	// silently flatten the solution instead of failing.
	out << "        // Defaults to true, which would clamp the field to [0,1].\n"
	       "        bounded01       false;\n\n"
	       "        resetOnStartUp  false;\n"
	       "        writeControl    writeTime;\n"
	       "    }\n";
}

}

bool writeControlDict(const std::filesystem::path& path, const FoamCaseSetup& setup) {

	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out) {
		std::cerr << "writeControlDict: cannot open " << path.string() << '\n';
		return false;
	}

	writeFoamHeader(out, "dictionary", "controlDict", "system");

	// Needed by blockMesh, not only by a solver: blockMesh constructs a Time object
	// before it opens the mesh dict, and Time reads this as MUST_READ. Without it the
	// case fails on a missing file that says nothing about the mesh.
	out << "// Written from the project's solver settings. pimpleFoam/simpleFoam follows\n"
	       "// the Transient checkbox; endTime, deltaT and the relaxation factors all come\n"
	       "// from the Solver tab.\n\n";

	out << "application     " << (setup.transient ? "pimpleFoam" : "simpleFoam") << ";\n\n";

	// startTime, not latestTime: 0/ is the only time directory the export writes, and
	// latestTime on a re-run would silently continue from a previous solution rather
	// than from the conditions being validated.
	out << "startFrom       startTime;\n"
	       "startTime       0;\n\n"
	       "stopAt          endTime;\n";

	if (setup.transient) {
		out << "endTime         " << foamNumber(setup.tEnd) << ";\n\n"
		    << "deltaT          " << foamNumber(setup.dt) << ";\n\n";

		// Ten dumps over the run, so a transient comparison has frames to line up
		// against AxiSim's own keyframes without filling the disk.
		const double interval = setup.tEnd > 0.0 ? setup.tEnd / 10.0 : setup.dt;
		out << "writeControl    runTime;\n"
		    << "writeInterval   " << foamNumber(interval) << ";\n";
	}
	else {
		// For a steady solver deltaT is an iteration counter, so this is a cap of
		// steadyIterations SIMPLE iterations, not a number of seconds. It is only a
		// backstop -- residualControl in fvSolution is what normally stops the run.
		out << "endTime         " << setup.steadyIterations << ";\n\n"
		       "deltaT          1;\n\n"
		       "writeControl    timeStep;\n"
		    << "writeInterval   " << setup.steadyIterations << ";\n";
	}

	out << "purgeWrite      0;\n\n"
	       "writeFormat     ascii;\n"
	       "writePrecision  12;\n"
	       "writeCompression off;\n\n"
	       "timeFormat      general;\n"
	       "timePrecision   6;\n\n"
	       "runTimeModifiable true;\n\n";

	if (setup.solveEnergy || setup.solveConcentration) {

		out << "functions\n"
		       "{\n";

		if (setup.solveEnergy) {
			// k is a conductivity; scalarTransport wants a diffusivity, so what goes
			// out is alpha = k/(rho*cp).
			writeScalarTransport(out, "T", setup.alpha());
		}
		if (setup.solveEnergy && setup.solveConcentration) out << '\n';
		if (setup.solveConcentration) {
			writeScalarTransport(out, "C", setup.D);
		}

		out << "}\n\n";
	}

	out << "// ************************************************************************* //\n";

	out.flush();
	if (!out) {
		std::cerr << "writeControlDict: write failed for " << path.string() << '\n';
		return false;
	}

	return true;
}

bool writeFvSchemes(const std::filesystem::path& path, const FoamCaseSetup& setup) {

	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out) {
		std::cerr << "writeFvSchemes: cannot open " << path.string() << '\n';
		return false;
	}

	writeFoamHeader(out, "dictionary", "fvSchemes", "system");

	out << "ddtSchemes\n"
	       "{\n";
	if (!setup.transient) {
		out << "    default         steadyState;\n";
	}
	else {
		// Euler is backward Euler, backward is BDF2 -- the same two schemes TimeScheme
		// offers, under OpenFOAM's names.
		out << "    default         " << (setup.secondOrderTime ? "backward" : "Euler") << ";\n";
	}
	out << "}\n\n";

	out << "gradSchemes\n"
	       "{\n"
	    << "    default         "
	    << (setup.leastSquaresGradient ? "leastSquares" : "Gauss linear") << ";\n"
	       "}\n\n";

	// `bounded` subtracts the (div(phi)*field) term that a not-yet-converged steady
	// flux leaves behind. It is for steady runs only -- on a transient run the flux
	// is meant to satisfy continuity at every step, and bounding it would be wrong.
	const char* boundedPrefix = setup.transient ? "" : "bounded ";

	const char* interp = "upwind";
	switch (setup.convection) {
		case FoamConvection::Linear:       interp = "linear";       break;
		case FoamConvection::LinearUpwind: interp = "linearUpwind"; break;
		case FoamConvection::Quick:        interp = "QUICK";        break;
		case FoamConvection::Upwind:       break;
	}

	// linearUpwind is the one that needs a gradient to extrapolate with, and it wants
	// the gradient OF THE FIELD being convected -- so these cannot share one entry.
	const bool needsGrad = (setup.convection == FoamConvection::LinearUpwind);

	// Six spaces lines the value up with `default         none;` above it -- every
	// field name here is one character, so the padding is fixed.
	auto divEntry = [&](const char* field) {
		out << "    div(phi," << field << ")      "
		    << boundedPrefix << "Gauss " << interp;
		if (needsGrad) out << " grad(" << field << ")";
		out << ";\n";
	};

	out << "divSchemes\n"
	       "{\n"
	       "    default         none;\n";
	divEntry("U");
	if (setup.solveEnergy)        divEntry("T");
	if (setup.solveConcentration) divEntry("C");

	// The viscous stress term simpleFoam/pimpleFoam always assembles. Not optional:
	// `default none` means every div the solver forms has to be named here.
	out << "    div((nuEff*dev2(T(grad(U))))) Gauss linear;\n"
	       "}\n\n";

	// `corrected` rather than `orthogonal`: the wedge planes are not parallel, so
	// even a perfectly rectangular r-z grid has non-orthogonality to correct for.
	out << "laplacianSchemes\n"
	       "{\n"
	       "    default         Gauss linear corrected;\n"
	       "}\n\n"
	       "interpolationSchemes\n"
	       "{\n"
	       "    default         linear;\n"
	       "}\n\n"
	       "snGradSchemes\n"
	       "{\n"
	       "    default         corrected;\n"
	       "}\n\n";

	out << "// ************************************************************************* //\n";

	out.flush();
	if (!out) {
		std::cerr << "writeFvSchemes: write failed for " << path.string() << '\n';
		return false;
	}

	return true;
}

bool writeFvSolution(const std::filesystem::path& path, const FoamCaseSetup& setup) {

	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out) {
		std::cerr << "writeFvSolution: cannot open " << path.string() << '\n';
		return false;
	}

	writeFoamHeader(out, "dictionary", "fvSolution", "system");

	// Regex keys rather than plain names so the *Final variants a PIMPLE run creates
	// (pFinal, UFinal) are covered without a second copy of every block.
	out << "solvers\n"
	       "{\n"
	       "    \"p.*\"\n"
	       "    {\n"
	       "        solver          GAMG;\n"
	       "        smoother        GaussSeidel;\n"
	       "        tolerance       1e-08;\n"
	       "        relTol          0.01;\n"
	       "    }\n\n"
	       "    \"(U|T|C).*\"\n"
	       "    {\n"
	       "        solver          smoothSolver;\n"
	       "        smoother        symGaussSeidel;\n"
	       "        tolerance       1e-08;\n"
	       "        relTol          0.1;\n"
	       "    }\n"
	       "}\n\n";

	if (setup.transient) {
		out << "PIMPLE\n"
		       "{\n"
		       "    nOuterCorrectors 1;\n"
		       "    nCorrectors     2;\n"
		       "    nNonOrthogonalCorrectors 0;\n"
		       "}\n\n";
	}
	else {
		// 1e-6, NOT the Solver tab's convergence tolerance. The two numbers are not
		// comparable: AxiSim's is its own scaled residual, OpenFOAM's is the initial
		// residual of each outer iteration under its own normalisation. Copying
		// AxiSim's 1e-3 across stops SIMPLE about 9% short of the developed profile
		// -- measured on a Poiseuille pipe, where it costs the peak velocity 0.0180
		// against an analytic 0.0200. 1e-6 lands within 0.03% of a 1e-8 run.
		out << "SIMPLE\n"
		       "{\n"
		       "    nNonOrthogonalCorrectors 0;\n"
		       "    consistent      no;\n\n"
		       "    residualControl\n"
		       "    {\n"
		       "        p               1e-06;\n"
		       "        U               1e-06;\n"
		       "        \"(T|C)\"         1e-06;\n"
		       "    }\n"
		       "}\n\n";

		out << "relaxationFactors\n"
		       "{\n"
		       "    fields\n"
		       "    {\n"
		    << "        p               " << foamNumber(setup.pressureRelaxation) << ";\n"
		       "    }\n"
		       "    equations\n"
		       "    {\n"
		    << "        U               " << foamNumber(setup.momentumRelaxation) << ";\n"
		    << "        \"(T|C)\"         " << foamNumber(setup.momentumRelaxation) << ";\n"
		       "    }\n"
		       "}\n\n";
	}

	out << "// ************************************************************************* //\n";

	out.flush();
	if (!out) {
		std::cerr << "writeFvSolution: write failed for " << path.string() << '\n';
		return false;
	}

	return true;
}

bool writeTransportProperties(const std::filesystem::path& path, const FoamCaseSetup& setup) {

	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out) {
		std::cerr << "writeTransportProperties: cannot open " << path.string() << '\n';
		return false;
	}

	writeFoamHeader(out, "dictionary", "transportProperties", "constant");

	out << "transportModel  Newtonian;\n\n";

	// The incompressible solvers never see a density -- it divides out of the whole
	// equation set -- so mu goes out as the kinematic nu and rho survives only in
	// this comment and in the p field's units.
	out << "// nu = mu / rho = " << foamNumber(setup.mu)
	    << " / " << foamNumber(setup.rho) << "\n"
	    << "nu              " << foamNumber(setup.nu()) << ";\n";

	if (setup.solveConcentration) {
		out << "\n// mass diffusivity. The scalarTransport function object in controlDict\n"
		       "// reads its own copy of this; the entry here is for utilities that expect it.\n"
		    << "DT              " << foamNumber(setup.D) << ";\n";
	}

	out << "\n// ************************************************************************* //\n";

	out.flush();
	if (!out) {
		std::cerr << "writeTransportProperties: write failed for " << path.string() << '\n';
		return false;
	}

	return true;
}

bool writeTurbulenceProperties(const std::filesystem::path& path) {

	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out) {
		std::cerr << "writeTurbulenceProperties: cannot open " << path.string() << '\n';
		return false;
	}

	writeFoamHeader(out, "dictionary", "turbulenceProperties", "constant");

	// Not optional even though nothing here is turbulent: simpleFoam and pimpleFoam
	// construct a turbulence model unconditionally and abort without this file.
	// AxiSim solves the laminar equations, so laminar is the honest setting.
	out << "simulationType  laminar;\n\n";

	out << "// ************************************************************************* //\n";

	out.flush();
	if (!out) {
		std::cerr << "writeTurbulenceProperties: write failed for " << path.string() << '\n';
		return false;
	}

	return true;
}

// ====================================================
// -------------------0/ FIELDS------------------------
// ====================================================
namespace {

// AxiSim scalar BC -> the patch entry that reproduces it. Every branch that wants
// a plain zeroGradient just leaves FieldPatch's default alone.
FieldPatch scalarPatch(const BoundaryCondition& bc) {

	FieldPatch patch;

	switch (bc.type()) {

	case DIRICHLET:
		patch.type = "fixedValue";
		patch.entry = "value           uniform " + foamNumber(bc.value()) + ";";
		break;

	case NEUMANN:
		// A zero fixedGradient IS zeroGradient -- the default -- and zeroGradient is
		// how every OpenFOAM case spells it. Writing the long form for the
		// overwhelmingly common case would only make this harder to diff against a
		// hand-written one.
		if (bc.value() != 0.0) {
			patch.type = "fixedGradient";
			patch.entry = "gradient        uniform " + foamNumber(bc.value()) + ";";
		}
		break;

	case PULSATILE:
		// The mean is the most a time-independent 0/ file can carry. It is a real
		// difference from what AxiSim solves, so it is flagged rather than left for
		// the user to find by comparing two transient results.
		patch.type = "fixedValue";
		patch.entry = "value           uniform " + foamNumber(bc.value()) + ";";
		patch.note = "steady mean of a pulsatile condition";

		if (const auto* pulse = std::get_if<PulsatileParams>(&bc.params)) {
			patch.note += ": value*(1 + " + foamNumber(pulse->amplitude) + "*sin(2*pi*"
				+ foamNumber(pulse->frequency) + "*t))"
				" -- reproduce with uniformFixedValue + a sine Function1";
		}
		break;

	case MICHAELIS_MENTEN:
	case HILL:
		// A saturating consumption flux at the wall. OpenFOAM has no built-in for
		// it, and the default -- no flux at all -- is a DIFFERENT problem, so the
		// note has to be loud: a run made against this file is not a
		// cross-validation of anything.
		patch.note = "PLACEHOLDER, NOT THE SAME PROBLEM: AxiSim applies a saturating wall flux";

		if (const auto* mm = std::get_if<MichaelisMentenParams>(&bc.params)) {
			patch.note += " (Michaelis-Menten, Vmax=" + foamNumber(mm->Vmax)
				+ " Km=" + foamNumber(mm->Km) + ")";
		}
		else if (const auto* hill = std::get_if<HillParams>(&bc.params)) {
			patch.note += " (Hill, Vmax=" + foamNumber(hill->Vmax)
				+ " Km=" + foamNumber(hill->Km) + " n=" + foamNumber(hill->n) + ")";
		}

		patch.note += "; swap in a codedMixed to match it";
		break;

	// FULLY_DEVELOPED is a zero streamwise gradient, which is what zeroGradient
	// already is on a face whose normal points downstream. NONE has nothing to say.
	// Both keep the default.
	case FULLY_DEVELOPED:
	case NONE:
	default:
		break;
	}

	return patch;
}

// U cannot go through scalarPatch: it is one vector field built from two scalar
// conditions, and OpenFOAM's fixedValue/zeroGradient act on the whole vector. A
// group that fixes one component and leaves the other free has no exact spelling.
FieldPatch velocityPatch(const BoundarySegmentGroup& group) {

	const BoundaryCondition axial =
		BoundaryDefaults::getEffectiveBC(group, BoundaryVariable::UVelocity);
	const BoundaryCondition radial =
		BoundaryDefaults::getEffectiveBC(group, BoundaryVariable::VVelocity);

	// PULSATILE counts as fixed -- its mean is what a steady export can carry.
	auto isFixed = [](const BoundaryCondition& bc) {
		return bc.type() == DIRICHLET || bc.type() == PULSATILE;
	};

	FieldPatch patch;

	// Neither component pinned: the default zeroGradient is the whole answer.
	if (!isFixed(axial) && !isFixed(radial)) return patch;

	// x is axial, y is radial, z is the wedge direction -- verticesFromBlock lays
	// every vertex out that way. A single-cell wedge carries no swirl, so z is 0.
	const double u = isFixed(axial)  ? axial.value()  : 0.0;
	const double v = isFixed(radial) ? radial.value() : 0.0;

	if (isFixed(axial) && isFixed(radial) && u == 0.0 && v == 0.0) {
		patch.type = "noSlip";
		return patch;
	}

	patch.type = "fixedValue";
	patch.entry = "value           uniform (" + foamNumber(u) + " " + foamNumber(v) + " 0);";

	if (isFixed(axial) != isFixed(radial)) {
		patch.note = "one velocity component was free in AxiSim; pinned to 0 here, "
			"because fixedValue takes the whole vector";
	}
	else if (axial.type() == PULSATILE || radial.type() == PULSATILE) {
		patch.note = "steady mean of a pulsatile inlet"
			" -- reproduce with uniformFixedValue + a sine Function1";
	}

	return patch;
}

// Constraint patches are not a condition the user picks: the field entry MUST name
// the same constraint as the mesh patch, in every field file, or the solver aborts
// while constructing the field. Returns null for a patch whose entry comes from the
// group's boundary conditions instead.
//
// The name comes from foamPatchTypeName rather than a second set of literals,
// precisely because the two spellings have to agree.
const char* constraintPatchType(BoundaryFOAMType type) {

	switch (type) {
		case BoundaryFOAMType::WEDGE:
		case BoundaryFOAMType::SYMMETRY_PLANE:
			return foamPatchTypeName(type);
		default:
			break;
	}
	return nullptr;
}

}

std::vector<FoamField> initialFieldsFromDict(
	const BlockMeshDict& dict,
	const std::vector<BoundarySegmentGroup>& groups,
	const FoamCaseSetup& setup
) {

	// Starting a scalar at 0 is plain wrong for temperature (0 K) and needlessly slow
	// for concentration, so seed the interior from the inlet value where there is one.
	auto inletValue = [&groups](BoundaryVariable variable) {

		for (const BoundarySegmentGroup& group : groups) {

			if (group.type != BoundaryType::VELOCITY_INLET) continue;

			const auto found = group.bcs.find(variable);
			if (found != group.bcs.end() && found->second.type() == DIRICHLET)
				return found->second.value();
		}
		return 0.0;
	};

	std::vector<FoamField> fields;

	fields.push_back({
		.name = "U",
		.className = "volVectorField",
		.dimensions = "[0 1 -1 0 0 0 0]",
		.internalField = "uniform (0 0 0)"
	});

	fields.push_back({
		.name = "p",
		.variable = BoundaryVariable::Pressure,
		.dimensions = "[0 2 -2 0 0 0 0]",
		.internalField = "uniform 0",
		.note = "p is KINEMATIC (p/rho), which is what the incompressible solvers want."
			" AxiSim stores pressure in Pa, so divide any non-zero value below by rho."
	});

	// Gated on the Solver tab's field checkboxes, not on which groups happen to hold
	// a condition: a field that is being solved needs its file even if every patch
	// ends up on a default, and the scalarTransport entries in controlDict are
	// written off the same two flags, so the two must not be able to disagree.
	if (setup.solveEnergy) {
		fields.push_back({
			.name = "T",
			.variable = BoundaryVariable::StaticTemperature,
			.dimensions = "[0 0 0 1 0 0 0]",
			.internalField = "uniform " + foamNumber(
				inletValue(BoundaryVariable::StaticTemperature))
		});
	}

	if (setup.solveConcentration) {
		fields.push_back({
			.name = "C",
			.variable = BoundaryVariable::Concentration,
			.dimensions = "[0 -3 0 0 1 0 0]",
			.internalField = "uniform " + foamNumber(
				inletValue(BoundaryVariable::Concentration)),
			.note = "dimensioned as mol/m^3, matching the base SI values AxiSim stores."
		});
	}

	// One pass over the mesh's patches filling every field, rather than one pass per
	// field: a patch can then never end up present in one file and missing from
	// another, which the solver treats as fatal rather than defaulting.
	for (const auto& entry : dict.boundary) {

		const std::string& patchName = entry.first;
		const BoundaryFOAM& patch = entry.second;

		// Both of these depend on the patch alone, so they are resolved once here
		// rather than per field.
		const char* constraint = constraintPatchType(patch.type);
		const BoundarySegmentGroup* group =
			BoundaryGet::getBoundaryGroupByID(groups, patch.groupID);

		for (FoamField& field : fields) {

			FieldPatch fieldPatch;

			if (constraint) {
				fieldPatch.type = constraint;
			}
			else if (!group) {
				// boundaryFromMultiblock's "unassigned" fallback, or a group that
				// vanished between the two calls. Either way there is nothing to
				// derive an entry from, so say so instead of guessing silently.
				fieldPatch.note = "no boundary group behind this patch. zeroGradient is a guess";
			}
			else if (field.name == "U") {
				fieldPatch = velocityPatch(*group);
			}
			else {
				fieldPatch = scalarPatch(
					BoundaryDefaults::getEffectiveBC(*group, field.variable));
			}

			field.boundaryField[patchName] = fieldPatch;
		}
	}

	return fields;
}

bool writeInitialFields(const std::filesystem::path& dir, const std::vector<FoamField>& fields) {

	for (const FoamField& field : fields) {

		const std::filesystem::path path = dir / field.name;

		// Binary for the same reason the dict is: written on Windows, read by a
		// solver under WSL, and both sides should see the same bytes.
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		if (!out) {
			std::cerr << "writeInitialFields: cannot open " << path.string() << '\n';
			return false;
		}

		writeFoamHeader(out, field.className, field.name, "0");

		if (!field.note.empty()) out << "// " << field.note << "\n\n";

		out << "dimensions      " << field.dimensions << ";\n\n"
			<< "internalField   " << field.internalField << ";\n\n"
			   "boundaryField\n"
			   "{\n";

		for (const auto& entry : field.boundaryField) {

			out << "    " << entry.first << "\n"
			       "    {\n"
			    << "        type            " << entry.second.type << ";\n";

			if (!entry.second.entry.empty())
				out << "        " << entry.second.entry << "\n";

			if (!entry.second.note.empty())
				out << "        // " << entry.second.note << "\n";

			out << "    }\n";
		}

		out << "}\n\n"
		       "// ************************************************************************* //\n";

		// Buffered like the dict, so the stream state is worth one last look before
		// claiming the file was written.
		out.flush();
		if (!out) {
			std::cerr << "writeInitialFields: write failed for " << path.string() << '\n';
			return false;
		}
	}

	return true;
}
