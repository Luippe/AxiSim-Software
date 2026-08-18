#pragma once

#include <vector>

namespace AxiMesh{
	struct Point {
		double x = 0.0;
		double y = 0.0;
	};

	struct Triangle {
		int v[3];
		int adj[3];
	};

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

	enum class SegmentState {
		Exists,
		Crossing,
		Degenerate,
		Unresolved
	};

	struct Mesh {
		// the bin sort reorders the input, so points is not in caller order; the three
		// super triangle vertices follow the `size` input points
		std::vector<Point> points;
		std::vector<Triangle> triangles;
		std::vector<Segment> segments;		// remapped to the reordered point indices
	};

	Mesh generateMesh(
		const std::vector<Point>& points,
		const std::vector<Segment>& segments,
		int size
	);


	// Steps 3 and 5-7 on their own: build the super triangle, then insert each point into
	// the triangle containing it and swap back to a Delaunay triangulation. Public so
	// aximesh_cli can test them with a hand-built point list, skipping the normalize and
	// bin-sort passes that would reorder the insertions.
	// `points` holds the `size` input points; the super triangle vertices are appended.
	std::vector<Triangle> insertPoints(std::vector<Point>& points, int size);



}
