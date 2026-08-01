#include "file_converter.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>

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

	// Tolerance relative to the block's own radial extent, so a sketch corner
	// that landed on the axis at 1e-17 still reads as on-axis in a domain
	// measured in microns. makeRectBlock puts an exact 0.0 there, but blocks
	// built from sketch geometry (makeQuadBlock/makeTransfiniteBlock) do not.
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

// Every Foam dictionary opens with a FoamFile sub-dict. It is not decoration:
// IOobject reads it to decide the file's class, and a dict without one is rejected
// before blockMesh ever reaches the vertices.
void writeFoamHeader(std::ofstream& out) {

	out << "/*--------------------------------*- C++ -*----------------------------------*\\\n"
	       "  Written by AxiSim. Axisymmetric wedge mesh, one cell thick in the\n"
	       "  circumferential direction -- both wedge planes are patches of type wedge.\n"
	       "\\*---------------------------------------------------------------------------*/\n"
	       "FoamFile\n"
	       "{\n"
	       "    version     2.0;\n"
	       "    format      ascii;\n"
	       "    class       dictionary;\n"
	       "    object      blockMeshDict;\n"
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
	// makes the hex's first face wind correctly. A block whose corners were handed
	// to makeQuadBlock in another order comes out inside-out here, and blockMesh
	// rejects it for negative volume.
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

		const BoundarySegmentGroup* group = nullptr;
		for (const BoundarySegmentGroup& g : groups)
			if (g.id == groupID) { group = &g; break; }

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

	writeFoamHeader(out);

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