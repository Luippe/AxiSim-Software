#pragma once

#include <queue>
#include <vector>
#include <set>
namespace AxiMesh{

	enum class InsertionScheme {
		REBAY,
		ENGWIRDA
	};

	enum class SmoothingScheme {
		NONE,
		CENTROID,
		LAPLACIAN,
		OURS,
	};

	enum class ErrorCase {
		OK,
		STALLED,
		WALK_OFF,
		DUPLICATE,
		ON_EDGE,
		SIZE_DIFF,
		COMPLEX_SQRT

	};

	struct Point {
		double x = 0.0;
		double y = 0.0;
	};

	struct Triangle {
		int v[3];
		int adj[3];
	};

	struct FrontEdge {
		int t = -1;
		int e = -1;
		double len = 0.0;
		double crad = 0.0;
		int v0;
		int v1;
		int v2;
	};

	struct PointRing {
		std::vector<int> neighbors;
		std::vector<int> tris;
	};

	// max-heap on crad -- Rebay advances from the active triangle with the largest circumradius
	struct FrontEdgeCompare {
		bool operator()(const FrontEdge& a, const FrontEdge& b) const { return a.crad < b.crad; }
	};

	using FrontQueue = std::priority_queue<FrontEdge, std::vector<FrontEdge>, FrontEdgeCompare>;

	// an edge that must survive into the triangulation. Segments never introduce points --
	// a and b are indices of points that already exist in px/py.
	struct Segment {
		int a;
		int b;
	};

	struct NormVariables {
		double pxMin = 0;
		double pyMin = 0;
		double dMax = 0;
		double dx = 0;
		double dy = 0;
	};

	struct SizeField {
		int nz = 100;
		int nr = 100;
		double dz = 0.0;
		double dr = 0.0;
		std::vector<double> h;
		double sample(const Point& p) const;
	};

	struct Params {
		double B = 1.0;
		double gSmoothing = 0.3;
		double classify_tol = 1.2247;
		InsertionScheme scheme = InsertionScheme::REBAY;
		//InsertionScheme scheme = InsertionScheme::ENGWIRDA;


		// smoothing variables
		bool enableSmoothing = true;
		SmoothingScheme smoothingScheme = SmoothingScheme::LAPLACIAN;
		//SmoothingScheme smoothingScheme = SmoothingScheme::OURS;
		int iterSmoothing = 10;
		double hMax = 1.0;
		double hMin = 0.0;
		int stampCells = 2;
	};

	enum class SegmentState {
		Exists,
		Crossing,
		Degenerate,
		Unresolved
	};

	enum class AdvancingState : uint8_t{
		ACCEPTED,
		ACTIVE,
		WAITING
	};

	struct Mesh {
		// the bin sort reorders the input, so points is not in caller order; the three
		// super triangle vertices follow the `size` input points
		std::vector<Point> points;
		std::vector<Triangle> triangles;
		std::vector<Segment> segments;		// remapped to the reordered point indices

		// target edge length per point, in the NORMALIZED coordinates the mesher works
		// in -- points are unnormalized on the way out, this is not. Scale by
		// buildNormVariables(<the same input points>).dMax for a world length. A point
		// the field never reached (the super triangle corners) keeps DBL_MAX.
		std::vector<double> sizing;
		std::vector<PointRing> pRings;
	};

	Mesh generateMesh(
		const std::vector<Point>& points,
		const std::vector<Segment>& segments,
		int size,
		const Params& params = {}
	);

	// The affine map generateMesh normalizes with. Public so a caller can undo it on
	// Mesh::sizing, which comes back in normalized units -- pass the same point list
	// that was handed to generateMesh.
	NormVariables buildNormVariables(const std::vector<Point>& points);


	// Steps 3 and 5-7 on their own: build the super triangle, then insert each point into
	// the triangle containing it and swap back to a Delaunay triangulation. Public so
	// aximesh_cli can test them with a hand-built point list, skipping the normalize and
	// bin-sort passes that would reorder the insertions.
	// `points` holds the `size` input points; the super triangle vertices are appended.
	std::vector<Triangle> insertPoints(std::vector<Point>& points, int size);


}
