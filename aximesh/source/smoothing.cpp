#include "aximesh/smoothing.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace AxiMesh {

	// defined in aximesh.cpp -- unqualified calls below reach them through the
	// enclosing namespace
	void throwError(ErrorCase e);
	double dist2(const Point& a, const Point& b);
	double dist(const Point& a, const Point& b);
	double orient(const Point& A, const Point& B, const Point& P);
	bool isBoundary(int tIndex);
	int getEdgeFromNeighbour(const Triangle& tri, int tIndex);
	void flipEdge(std::vector<Triangle>& triangles, int t, int e);

}

namespace AxiMesh::Smoothing {

	Point ringCentroid(
		const std::vector<Point>& points,
		const PointRing& ring
	) {
		Point p;
		int ringSize = (int)ring.neighbors.size();
		for (const int& nb : ring.neighbors) {
			p.x += points[nb].x;
			p.y += points[nb].y;
		}
		p.x = p.x / ringSize;
		p.y = p.y / ringSize;
		return p;
	}

	// 1 = equilateral
	double triangleQuality(
		const Point& a,
		const Point& b,
		const Point& c
	) {
		double l2 = dist2(a, b) + dist2(b, c) + dist2(c, a);
		return 2.0 * std::sqrt(3.0) * orient(a, b, c) / l2;
	}

	double fanQuality(
		const std::vector<Point>& points,
		const std::vector<Triangle>& triangles,
		const PointRing& ring,
		int vi,
		const Point& candidate
	) {
		double q = std::numeric_limits<double>::max();
		for (int t : ring.tris) {
			const Triangle& T = triangles[t];
			Point p[3];
			for (int e = 0; e < 3; e++) {
				p[e] = (T.v[e] == vi) ? candidate : points[T.v[e]];
			}
			q = std::min(q, triangleQuality(p[0], p[1], p[2]));
		}
		return q;
	}

	std::vector<uint8_t> buildPinnedPoints(
		const std::vector<Segment>& segments,
		int size
	) {
		std::vector<uint8_t> pinned(size, 0);
		for (const Segment& seg : segments) {
			pinned[seg.a] = 1;
			pinned[seg.b] = 1;
		}
		return pinned;
	}

	std::vector<PointRing> buildPointRings(
		const std::vector<Point>& points,
		const std::vector<Triangle>& triangles,
		const std::vector<uint8_t>& pinned
	) {
		// if the point is not on the boundary, push
		std::vector<PointRing> pRings(points.size());
		for (const Triangle& triangle : triangles) {
			for (int e = 0; e < 3; e++) {
				int a = triangle.v[e];
				int b = triangle.v[(e + 1) % 3];
				if (!pinned[a]) pRings[a].neighbors.push_back(b);
				if (!pinned[b]) pRings[b].neighbors.push_back(a);
			}
		}

		// remove duplicates
		for (PointRing& ring : pRings) {
			std::sort(ring.neighbors.begin(), ring.neighbors.end());
			auto it = std::unique(ring.neighbors.begin(), ring.neighbors.end());
			ring.neighbors.erase(it, ring.neighbors.end());
		}

		// add triangle fan
		for (int t = 0; t < (int)triangles.size(); t++) {
			for (int e = 0; e < 3; e++) {
				if (!pinned[triangles[t].v[e]]) pRings[triangles[t].v[e]].tris.push_back(t);
			}
		}
		return pRings;
	}

	SmoothFunc getSmoothingFunction(
		const Params& params
	) {
		switch (params.smoothingScheme) {
		case SmoothingScheme::LAPLACIAN:
			return [](double d) {return -d; };
		case SmoothingScheme::OURS:
			return [](double d) {
				double d4 = d * d * d * d;
				return (1 - d4) * std::exp(-d4);
				};
		default: return [](double) {return 0.0; };
		}
	}

	double normalizedArea(
		const std::vector<Point>& points,
		const Triangle& triangle,
		const std::vector<double>& h
	) {
		double hT = (h[triangle.v[0]] + h[triangle.v[1]] + h[triangle.v[2]]) / 3.0;
		double area = 0.5 * orient(points[triangle.v[0]], points[triangle.v[1]], points[triangle.v[2]]);
		return area / (hT * hT);
	}

	double getPointExtent(
		const std::vector<Point>& points,
		const std::vector<Triangle>& triangles,
		const std::vector<double>& h,
		const PointRing& ring,
		const Params& params
	) {
		double extent = 0.0;
		int size = (int)ring.tris.size();
		for (const int& t : ring.tris) {
			const Triangle& triangle = triangles[t];
			extent += normalizedArea(points, triangle, h);
		}
		return (2.3 * extent) / (std::sqrt(size * (size - 2)));
	}

	double getEdgeExtent(
		const std::vector<Point>& points,
		const std::vector<Triangle>& triangles,
		const std::vector<double>& h,
		int t,
		int e
	) {
		const Triangle& triangle = triangles[t];
		int tOpp = triangle.adj[e];
		if (tOpp == -1) {
			int a = triangle.v[e];
			int b = triangle.v[(e + 1) % 3];
			double r = 0.5 * (h[a] + h[b]);
			double len = dist(points[a], points[b]) / r;
			return 0.5 * len * len;
		}
		return 0.8 * (normalizedArea(points, triangle, h) + normalizedArea(points, triangles[tOpp], h));
	}

	Point generalSmoothing(
		const std::vector<Point>& points,
		const std::vector<Triangle>& triangles,
		const std::vector<double>& h,
		const PointRing& ring,
		const Params& params,
		SmoothFunc func,
		int nP
	) {
		Point p;
		int ringSize = (int)ring.neighbors.size();
		double sumX = 0.0;
		double sumY = 0.0;

		for (const int& nb : ring.neighbors) {
			double r = 0.5 * (h[nP] + h[nb]);
			double d = dist(points[nP], points[nb]) / r;
			double nx = (points[nP].x - points[nb].x) / d;
			double ny = (points[nP].y - points[nb].y) / d;
			double fd = func(d);
			sumX += fd * nx;
			sumY += fd * ny;
		}
		p.x = points[nP].x + (sumX / ringSize);
		p.y = points[nP].y + (sumY / ringSize);
		return p;
	}


	// when a triangle is deleted, ensure the adjacency is held between the two triangles that are affected
	// takes t, the triangle to be removed, and e, the edge which will collapse
	void preserveTriangleAdjacency(
		std::vector<Triangle>& triangles,
		int t,
		int e
	) {
		int tAdj1 = triangles[t].adj[(e + 1) % 3];
		int tAdj2 = triangles[t].adj[(e + 2) % 3];
		if (tAdj1 != -1) {
			int eOpp1 = getEdgeFromNeighbour(triangles[tAdj1], t);
			triangles[tAdj1].adj[eOpp1] = tAdj2;
		}
		if (tAdj2 != -1) {
			int eOpp2 = getEdgeFromNeighbour(triangles[tAdj2], t);
			triangles[tAdj2].adj[eOpp2] = tAdj1;
		}
	}

	bool collapseEdge(
		std::vector<Point>& points,
		std::vector<Triangle>& triangles,
		std::vector<uint8_t>& dead,
		const PointRing& ring,
		int np,
		int nb,
		int boundary
	) {
		// find the two triangles that are adjacent to edge np-nb. also find the corresponding edge
		int tA = -1;
		int tB = -1;
		int eA = -1;
		int eB = -1;
		for (int t : ring.tris) {
			if (dead[t]) continue;
			Triangle& triangle = triangles[t];
			for (int e = 0; e < 3; e++) {
				if (triangle.v[e] == np && triangle.v[(e + 1) % 3] == nb) {
					tA = t;
					eA = e;
				}
				else if (triangle.v[e] == nb && triangle.v[(e + 1) % 3] == np) {
					tB = t;
					eB = e;
				}
			}
		}

		// an earlier collapse in this sweep may already have taken the edge
		if (tA == -1 || tB == -1) return false;

		// removing a triangle glues its two surviving edges together
		preserveTriangleAdjacency(triangles, tA, eA);
		preserveTriangleAdjacency(triangles, tB, eB);


		if (!boundary) {
			// calculate the midpoint between np and nb
			Point M = { 0.5 * (points[np].x + points[nb].x), 0.5 * (points[np].y + points[nb].y) };

			// nb becomes the new midpoint
			points[nb] = M;
		}

		// np disappears into nb across its whole fan
		for (int t : ring.tris) {
			if (dead[t]) continue;
			Triangle& triangle = triangles[t];
			for (int e = 0; e < 3; e++) {
				if (triangle.v[e] == np) triangle.v[e] = nb;
			}
		}

		dead[tA] = 1;
		dead[tB] = 1;
		return true;
	}

	// drop dead triangles and remap adjacency -- same remap as removeFloodFill
	void removeDeadTriangles(
		std::vector<Triangle>& triangles,
		const std::vector<uint8_t>& dead
	) {
		int triSize = (int)triangles.size();
		std::vector<int> newIndices(triSize, -1);
		int keep = 0;
		for (int t = 0; t < triSize; t++) {
			if (dead[t]) continue;
			newIndices[t] = keep++;
		}

		for (int t = 0; t < triSize; t++) {
			if (newIndices[t] == -1) continue;
			Triangle triangle = triangles[t];
			for (int i = 0; i < 3; i++) {
				int nb = triangle.adj[i];
				if (isBoundary(nb)) continue;
				// -1 here would read as a domain boundary and quietly punch a hole in the mesh
				if (newIndices[nb] == -1) throwError(ErrorCase::DEAD_NEIGHBOR);
				triangle.adj[i] = newIndices[nb];
			}
			triangles[newIndices[t]] = triangle;
		}
		triangles.resize(keep);
	}

	// drop points no live triangle still references, and renumber everything that names a point
	void removeDeadPoints(
		std::vector<Point>& points,
		std::vector<Triangle>& triangles,
		std::vector<Segment>& segments,
		std::vector<double>& h,
		std::vector<uint8_t>& pinned
	) {
		int size = (int)points.size();
		std::vector<uint8_t> used(size, 0);
		for (const Triangle& triangle : triangles) {
			for (int i = 0; i < 3; i++) used[triangle.v[i]] = 1;
		}

		std::vector<int> newIndices(size, -1);
		int keep = 0;
		for (int p = 0; p < size; p++) {
			if (!used[p]) continue;
			newIndices[p] = keep;
			points[keep] = points[p];
			h[keep] = h[p];
			pinned[keep] = pinned[p];
			keep++;
		}
		if (keep == size) return;

		points.resize(keep);
		h.resize(keep);
		pinned.resize(keep);

		for (Triangle& triangle : triangles) {
			for (int i = 0; i < 3; i++) triangle.v[i] = newIndices[triangle.v[i]];
		}

		// a segment endpoint is pinned and pinned points never collapse, so this only renumbers
		for (Segment& seg : segments) {
			if (newIndices[seg.a] == -1 || newIndices[seg.b] == -1) throwError(ErrorCase::DEAD_SEGMENT);
			seg.a = newIndices[seg.a];
			seg.b = newIndices[seg.b];
		}
	}

	void smartEdgeCollapse(
		std::vector<Point>& points,
		std::vector<Triangle>& triangles,
		std::vector<Segment>& segments,
		const std::vector<PointRing>& pRings,
		std::vector<double>& h,
		std::vector<uint8_t>& pinned,
		const Params& params
	) {
		std::vector<uint8_t> dead(triangles.size(), 0);
		std::vector<uint8_t> gone(points.size(), 0);	// vertices an earlier collapse absorbed
		bool collapsed = false;

		for (int i = 0; i < (int)pRings.size(); i++) {
			if (pRings[i].neighbors.empty() || gone[i]) continue;
			std::vector<std::pair<uint8_t, int>> candidates;	// <is it a boundary?, neighbor index>
			// get candidate neighbors
			for (int nb : pRings[i].neighbors) {
				double target = 0.5 * (h[i] + h[nb]);
				double len = dist(points[i], points[nb]);

				if (len < (4 * target / 5)) {
					candidates.push_back({ pinned[nb], nb });
				};
			}


			for (auto& [boundary, nb] : candidates) {
				if (gone[nb]) continue;
				if (!collapseEdge(points, triangles, dead, pRings[i], i, nb, boundary)) continue;
				gone[i] = 1;
				gone[nb] = 1;		// nb inherited i's fan, so its cached ring is stale for the rest of the sweep
				collapsed = true;
				break;		// i is absorbed into nb, so its remaining candidates no longer exist
			}
		}

		if (!collapsed) return;

		// deferred so the sweep above can keep using its cached triangle and point indices
		removeDeadTriangles(triangles, dead);
		removeDeadPoints(points, triangles, segments, h, pinned);
	}

	void smartEdgeFlip(
		std::vector<Point>& points,
		std::vector<Triangle>& triangles
	) {
		for (int t = 0; t < (int)triangles.size(); t++) {
			for (int e = 0; e < 3; e++) {
				// for interior edge (t,e), quad in CCW order is: v2, v0, vOpp, v1
				int tOpp = triangles[t].adj[e];
				if (tOpp == -1) continue;                 // boundary
				int eOpp = getEdgeFromNeighbour(triangles[tOpp], t);

				int v0 = triangles[t].v[e];
				int v1 = triangles[t].v[(e + 1) % 3];
				int v2 = triangles[t].v[(e + 2) % 3];
				int vOpp = triangles[tOpp].v[(eOpp + 2) % 3];

				double before = std::min(triangleQuality(points[v0], points[v1], points[v2]),
										triangleQuality(points[v1], points[v0], points[vOpp]));
				double after = std::min(triangleQuality(points[v2], points[v0], points[vOpp]),
										triangleQuality(points[v2], points[vOpp], points[v1]));

				if (after > before) flipEdge(triangles, t, e);
			}
		}
	}

	void smartSmoothing(
		std::vector<Point>& points,
		std::vector<Triangle>& triangles,
		std::vector<PointRing>& pRings,
		std::vector<double>& h,
		const Params& params
	) {
		for (int i = 0; i < (int)points.size(); i++) {
			const PointRing& ring = pRings[i];
			if (ring.neighbors.empty() || ring.tris.empty()) continue;

			SmoothFunc func = getSmoothingFunction(params);
			Point target = generalSmoothing(points, triangles, h, ring, params, func, i);

			// only move if the worst triangle in the fan improves
			// points[i] = target;
			if (fanQuality(points, triangles, ring, i, target) >
				fanQuality(points, triangles, ring, i, points[i])) {
				points[i] = target;
			}
		}
	}

	void postSmoothing(
		std::vector<Point>& points,
		std::vector<Triangle>& triangles,
		std::vector<Segment>& segments,
		std::vector<PointRing>& pRings,
		std::vector<double>& h,
		std::vector<uint8_t>& pinned,
		const Params& params
	) {

		if ((int)points.size() != (int)pRings.size()) throwError(ErrorCase::SIZE_DIFF);

		// flip any edges -> rebuild pRings -> smoothing
		for (int k = 0; k < params.iterSmoothing; k++) {
			// collapse compacts points, so pRings is stale until it is rebuilt below
			smartEdgeCollapse(points, triangles, segments, pRings, h, pinned, params);

			smartEdgeFlip(points, triangles);
			pRings = buildPointRings(points, triangles, pinned);
			if (params.enableSmoothing) smartSmoothing(points, triangles, pRings, h, params);
			else return;

		}
	}

}
