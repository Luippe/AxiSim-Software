#include "file_converter.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <set>

#include "boundary_func.h"
#include "math_func.h"
#include "multiblock.h"

namespace {

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

// Resolve a hex-local face through a block's welded labels. Returns false when
// fewer than 3 distinct corners survive -- that is the face lying ON the axis,
// which has zero area and must never be emitted. A face that merely touches the
// axis keeps 3 and goes out as a triangle.
bool globalFace(const std::array<int, 4>& local, const std::array<int, 8>& labels,
                std::array<int, 4>& out) {

	int distinct = 0;
	for (int k = 0; k < 4; k++) {
		out[k] = labels[local[k]];

		bool seen = false;
		for (int m = 0; m < k; m++)
			if (out[m] == out[k]) { seen = true; break; }
		if (!seen) distinct++;
	}
	return distinct >= 3;
}

const char* edgeName(Edge e) {
	switch (e) {
		case Edge::West:  return "West";
		case Edge::East:  return "East";
		case Edge::South: return "South";
		case Edge::North: break;
	}
	return "North";
}

// The two (z, r) endpoints of a block edge. Mirrors edgeFaceLabels: West/East are
// the fixed-J (axial) ends and run along i, South/North are the fixed-I (radial)
// ends and run along j.
void blockEdgeEndpoints(const Block& block, Edge e, Vec2& first, Vec2& second) {
	switch (e) {
		case Edge::East:
			first = block.node(0, block.nz);
			second = block.node(block.nr, block.nz);
			return;
		case Edge::South:
			first = block.node(0, 0);
			second = block.node(0, block.nz);
			return;
		case Edge::North:
			first = block.node(block.nr, 0);
			second = block.node(block.nr, block.nz);
			return;
		case Edge::West:
			break;
	}
	first = block.node(0, 0);
	second = block.node(block.nr, 0);
}

// Boundary group of the sketch edge nearest `point`, or -1 if none is within `tol`.
//
// Deliberately the same nearest-edge rule createMultiBlockFVMesh runs on face
// centres (solver/source/mesh.cpp), so the exported patches and the solver's BC
// groups cut the boundary the same way. Ungrouped sketch edges are skipped rather
// than matched-then-rejected: an untagged edge lying closer must not shadow the
// tagged one behind it.
int groupAtPoint(Vec2 point, const std::vector<BoundaryEdge>& edges,
                 const std::vector<BoundaryVertex>& vertices, double tol) {

	int best = -1;
	double bestDist = tol;

	for (const BoundaryEdge& edge : edges) {
		if (edge.groupID < 0) continue;
		if (!edgeInRange(edge, vertices.size())) continue;

		const Vec2 near = closestPointOnSegment(
			point, vertices[edge.v0].pos, vertices[edge.v1].pos);

		const double dz = point.z - near.z;
		const double dr = point.r - near.r;
		const double dist = std::sqrt(dz * dz + dr * dr);

		if (dist < bestDist) {
			bestDist = dist;
			best = edge.groupID;
		}
	}

	return best;
}

// Largest |z| or r anywhere in the mesh -- the length the match tolerance scales
// with. Same quantity as createMultiBlockFVMesh's max(g.L, g.R) for a domain whose
// blocks span it, so both paths end up with the same tolerance.
double multiblockExtent(const MultiBlockMesh& mesh) {

	double extent = 0.0;
	for (const Block& block : mesh.blocks)
		for (const MBNode& node : block.nodes)
			extent = std::max(extent, std::max(std::fabs(node.z), std::fabs(node.r)));

	return extent;
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

// NOT "C". OpenFOAM reserves that name for the cell-centre field -- mesh.C() -- and
// `postProcess -func writeCellCentres` writes it as a volVectorField straight into
// the time directory. A concentration exported as C is silently overwritten by its
// own cell centres the first time anyone asks where the cells are, which is exactly
// what a comparison against AxiSim needs to do. Verified against v2606.
//
// Temperature keeps "T": that is OpenFOAM's own name for it and collides with
// nothing. The other reserved geometry name is "V" (cell volumes).
const char* const kConcentrationField = "Conc";

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

VertexWeld weldVertices(const MultiBlockMesh& mesh, double wedgeAngleDeg) {

	VertexWeld weld;
	weld.labels.reserve(mesh.blocks.size());

	// Far below one cell in any mesh AxiSim can build, and far above the ulp-scale
	// drift between two blocks' spellings of the same band corner. The max against
	// a degenerate extent only keeps the division below well defined.
	const double extent = multiblockExtent(mesh);
	const double tol = 1e-9 * (extent > 0.0 ? extent : 1.0);

	// Bucket on a grid one tolerance wide so the search stays linear in the corner
	// count. The 26 neighbouring buckets are probed as well -- two points within tol
	// of each other can still fall on opposite sides of a bucket boundary.
	std::map<std::array<long long, 3>, std::vector<int>> grid;

	auto bucketOf = [&](const Vec3& v) {
		return std::array<long long, 3>{
			std::llround(v.x / tol), std::llround(v.y / tol), std::llround(v.z / tol)
		};
	};

	for (const Block& block : mesh.blocks) {

		const std::vector<Vec3> corners = verticesFromBlock(block, wedgeAngleDeg);

		// Leaving the row out rather than padding it: a short row would weld to
		// label 0 and hand blockMesh a hex quietly pointing at the wrong corner,
		// whereas a missing one trips the size check every caller runs.
		if (corners.size() != 8) {
			std::cerr << "weldVertices: block " << block.id << " gave "
				<< corners.size() << " corners, expected 8\n";
			continue;
		}

		std::array<int, 8> labels = { 0,0,0,0,0,0,0,0 };

		for (int k = 0; k < 8; k++) {

			const Vec3& v = corners[k];
			const std::array<long long, 3> home = bucketOf(v);

			int match = -1;
			for (long long dx = -1; dx <= 1 && match < 0; dx++)
			for (long long dy = -1; dy <= 1 && match < 0; dy++)
			for (long long dz = -1; dz <= 1 && match < 0; dz++) {

				const auto bucket =
					grid.find({ home[0] + dx, home[1] + dy, home[2] + dz });
				if (bucket == grid.end()) continue;

				for (int candidate : bucket->second) {
					const Vec3& q = weld.points[candidate];
					if (std::fabs(q.x - v.x) <= tol &&
					    std::fabs(q.y - v.y) <= tol &&
					    std::fabs(q.z - v.z) <= tol) { match = candidate; break; }
				}
			}

			if (match < 0) {
				match = (int)weld.points.size();
				weld.points.push_back(v);
				grid[home].push_back(match);
			}

			labels[k] = match;
		}

		weld.labels.push_back(labels);
	}

	return weld;
}

Hex hexFromBlock(const Block& block, const std::array<int, 8>& labels) {

	Hex hex;

	hex.indices = labels;
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
	const std::vector<BoundarySegmentGroup>& groups,
	const std::vector<BoundaryEdge>& boundaryEdges,
	const std::vector<BoundaryVertex>& boundaryVertices,
	const VertexWeld& weld
) {

	std::unordered_map<std::string, BoundaryFOAM> boundary;

	// Every face below is written against weld.labels[bi]. A weld built from a
	// different mesh would silently name corners of the wrong block.
	if (weld.labels.size() != mesh.blocks.size()) {
		std::cerr << "boundaryFromMultiblock: weld covers " << weld.labels.size()
			<< " blocks but the mesh has " << mesh.blocks.size()
			<< " -- no patches written\n";
		return boundary;
	}

	// Both wedge planes exist on every block, so seed them first. That also lets
	// them win the name-collision loop below against a user group whose name
	// sanitizes to "front" or "back". References into an unordered_map survive
	// rehashing, so holding these across the inserts underneath is safe.
	BoundaryFOAM& back  = boundary["back"];
	BoundaryFOAM& front = boundary["front"];
	back.type  = BoundaryFOAMType::WEDGE;
	front.type = BoundaryFOAMType::WEDGE;

	// Interface edges are interior faces: weldVertices gave both blocks the same
	// four corner labels there, so blockMesh reads them as one face. They must NOT
	// be emitted as patch faces -- doing so would wall off the seam and leave the
	// blocks touching but unconnected.
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

		// Dropping the faces of an unmatched edge would hand blockMesh a mesh with
		// a hole and no clue where, so it gets a patch instead. The caller has
		// already warned about the edge by the time this runs.
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

	// Boundary faces sit exactly on the sketch edges, so the match is tight and this
	// only has to absorb floating-point drift. The max against 1.0 mirrors
	// createMultiBlockFVMesh; it makes the tolerance absolute on any domain smaller
	// than a metre, which is slack but harmless -- the search takes the NEAREST edge,
	// so the tolerance only decides when nothing matches at all.
	const double matchTol = 1e-4 * std::max(multiblockExtent(mesh), 1.0);

	for (int bi = 0; bi < (int)mesh.blocks.size(); bi++) {

		const Block& b = mesh.blocks[bi];
		const std::array<int, 8>& labels = weld.labels[bi];

		std::array<int, 4> face;
		if (globalFace(kBackFace,  labels, face)) back.faces.push_back(face);
		if (globalFace(kFrontFace, labels, face)) front.faces.push_back(face);

		for (Edge e : allEdges) {
			if (claimed.count(mbEdgeKey(bi, e))) continue;              // interior
			if (!globalFace(edgeFaceLabels(e), labels, face))           // on the axis:
				continue;                                              // zero area

			Vec2 first, second;
			blockEdgeEndpoints(b, e, first, second);

			auto sampleAt = [&](double t) {
				return groupAtPoint(
					{ first.z + t * (second.z - first.z),
					  first.r + t * (second.r - first.r) },
					boundaryEdges, boundaryVertices, matchTol);
			};

			const int groupID = sampleAt(0.5);

			// Sampled at the quarter points, not the ends: a block corner lies where
			// two sketch groups meet and is equidistant from both, so testing the
			// endpoints would report a conflict on every ordinary corner.
			//
			// blockMeshDict gives a block edge exactly one patch, so an edge that
			// really does span two groups cannot be exported faithfully without
			// splitting the block. Say which one lost rather than let the midpoint
			// decide in silence.
			if (groupID >= 0 && (sampleAt(0.25) != groupID || sampleAt(0.75) != groupID)) {
				std::cerr << "boundaryFromMultiblock: " << edgeName(e) << " edge of block "
					<< b.id << " spans more than one boundary group -- taking the one at"
					" its midpoint. Split the block to export both.\n";
			}

			if (groupID < 0) {
				std::cerr << "boundaryFromMultiblock: " << edgeName(e) << " edge of block "
					<< b.id << " matched no boundary group within " << matchTol
					<< " -- exported as \"unassigned\", which carries no boundary"
					" condition. Tag the sketch edge behind it.\n";
			}

			patchFor(groupID).faces.push_back(face);
		}
	}

	return boundary;
}

BlockMeshDict blockMeshDictFromMultiblock(
	const MultiBlockMesh& mesh,
	const std::vector<BoundarySegmentGroup>& groups,
	const std::vector<BoundaryEdge>& boundaryEdges,
	const std::vector<BoundaryVertex>& boundaryVertices
) {

	BlockMeshDict blockMeshDict;

	const VertexWeld weld = weldVertices(mesh);

	// weldVertices drops a block it could not label, and everything below indexes
	// weld.labels by block. An empty dict is the honest outcome -- blockMesh rejects
	// it immediately, which beats exporting a mesh missing a block.
	if (weld.labels.size() != mesh.blocks.size()) {
		std::cerr << "blockMeshDictFromMultiblock: welded " << weld.labels.size()
			<< " of " << mesh.blocks.size() << " blocks -- no dict written\n";
		return blockMeshDict;
	}

	blockMeshDict.vertices = weld.points;
	for (int i = 0; i < (int)mesh.blocks.size(); i++) {
		blockMeshDict.hexes.push_back(hexFromBlock(mesh.blocks[i], weld.labels[i]));
	}

	blockMeshDict.boundary =
		boundaryFromMultiblock(mesh, groups, boundaryEdges, boundaryVertices, weld);

	// Every interface edge is deliberately left OUT of the patches above, on the
	// promise that the weld left both blocks naming the same four corners there. If
	// it did not, those faces fall through to blockMesh's default patch instead --
	// type `empty`, which costs the case its axial and radial solution directions --
	// and the mesh comes back as one disconnected region per block. checkMesh calls
	// that mesh OK and the run dies much later inside GAMG, so say it here.
	for (const Interface& itf : mesh.interfaces) {

		if (itf.blockA < 0 || itf.blockA >= (int)mesh.blocks.size() ||
			itf.blockB < 0 || itf.blockB >= (int)mesh.blocks.size()) continue;

		std::array<int, 4> a, b;
		const bool live =
			globalFace(edgeFaceLabels(itf.edgeA), weld.labels[itf.blockA], a) &&
			globalFace(edgeFaceLabels(itf.edgeB), weld.labels[itf.blockB], b);

		std::sort(a.begin(), a.end());
		std::sort(b.begin(), b.end());

		if (!live || a != b) {
			std::cerr << "blockMeshDictFromMultiblock: the "
				<< edgeName(itf.edgeA) << "/" << edgeName(itf.edgeB)
				<< " interface between blocks " << mesh.blocks[itf.blockA].id
				<< " and " << mesh.blocks[itf.blockB].id
				<< " did not weld to shared corners -- blockMesh will hand back a"
				" disconnected mesh\n";
		}
	}

	return blockMeshDict;
}

namespace {

// Drop repeated labels CYCLICALLY, so a face the wedge revolve degenerated comes
// out as the polygon it actually is.
//
// A triangle edge sweeps a quad, but an endpoint ON the axis carries one label for
// both wedge planes instead of two, so that quad names it twice and is really a
// triangle. The repeat sits at positions 1,2 when the second endpoint is the one on
// the axis and wraps round to 3,0 when it is the first -- which is why this is
// cyclic rather than plain std::unique. Dropping a repeat preserves the winding, so
// the collapsed face still points out of its owner.
//
// Fewer than 3 labels survive exactly when BOTH endpoints are on the axis. That
// face sweeps nothing and has zero area; callers treat a short result as "skip this
// face", which is what blockMesh does to a block face on the axis too.
std::vector<int> collapseFace(int a, int b, int c, int d) {

	std::vector<int> out;
	out.reserve(4);

	for (int label : { a, b, c, d }) {
		if (out.empty() || out.back() != label) out.push_back(label);
	}

	if (out.size() > 1 && out.front() == out.back()) out.pop_back();

	return out;
}

// A face on its way into PolyMesh, before the ordering pass decides where it lands.
// `neighbour` is -1 on a boundary face, exactly as in the finished mesh.
struct StagedFace {
	std::vector<int> labels;
	int owner = -1;
	int neighbour = -1;
};

// Faces collected under one patch name, before they become a contiguous run.
struct StagedPatch {
	BoundaryFOAMType type = BoundaryFOAMType::PATCH;
	int groupID = -1;
	std::vector<StagedFace> faces;
};

}

PolyMesh polyMeshFromUnstructured(
	const std::vector<Vec2>& points,
	const std::vector<Triangle>& triangles,
	const FVMesh& fvMesh,
	const std::vector<BoundarySegmentGroup>& groups,
	double wedgeAngleDeg
) {

	PolyMesh poly;

	const int nCells = (int)triangles.size();
	const int nPoints2D = (int)points.size();

	// The triangles carry the corners and fvMesh carries the connectivity and the
	// boundary groups, and cell c has to mean the same cell in both. An FVMesh built
	// from a different mesh would export something that looks valid and whose patches
	// belong to another geometry.
	if (nCells == 0 || (int)fvMesh.cells.size() != nCells) {
		std::cerr << "polyMeshFromUnstructured: " << nCells << " triangles against "
			<< fvMesh.cells.size() << " FV cells -- no mesh written\n";
		return poly;
	}

	for (const Triangle& tri : triangles) {
		if (tri.v0 < 0 || tri.v1 < 0 || tri.v2 < 0 ||
			tri.v0 >= nPoints2D || tri.v1 >= nPoints2D || tri.v2 >= nPoints2D) {
			std::cerr << "polyMeshFromUnstructured: a triangle names a point outside the "
				<< nPoints2D << "-point mesh -- no mesh written\n";
			return PolyMesh{};
		}
	}

	double extent = 0.0;
	for (const Vec2& p : points)
		extent = std::max(extent, std::max(std::fabs(p.z), std::fabs(p.r)));

	// A point on the axis gets ONE label for both wedge planes. This is the same
	// collapse blockMesh's geometric merge performs on a block touching r = 0, and it
	// is what turns the cells there into prisms instead of leaving zero-thickness
	// slivers behind. The tolerance matches weldVertices': orders of magnitude below
	// one cell, so only a point meant to BE on the axis can reach it.
	const double axisTol = 1e-9 * (extent > 0.0 ? extent : 1.0);

	const double halfAngle = 0.5 * wedgeAngleDeg * (MB_PI / 180.0);
	const double cosHalf = std::cos(halfAngle);
	const double sinHalf = std::sin(halfAngle);

	// Label of each 2D point on the -angle/2 and +angle/2 planes. Equal on the axis.
	std::vector<int> backOf(nPoints2D, -1);
	std::vector<int> frontOf(nPoints2D, -1);

	for (int i = 0; i < nPoints2D; i++) {

		const Vec2& p = points[i];

		if (p.r <= axisTol) {
			backOf[i] = frontOf[i] = (int)poly.points.size();
			poly.points.push_back({ p.z, 0.0, 0.0 });
			continue;
		}

		// x axial, y radial, z the wedge direction -- the frame verticesFromBlock
		// lays the dict's vertices out in, so both exports of the same project land
		// in the same coordinates and one reader serves both.
		backOf[i] = (int)poly.points.size();
		poly.points.push_back({ p.z, p.r * cosHalf, -p.r * sinHalf });

		frontOf[i] = (int)poly.points.size();
		poly.points.push_back({ p.z, p.r * cosHalf, p.r * sinHalf });
	}

	// Ordered, so two exports of the same mesh come out byte-identical -- the same
	// reason writeBlockMeshDict sorts its patches on the way out.
	std::map<std::string, StagedPatch> patches;

	// Both wedge planes exist on every cell, so seed them first. That also lets them
	// win the name-collision loop below against a user group whose name sanitizes to
	// "front" or "back". References into a std::map survive later inserts.
	StagedPatch& backPlane = patches["back"];
	StagedPatch& frontPlane = patches["front"];
	backPlane.type = BoundaryFOAMType::WEDGE;
	frontPlane.type = BoundaryFOAMType::WEDGE;

	// Resolved patch name per boundary group id, so the sanitize + collision work
	// runs once per group rather than once per face.
	std::unordered_map<int, std::string> nameOfGroup;

	auto patchFor = [&](int groupID) -> StagedPatch& {

		const auto known = nameOfGroup.find(groupID);
		if (known != nameOfGroup.end()) return patches[known->second];

		const BoundarySegmentGroup* group =
			BoundaryGet::getBoundaryGroupByID(groups, groupID);

		// Dropping an unmatched face would hand the solver a mesh with a hole in it
		// and no clue where, so it gets a patch instead. The caller warns about the
		// count once the pass is done.
		const std::string stem = group ? foamPatchName(*group) : "unassigned";

		// Two groups can sanitize to the same word, and the map is keyed by name, so
		// a collision would quietly merge two physically distinct patches.
		std::string name = stem;
		for (int n = 2; patches.count(name); n++)
			name = stem + "_" + std::to_string(n);

		nameOfGroup[groupID] = name;
		StagedPatch& patch = patches[name];
		patch.type = group ? foamType(group->type) : BoundaryFOAMType::PATCH;
		patch.groupID = group ? group->id : -1;
		return patch;
	};

	std::vector<StagedFace> interior;
	interior.reserve(fvMesh.faces.size());

	int axisFaces = 0;
	int unassignedFaces = 0;

	// ---- the sides: one revolved triangle edge per FV face ----
	for (const FVFace& face : fvMesh.faces) {

		// createUnstructuredMesh fills these from the triangle edge the face was
		// built out of. They stay -1 on the multiblock path, whose faces come from
		// the packer and carry no vertex ids -- an FVMesh from there cannot be
		// revolved, and silently exporting part of one would be worse than stopping.
		if (face.v0 < 0 || face.v1 < 0 || face.v0 >= nPoints2D || face.v1 >= nPoints2D) {
			std::cerr << "polyMeshFromUnstructured: a face carries no triangle edge --"
				" this FVMesh is not an unstructured one. No mesh written\n";
			return PolyMesh{};
		}

		if (face.owner < 0 || face.owner >= nCells || face.neighbor >= nCells) {
			std::cerr << "polyMeshFromUnstructured: face owner/neighbour " << face.owner
				<< '/' << face.neighbor << " outside the " << nCells
				<< "-cell mesh -- no mesh written\n";
			return PolyMesh{};
		}

		int a = face.v0;
		int b = face.v1;

		// Wind the swept face so its normal points out of the owner. The r-z normal
		// of the segment a->b is (dr, -dz) and the revolve carries that straight
		// through, so testing it against FVFace::normal -- which createUnstructuredMesh
		// has already flipped to face away from the owner's centre -- settles the
		// direction without redoing that test and without trusting gmsh's winding.
		const double dz = points[b].z - points[a].z;
		const double dr = points[b].r - points[a].r;

		if (dr * face.normal.z - dz * face.normal.r < 0.0) std::swap(a, b);

		std::vector<int> labels =
			collapseFace(backOf[a], backOf[b], frontOf[b], frontOf[a]);

		// The edge lies ON the axis and sweeps zero area.
		if (labels.size() < 3) {
			axisFaces++;
			continue;
		}

		if (face.neighbor >= 0) {

			int owner = face.owner;
			int neighbour = face.neighbor;

			// polyMesh insists on owner < neighbour, and reversing the winding is what
			// keeps the normal pointing from the new owner towards the new neighbour.
			// createUnstructuredMesh numbers them the right way round already -- this
			// is here so the invariant does not depend on that.
			if (owner > neighbour) {
				std::swap(owner, neighbour);
				std::reverse(labels.begin(), labels.end());
			}

			interior.push_back({ std::move(labels), owner, neighbour });
			continue;
		}

		if (face.boundaryGroupID < 0) unassignedFaces++;

		patchFor(face.boundaryGroupID).faces.push_back(
			{ std::move(labels), face.owner, -1 });
	}

	// ---- the two wedge planes: one triangle each per cell ----
	int flatCells = 0;

	for (int c = 0; c < nCells; c++) {

		int v0 = triangles[c].v0;
		int v1 = triangles[c].v1;
		int v2 = triangles[c].v2;

		const Vec2& a = points[v0];
		const Vec2& b = points[v1];
		const Vec2& d = points[v2];

		// Counter-clockwise in (z, r) -- which is counter-clockwise in OpenFOAM's
		// (x, y) -- makes the front plane's winding give a normal along +z, out of the
		// cell. gmsh promises no orientation and triangleArea2D takes the absolute
		// value, so it is measured here rather than assumed: an inside-out cell is a
		// negative volume the solver refuses to start on.
		const double twiceArea =
			(b.z - a.z) * (d.r - a.r) - (b.r - a.r) * (d.z - a.z);

		if (twiceArea < 0.0) std::swap(v1, v2);
		if (twiceArea == 0.0) flatCells++;

		frontPlane.faces.push_back({ { frontOf[v0], frontOf[v1], frontOf[v2] }, c, -1 });
		backPlane.faces.push_back({ { backOf[v0], backOf[v2], backOf[v1] }, c, -1 });
	}

	// ---- lay the faces out the way polyMesh reads them ----
	// Upper-triangular order: by owner, then by neighbour. OpenFOAM checks this on
	// read rather than repairing it. The keys are unique, so no tie to break.
	std::sort(interior.begin(), interior.end(),
		[](const StagedFace& x, const StagedFace& y) {
			return x.owner != y.owner ? x.owner < y.owner : x.neighbour < y.neighbour;
		});

	poly.nCells = nCells;
	poly.faces.reserve(interior.size());
	poly.faceOwner.reserve(interior.size());
	poly.faceNeighbour.reserve(interior.size());

	for (StagedFace& face : interior) {
		poly.faces.push_back(std::move(face.labels));
		poly.faceOwner.push_back(face.owner);
		poly.faceNeighbour.push_back(face.neighbour);
	}

	for (auto& entry : patches) {

		StagedPatch& patch = entry.second;

		// Every face of this group collapsed on the axis, so there is nothing left to
		// put in the patch. Emitting it empty would also need a 0/ entry for a patch
		// with no faces to apply it to; blockMesh's own wedge output leaves the axis
		// out the same way.
		if (patch.faces.empty()) continue;

		std::stable_sort(patch.faces.begin(), patch.faces.end(),
			[](const StagedFace& x, const StagedFace& y) { return x.owner < y.owner; });

		FoamPolyPatch out;
		out.name = entry.first;
		out.type = patch.type;
		out.groupID = patch.groupID;
		out.startFace = (int)poly.faces.size();
		out.nFaces = (int)patch.faces.size();

		for (StagedFace& face : patch.faces) {
			poly.faces.push_back(std::move(face.labels));
			poly.faceOwner.push_back(face.owner);
		}

		poly.patches.push_back(out);
	}

	if (unassignedFaces > 0) {
		std::cerr << "polyMeshFromUnstructured: " << unassignedFaces << " boundary face(s)"
			" matched no boundary group -- exported as \"unassigned\", which carries no"
			" boundary condition. Tag the sketch edges behind them.\n";
	}

	if (flatCells > 0) {
		std::cerr << "polyMeshFromUnstructured: " << flatCells << " triangle(s) have zero"
			" area -- their cells have no volume and checkMesh will reject the mesh\n";
	}

	std::cout << "polyMesh: " << poly.nCells << " cells, " << poly.points.size()
		<< " points, " << poly.faces.size() << " faces (" << poly.nInternalFaces()
		<< " internal), " << poly.patches.size() << " patches; " << axisFaces
		<< " face(s) on the axis collapsed to zero area and were dropped\n";

	return poly;
}

bool writeBlockMeshDict(const std::filesystem::path& path, const BlockMeshDict& dict) {

	// Every hex and every patch face below indexes dict.vertices directly. An
	// out-of-range label is a weld that did not cover the mesh it was written
	// against; blockMesh reports it, if at all, as a negative-volume cell somewhere
	// unrelated, so it is worth catching before anything reaches disk.
	const int nPoints = (int)dict.vertices.size();

	for (std::size_t i = 0; i < dict.hexes.size(); i++) {
		for (int label : dict.hexes[i].indices) {
			if (label < 0 || label >= nPoints) {
				std::cerr << "writeBlockMeshDict: hex " << i << " names vertex "
					<< label << " of " << nPoints << '\n';
				return false;
			}
		}
	}

	for (const auto& entry : dict.boundary) {
		for (const std::array<int, 4>& face : entry.second.faces) {
			for (int label : face) {
				if (label < 0 || label >= nPoints) {
					std::cerr << "writeBlockMeshDict: patch " << entry.first
						<< " names vertex " << label << " of " << nPoints << '\n';
					return false;
				}
			}
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

	// Already welded and already global: position in this list IS the label.
	out << "vertices\n(\n";
	for (const Vec3& v : dict.vertices) {
		out << "    (" << v.x << " " << v.y << " " << v.z << ")\n";
	}
	out << ");\n\n";

	out << "blocks\n(\n";
	for (const Hex& hex : dict.hexes) {

		out << "    hex (";
		for (int k = 0; k < 8; k++) {
			out << (k ? " " : "") << hex.indices[k];
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

	// `points` rather than OpenFOAM's default `topology`, and NOT for the block
	// seams: weldVertices already gave those shared labels, which is what the
	// topological merge looks for and all it looks for. It is the AXIS that needs
	// the geometric merge. A block touching r = 0 has its two wedge planes meet
	// there, and blockMesh lays that edge's points down once per plane whatever the
	// hex says; only the geometric pass fuses the pair and collapses those cells to
	// prisms. Under `topology` they stay zero-thickness hexes whose degenerate faces
	// land on the default `empty` patch -- 224 of them on the pipe-with-obstacle
	// case, and an empty patch is exactly what a wedge case cannot afford.
	out << "mergeType "
		<< (dict.mergeType == MergeType::MERGE_TOPOLOGY ? "topology" : "points")
		<< ";\n\n";

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

bool writePolyMesh(const std::filesystem::path& dir, const PolyMesh& mesh) {

	const int nPoints = (int)mesh.points.size();
	const int nFaces = (int)mesh.faces.size();
	const int nInternal = mesh.nInternalFaces();

	if (nPoints == 0 || nFaces == 0 || mesh.nCells <= 0) {
		std::cerr << "writePolyMesh: nothing to write -- " << nPoints << " points, "
			<< nFaces << " faces, " << mesh.nCells << " cells\n";
		return false;
	}

	if ((int)mesh.faceOwner.size() != nFaces || nInternal > nFaces) {
		std::cerr << "writePolyMesh: " << nFaces << " faces against "
			<< mesh.faceOwner.size() << " owners and " << nInternal
			<< " neighbours\n";
		return false;
	}

	// OpenFOAM reads these files as written -- it validates almost none of this on
	// the way in, so a label out of range or a face wound the wrong way surfaces much
	// later as a wrong answer or a crash somewhere unrelated. Catch it here.
	for (int f = 0; f < nFaces; f++) {

		if (mesh.faces[f].size() < 3) {
			std::cerr << "writePolyMesh: face " << f << " has only "
				<< mesh.faces[f].size() << " points\n";
			return false;
		}

		for (int label : mesh.faces[f]) {
			if (label < 0 || label >= nPoints) {
				std::cerr << "writePolyMesh: face " << f << " names point " << label
					<< " of " << nPoints << '\n';
				return false;
			}
		}

		if (mesh.faceOwner[f] < 0 || mesh.faceOwner[f] >= mesh.nCells) {
			std::cerr << "writePolyMesh: face " << f << " is owned by cell "
				<< mesh.faceOwner[f] << " of " << mesh.nCells << '\n';
			return false;
		}

		// Upper-triangular order, which is a requirement and not a convention.
		if (f < nInternal) {

			if (mesh.faceNeighbour[f] < 0 || mesh.faceNeighbour[f] >= mesh.nCells) {
				std::cerr << "writePolyMesh: internal face " << f << " has neighbour "
					<< mesh.faceNeighbour[f] << " of " << mesh.nCells << '\n';
				return false;
			}

			if (mesh.faceOwner[f] >= mesh.faceNeighbour[f]) {
				std::cerr << "writePolyMesh: internal face " << f << " has owner "
					<< mesh.faceOwner[f] << " >= neighbour " << mesh.faceNeighbour[f]
					<< " -- polyMesh needs owner < neighbour\n";
				return false;
			}

			if (f > 0 &&
				(mesh.faceOwner[f] < mesh.faceOwner[f - 1] ||
				 (mesh.faceOwner[f] == mesh.faceOwner[f - 1] &&
				  mesh.faceNeighbour[f] < mesh.faceNeighbour[f - 1]))) {
				std::cerr << "writePolyMesh: internal face " << f
					<< " breaks upper-triangular order\n";
				return false;
			}
		}
	}

	// The patches have to TILE the boundary block. A face left out of every patch is
	// one the solver has no condition for, and it aborts on it while constructing the
	// first field -- naming the field, not the hole in the mesh.
	int expected = nInternal;
	for (const FoamPolyPatch& patch : mesh.patches) {

		if (patch.startFace != expected || patch.nFaces <= 0) {
			std::cerr << "writePolyMesh: patch " << patch.name << " starts at "
				<< patch.startFace << " with " << patch.nFaces << " faces, expected "
				<< expected << '\n';
			return false;
		}

		expected += patch.nFaces;
	}

	if (expected != nFaces) {
		std::cerr << "writePolyMesh: the patches cover faces " << nInternal << ".."
			<< expected << " but the mesh has " << nFaces << '\n';
		return false;
	}

	std::error_code ec;
	std::filesystem::create_directories(dir, ec);
	if (ec) {
		std::cerr << "writePolyMesh: cannot create " << dir.string()
			<< " -- " << ec.message() << '\n';
		return false;
	}

	// Binary for the same reason the dict is: written on Windows, read by OpenFOAM
	// under WSL, and both sides should see the same bytes. 17 digits round-trips a
	// double exactly, which matters more here than in the dict -- these points ARE
	// the mesh, with no blockMesh in between to regenerate them.
	auto openFile = [&](std::ofstream& out, const char* object, const char* className) {

		out.open(dir / object, std::ios::binary | std::ios::trunc);
		if (!out) {
			std::cerr << "writePolyMesh: cannot open " << (dir / object).string() << '\n';
			return false;
		}

		out << std::setprecision(17);
		writeFoamHeader(out, className, object, "constant/polyMesh");
		return true;
	};

	auto closeFile = [](std::ofstream& out, const char* object) {

		out << "\n// ************************************************************************* //\n";

		// Everything above is buffered, so the stream state is worth one last look
		// before the file is called written.
		out.flush();
		if (!out) {
			std::cerr << "writePolyMesh: write failed for " << object << '\n';
			return false;
		}
		return true;
	};

	{
		std::ofstream out;
		if (!openFile(out, "points", "vectorField")) return false;

		out << nPoints << "\n(\n";
		for (const Vec3& p : mesh.points) {
			out << "(" << p.x << " " << p.y << " " << p.z << ")\n";
		}
		out << ")\n";

		if (!closeFile(out, "points")) return false;
	}

	{
		std::ofstream out;
		if (!openFile(out, "faces", "faceList")) return false;

		// `n(a b c ...)`: the point count is part of the entry, which is what lets one
		// list hold the quads, the collapsed triangles and the wedge planes together.
		out << nFaces << "\n(\n";
		for (const std::vector<int>& face : mesh.faces) {

			out << face.size() << "(";
			for (std::size_t k = 0; k < face.size(); k++) {
				out << (k ? " " : "") << face[k];
			}
			out << ")\n";
		}
		out << ")\n";

		if (!closeFile(out, "faces")) return false;
	}

	{
		std::ofstream out;
		if (!openFile(out, "owner", "labelList")) return false;

		out << nFaces << "\n(\n";
		for (int owner : mesh.faceOwner) out << owner << "\n";
		out << ")\n";

		if (!closeFile(out, "owner")) return false;
	}

	{
		std::ofstream out;
		if (!openFile(out, "neighbour", "labelList")) return false;

		// Internal faces only -- its LENGTH is how OpenFOAM tells the internal faces
		// from the boundary ones, so this is not just the owner list trimmed.
		out << nInternal << "\n(\n";
		for (int neighbour : mesh.faceNeighbour) out << neighbour << "\n";
		out << ")\n";

		if (!closeFile(out, "neighbour")) return false;
	}

	{
		std::ofstream out;
		if (!openFile(out, "boundary", "polyBoundaryMesh")) return false;

		out << mesh.patches.size() << "\n(\n";
		for (const FoamPolyPatch& patch : mesh.patches) {

			out << "    " << patch.name << "\n"
			       "    {\n"
			    << "        type            " << foamPatchTypeName(patch.type) << ";\n";

			// What blockMesh writes for a wall, and what the wall-aware function
			// objects (wallShearStress, yPlus) select on.
			if (patch.type == BoundaryFOAMType::WALL) {
				out << "        inGroups        1(wall);\n";
			}

			out << "        nFaces          " << patch.nFaces << ";\n"
			    << "        startFace       " << patch.startFace << ";\n"
			       "    }\n";
		}
		out << ")\n";

		if (!closeFile(out, "boundary")) return false;
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
	       "// the Transient checkbox, and the relaxation factors come from the Solver\n"
	       "// tab. On a transient run so do endTime and deltaT. On a steady run endTime\n"
	       "// is an ITERATION CAP and is not the Solver tab's value: it is the larger of\n"
	       "// that and 2000, because residualControl is what should stop this run and a\n"
	       "// reference cut short is worse than one that iterates longer than AxiSim did.\n\n";

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
			writeScalarTransport(out, kConcentrationField, setup.D);
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

	// Pad out to the column `default         none;` sets, whatever the name's length.
	auto divEntry = [&](const std::string& field) {
		const std::string key = "div(phi," + field + ")";
		out << "    " << key
		    << std::string(key.size() < 16 ? 16 - key.size() : 1, ' ')
		    << boundedPrefix << "Gauss " << interp;
		if (needsGrad) out << " grad(" << field << ")";
		out << ";\n";
	};

	out << "divSchemes\n"
	       "{\n"
	       "    default         none;\n";

	// The one AxiSim setting that CANNOT be carried across. simpleFoam and
	// pimpleFoam assemble div(phi,U) unconditionally, and no scheme entry can
	// remove a term the solver has already put in the matrix -- dropping the entry
	// only makes the run fail on `default none`. So the case is exported with
	// convection ON and says so in both places anyone would look. Silently
	// exporting it would leave two codes solving different equations with the
	// difference charged to discretisation error.
	if (!setup.addConvection) {
		std::cerr << "writeFvSchemes: AxiSim has the convection term disabled, but "
		             "simpleFoam/pimpleFoam always solve it -- the exported case is "
		             "NOT the equation set AxiSim ran\n";

		out << "\n    // AxiSim ran this case with convection DISABLED (Solver tab ->\n"
		       "    // Add Convection Term). A stock OpenFOAM flow solver always\n"
		       "    // assembles div(phi,U), so the entry below IS solved and the two\n"
		       "    // codes are not solving the same equations. Convection is\n"
		       "    // negligible at low Re, which is the only case where comparing\n"
		       "    // them still means something.\n";
	}

	divEntry("U");
	if (setup.solveEnergy)        divEntry("T");
	if (setup.solveConcentration) divEntry(kConcentrationField);

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
	       "    \"(U|T|Conc).*\"\n"
	       "    {\n"
	       "        solver          smoothSolver;\n"
	       "        smoother        symGaussSeidel;\n"
	       "        tolerance       1e-08;\n"
	       "        relTol          0.1;\n"
	       "    }\n"
	       "}\n\n";

	// AxiSim's useNonOrthCorrector is a bool -- one deferred corrector pass or none
	// -- so it maps to 1, not to a pass count. The GUI forces it off on a structured
	// mesh, where the cross term is identically zero, so this is only ever 1 on a
	// multiblock or unstructured case.
	const int nNonOrth = setup.nonOrthCorrector ? 1 : 0;

	if (setup.transient) {
		out << "PIMPLE\n"
		       "{\n"
		       "    nOuterCorrectors 1;\n"
		       "    nCorrectors     2;\n"
		    << "    nNonOrthogonalCorrectors " << nNonOrth << ";\n"
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
		    << "    nNonOrthogonalCorrectors " << nNonOrth << ";\n"
		       "    consistent      no;\n\n"
		       "    residualControl\n"
		       "    {\n"
		       "        p               1e-06;\n"
		       "        U               1e-06;\n"
		       "        \"(T|Conc)\"      1e-06;\n"
		       "    }\n"
		       "}\n\n";

		// T and Conc get separate entries rather than one "(T|Conc)" regex because
		// AxiSim does not relax them the same way: temperature shares the momentum
		// factor, concentration is solved unrelaxed. Under-relaxation cannot move
		// the converged answer, only the path to it, so this is about the dict
		// telling the truth about AxiSim rather than about the result.
		out << "relaxationFactors\n"
		       "{\n"
		       "    fields\n"
		       "    {\n"
		    << "        p               " << foamNumber(setup.pressureRelaxation) << ";\n"
		       "    }\n"
		       "    equations\n"
		       "    {\n"
		    << "        U               " << foamNumber(setup.momentumRelaxation) << ";\n"
		    << "        T               " << foamNumber(setup.momentumRelaxation) << ";\n"
		    << "        " << kConcentrationField << "            "
		    << foamNumber(setup.concentrationRelaxation)
		    << ";   // AxiSim solves concentration unrelaxed\n"
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

	case BCType::DIRICHLET:
		patch.type = "fixedValue";
		patch.entry = "value           uniform " + foamNumber(bc.value()) + ";";
		break;

	case BCType::NEUMANN:
		// A zero fixedGradient IS zeroGradient -- the default -- and zeroGradient is
		// how every OpenFOAM case spells it. Writing the long form for the
		// overwhelmingly common case would only make this harder to diff against a
		// hand-written one.
		if (bc.value() != 0.0) {
			patch.type = "fixedGradient";
			patch.entry = "gradient        uniform " + foamNumber(bc.value()) + ";";
		}
		break;

	case BCType::PULSATILE:
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

	case BCType::MICHAELIS_MENTEN:
	case BCType::HILL:
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

	// FULLY_DEVELOPED is NOT a gradient condition -- it is a Dirichlet whose value
	// varies along the boundary (prescribedBoundaryFaceValue in solver_util.cuh).
	// getAllowedBCType only ever offers it for UVelocity/VVelocity at a
	// VELOCITY_INLET, so velocityPatch is where it is actually handled and no scalar
	// the writer knows about can reach here. A legacy save still could, and letting
	// that fall through to zeroGradient would be a silently different problem.
	case BCType::FULLY_DEVELOPED:
		patch.note = "AxiSim had a fully-developed (parabolic) profile on this scalar,"
			" which zeroGradient is NOT -- see the U file for the codedFixedValue form";
		break;

	// NONE has nothing to say, and keeps the default.
	case BCType::NONE:
	default:
		break;
	}

	return patch;
}

// One velocity component's face values, as an OpenFOAM expression over the patch.
//
// A FULLY_DEVELOPED component is AxiSim's parabola, spelled exactly as
// prescribedBoundaryFaceValue (solver_util.cuh) computes it: value * (1 - (x/L)^2),
// with L the group's total path length and x the face centre's position along the
// boundary -- measured from the AXIS on a vertical boundary and from z = 0 on a
// horizontal one. That is the same split getFaceCenterAlongOrientation makes off
// the face normal, and getAllowedBCType only offers FULLY_DEVELOPED on the
// component whose boundary runs that way, so `coord` follows from the component.
//
// Anything else is the uniform value the writer was already exporting, including
// the 0 a free component gets -- fixedValue takes the whole vector either way.
std::string velocityComponentExpr(const BoundaryCondition& bc, double uniform,
                                  const BoundarySegmentGroup& group, const char* coord) {

	auto uniformField = [](double value) {
		return "scalarField(Cf.size(), scalar(" + foamNumber(value) + "))";
	};

	if (bc.type() != BCType::FULLY_DEVELOPED) return uniformField(uniform);

	// totalLength is a float and stays 0 for a group with no segments.
	// prescribedBoundaryFaceValue returns the uniform value there rather than
	// dividing by zero, so this has to degenerate the same way -- otherwise the two
	// codes are solving different problems on exactly the case that is already odd.
	// The float is widened, not rounded: lengthByGroup widens the same field.
	const double length = group.totalLength;

	if (!(length > 0.0)) return uniformField(bc.value());

	return "scalar(" + foamNumber(bc.value()) + ")*(1.0 - sqr("
		+ coord + "/scalar(" + foamNumber(length) + ")))";
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
		return bc.type() == BCType::DIRICHLET || bc.type() == BCType::PULSATILE;
	};

	FieldPatch patch;

	// A fully-developed inlet is a Dirichlet that VARIES along the patch, so it is
	// neither of the two cases below and used to fall straight through them into the
	// default zeroGradient -- silently, with no note. That exports a velocity inlet
	// carrying no velocity: nothing drives the case, simpleFoam converges on U = 0,
	// and the cross-validation compares AxiSim's Poiseuille profile against zeros.
	//
	// It has to go out as CODE rather than a list of face values. The profile is
	// per-face, blockMesh decides the face order within a patch, and AxiSim has no
	// way to predict that order -- so a `nonuniform List<vector>` would be the right
	// numbers against the wrong faces. codedFixedValue evaluates against
	// patch().Cf() on the solver's own faces and sidesteps the ordering entirely.
	if (axial.type() == BCType::FULLY_DEVELOPED || radial.type() == BCType::FULLY_DEVELOPED) {

		const std::string u = velocityComponentExpr(
			axial, isFixed(axial) ? axial.value() : 0.0, group, "r");
		const std::string v = velocityComponentExpr(
			radial, isFixed(radial) ? radial.value() : 0.0, group, "z");

		// Only the radial parabola is keyed on the axial coordinate, and an unused
		// local is a warning in code the user never asked to read.
		const bool needsZ = (radial.type() == BCType::FULLY_DEVELOPED);

		// Named per GROUP id, not per patch name: the compiled library is cached
		// under this name, so two patches sharing it would get the first one's
		// profile. foamPatchName can collide (boundaryFromMultiblock de-collides
		// afterwards, which velocityPatch cannot see); group ids cannot.
		patch.type = "codedFixedValue";
		patch.entry =
			"value           uniform (0 0 0);\n"
			"        name            axiInlet" + std::to_string(group.id) + ";\n"
			"        code\n"
			"        #{\n"
			"            const vectorField& Cf = patch().Cf();\n"
			"            const scalarField r(sqrt(sqr(Cf.component(1)) + sqr(Cf.component(2))));\n"
			+ (needsZ ? "            const scalarField z(Cf.component(0));\n" : "") +
			"\n"
			"            const scalarField u(" + u + ");\n"
			"            const scalarField v(" + v + ");\n"
			"\n"
			"            // r is a DIRECTION on the wedge, not an axis, so the radial\n"
			"            // component is projected onto the local radial unit vector.\n"
			"            // The max() only guards a face on the axis, where the\n"
			"            // components it scales are zero anyway.\n"
			"            const scalarField rHat(max(r, scalar(SMALL)));\n"
			"\n"
			"            vectorField out(Cf.size(), Zero);\n"
			"            out.replace(0, u);\n"
			"            out.replace(1, v*Cf.component(1)/rHat);\n"
			"            out.replace(2, v*Cf.component(2)/rHat);\n"
			"\n"
			"            operator==(out);\n"
			"        #};";

		// Coded conditions are compiled on first read, which some builds refuse
		// until told to. v2606 ships allowSystemOperations 1 in etc/controlDict, but
		// the openfoam.org builds default it to 0, and the error names the switch
		// without saying the profile is what is being lost.
		patch.note = "fully-developed profile, matching AxiSim's"
			" value*(1-(x/L)^2). Needs allowSystemOperations 1 in etc/controlDict";

		// The other component is written whatever it is, so an unpinned one goes out
		// as 0 -- same compromise the fixedValue branch makes below, and the same
		// reason: the condition covers the whole vector. getAllowedBCType pins it at
		// a VELOCITY_INLET, so this only fires on a group that got here some other way.
		const bool developedIsAxial = (axial.type() == BCType::FULLY_DEVELOPED);

		if (!isFixed(developedIsAxial ? radial : axial)) {
			patch.note += "; the other velocity component was free in AxiSim"
				" and is pinned to 0 here";
		}

		return patch;
	}

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
	else if (axial.type() == BCType::PULSATILE || radial.type() == BCType::PULSATILE) {
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

// Everything the 0/ writer needs to know about one patch: whether it is a
// constraint type, and which boundary group its conditions come from. A
// blockMeshDict patch and a polyMesh patch differ only in how their FACES are
// spelled, and there are no faces in 0/ -- so both mesh paths reduce to this and
// share every line below it.
struct PatchRef {
	BoundaryFOAMType type = BoundaryFOAMType::PATCH;
	int groupID = -1;
};

std::vector<FoamField> initialFields(
	const std::map<std::string, PatchRef>& patches,
	const std::vector<BoundarySegmentGroup>& groups,
	const FoamCaseSetup& setup
) {

	// Starting a scalar at 0 is plain wrong for temperature (0 K) and needlessly slow
	// for concentration, so seed the interior from the inlet value where there is one.
	auto inletValue = [&groups](BoundaryVariable variable) {

		for (const BoundarySegmentGroup& group : groups) {

			if (group.type != BoundaryType::VELOCITY_INLET) continue;

			const auto found = group.bcs.find(variable);
			if (found != group.bcs.end() && found->second.type() == BCType::DIRICHLET)
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
			.name = kConcentrationField,
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
	for (const auto& entry : patches) {

		const std::string& patchName = entry.first;
		const PatchRef& patch = entry.second;

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
				// The "unassigned" fallback both mesh paths fall back on, or a group
				// that vanished between the two calls. Either way there is nothing to
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

}

std::vector<FoamField> initialFieldsFromDict(
	const BlockMeshDict& dict,
	const std::vector<BoundarySegmentGroup>& groups,
	const FoamCaseSetup& setup
) {

	std::map<std::string, PatchRef> patches;
	for (const auto& entry : dict.boundary) {
		patches[entry.first] = { entry.second.type, entry.second.groupID };
	}

	return initialFields(patches, groups, setup);
}

std::vector<FoamField> initialFieldsFromPolyMesh(
	const PolyMesh& mesh,
	const std::vector<BoundarySegmentGroup>& groups,
	const FoamCaseSetup& setup
) {

	std::map<std::string, PatchRef> patches;
	for (const FoamPolyPatch& patch : mesh.patches) {
		patches[patch.name] = { patch.type, patch.groupID };
	}

	return initialFields(patches, groups, setup);
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
