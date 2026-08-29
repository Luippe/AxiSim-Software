#include "aximesh/quality.h"

#include <cmath>
#include <limits>

namespace AxiMesh {

	// defined in aximesh.cpp -- unqualified calls below reach them through the
	// enclosing namespace
	double dist2(const Point& a, const Point& b);
	double dist(const Point& a, const Point& b);
	double orient(const Point& A, const Point& B, const Point& P);

}

namespace AxiMesh::Quality {

	// 1 = equilateral
	double triangleQuality(
		const Point& a,
		const Point& b,
		const Point& c
	) {
		double l2 = dist2(a, b) + dist2(b, c) + dist2(c, a);
		return 2.0 * std::sqrt(3.0) * orient(a, b, c) / l2;
	}

	// 1 = equilateral, growing without bound as the triangle degenerates
	double triangleAspectRatio(
		const Point& a,
		const Point& b,
		const Point& c
	) {
		double l0 = dist(a, b);
		double l1 = dist(b, c);
		double l2 = dist(c, a);

		// orient is 2A, so 4*orient^2 = 16A^2 -- the denominator of R/(2r)
		double d = orient(a, b, c);
		double denom = 4.0 * d * d;

		return l0 * l1 * l2 * (l0 + l1 + l2) / denom;
	}

	void trianglePlaneAngle(
		std::vector<double>& planeAngle,
		const Point& a,
		const Point& b,
		const Point& c,
		int t
	) {
		const double cross = std::abs(orient(a, b, c));
		planeAngle[3 * t] = std::atan2(cross, (b.x - a.x) * (c.x - a.x) + (b.y - a.y) * (c.y - a.y));
		planeAngle[3 * t + 1] = std::atan2(cross, (c.x - b.x) * (a.x - b.x) + (c.y - b.y) * (a.y - b.y));
		planeAngle[3 * t + 2] = std::atan2(cross, (a.x - c.x) * (b.x - c.x) + (a.y - c.y) * (b.y - c.y));
	}

	void buildQuality(Mesh& mesh) {
		const std::vector<Triangle>& triangles = mesh.triangles;
		const std::vector<Point>& points = mesh.points;

		mesh.elementQuality.assign(triangles.size(), 0.0);
		mesh.aspectRatio.assign(triangles.size(), 0.0);
		mesh.planeAngle.assign(3 * triangles.size(), 0.0);

		for (int t = 0; t < (int)triangles.size(); t++) {
			const Triangle& tri = triangles[t];

			const Point& a = points[tri.v[0]];
			const Point& b = points[tri.v[1]];
			const Point& c = points[tri.v[2]];

			mesh.elementQuality[t] = triangleQuality(a, b, c);
			mesh.aspectRatio[t] = triangleAspectRatio(a, b, c);
			trianglePlaneAngle(mesh.planeAngle, a, b, c, t);
		}
	}

}
