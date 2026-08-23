#include "aximesh/aximesh.h"

#include <algorithm>
#include <array>
#include <stack>
#include <utility>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <queue>

//#include <format>

namespace AxiMesh {

	std::string errorTypeToMessage(ErrorCase e) {
		switch (e) {
		case ErrorCase::STALLED:
			return "point search has stalled";
		case ErrorCase::WALK_OFF:
			return "point is out of bounds and search has walked off";

		}
	}

	void throwError(ErrorCase e) {
		throw std::runtime_error(errorTypeToMessage(e));
	}

	struct ScopedTimer {
		std::chrono::steady_clock::time_point t0;
		void start() {
			t0 = std::chrono::steady_clock::now();
		}

		void end() {
			std::chrono::steady_clock::time_point t1 = std::chrono::steady_clock::now();
			printf("%.3f ms\n", std::chrono::duration<double, std::milli>(
				std::chrono::steady_clock::now() - t0).count());
		}
	};

	// distance squared
	double dist2(const Point& a, const Point& b) {
		return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y);
	}

	double orient(const Point& A, const Point& B, const Point& P) {
		return (B.x - A.x) * (P.y - A.y) - (B.y - A.y) * (P.x - A.x);
	}

	double circumradius2(const std::vector<Point>& points, const Triangle& triangle) {
		const Point& a = points[triangle.v[0]];
		const Point& b = points[triangle.v[1]];
		const Point& c = points[triangle.v[2]];
		double quadArea = orient(a, b, c);
		return (dist2(a, b) * dist2(b, c) * dist2(c, a)) / (4.0 * quadArea * quadArea);
	}

	bool isPointInTriangle(const Triangle& T, const Point& P, const std::vector<Point>& points) {
		for (int nE = 0; nE < 3; nE++) {

			int indexA = T.v[nE];
			int indexB = T.v[(nE + 1) % 3];
			const Point& A = points[indexA];
			const Point& B = points[indexB];

			double d = orient(A, B, P);
			const bool isPointInside = d >= 0;
			if (!isPointInside) {
				return false;
			};
		}
		return true;
	}

	bool swapTest(const std::vector<Point>& points, const Point& P, const Triangle& T, int edge) {

		// use Cline and Renka's method to check if a flip needs to occur
		const Point& p0 = points[T.v[edge]];
		const Point& p1 = points[T.v[(edge + 1) % 3]];
		const Point& p2 = points[T.v[(edge + 2) % 3]];

		double x13 = p0.x - p2.x;
		double x23 = p1.x - p2.x;
		double x1p = p0.x - P.x;
		double x2p = p1.x - P.x;

		double y13 = p0.y - p2.y;
		double y23 = p1.y - p2.y;
		double y1p = p0.y - P.y;
		double y2p = p1.y - P.y;

		double cosA = x13 * x23 + y13 * y23;
		double cosB = x2p * x1p + y2p * y1p;


		if (cosA >= 0 && cosB >= 0) return false;
		if (cosA < 0 && cosB < 0) return true;

		double sinAB = (x13 * y23 - x23 * y13) * cosB + (x2p * y1p - x1p * y2p) * cosA;

		if (sinAB < 0) return true;

		return false;
	}


	std::stack<int> buildInitialStack(int t0, int t1, int t2) {
		std::stack<int> tStack;
		if (t0 != -1) tStack.push(t0);
		if (t1 != -1) tStack.push(t1);
		if (t2 != -1) tStack.push(t2);
		return tStack;
	}

	bool isBoundary(int tIndex) {
		return tIndex == -1;
	}

	Point getMidpointFromPoints(const Point& a, const Point& b) {
		return Point{ 0.5 * (a.x + b.x), 0.5 * (a.y + b.y) };
	}

	int getEdgeFromVertex(const Triangle& T, int vi) {
		for (int i = 0; i < 3; i++) {
			if (T.v[i] == vi) return i;
		}
		return -1;
	}

	void updateEdge(Triangle& T, int check, int replace) {
		for (int i = 0; i < 3; i++) {
			if (T.adj[i] == check) {
				T.adj[i] = replace;
				break;
			}
		}
	}

	double dotThreePoint(const Point& A, const Point& B, const Point& C) {
		double dxAC = A.x - C.x;
		double dyAC = A.y - C.y;
		double dxBC = B.x - C.x;
		double dyBC = B.y - C.y;
		return dxAC * dxBC + dyAC * dyBC;
	}

	bool activeTest(
		const std::vector<Triangle>& triangles,
		std::vector<AdvancingState>& state,
		int t
	) {
		if (state[t] == AdvancingState::ACCEPTED) return false;
		for (int e = 0; e < 3; e++) {
			int tAdj = triangles[t].adj[e];
			if (tAdj == -1 || state[tAdj] == AdvancingState::ACCEPTED) {
				state[t] = AdvancingState::ACTIVE;
				break;
			}
		}
		return state[t] == AdvancingState::ACTIVE;
	}

	void acceptTest(
		const std::vector<Point>& points,
		const std::vector<Triangle>& triangles,
		const std::vector<double>& h,
		std::vector<AdvancingState>& state,
		const Params& params,
		int t
	) {
		const Triangle& triangle = triangles[t];
		double hT = (h[triangle.v[0]] + h[triangle.v[1]] + h[triangle.v[2]]) / 3.0;
		double target = params.classify_tol * hT;

		state[t] = (3.0 * circumradius2(points, triangle) <= target * target) ? AdvancingState::ACCEPTED : AdvancingState::WAITING;
	}


	// ensure the frontal edge is valid based on the current triangle layout
	bool isValidFrontEdge(
		const std::vector<Triangle>& triangles,
		const std::vector<AdvancingState>& state,
		const FrontEdge& frontEdge
	) {
		int e = frontEdge.e;
		const Triangle& triangle = triangles[frontEdge.t];

		if (triangle.v[e] != frontEdge.v0)						return false;
		if (triangle.v[(e + 1) % 3] != frontEdge.v1)			return false;
		if (triangle.v[(e + 2) % 3] != frontEdge.v2)			return false;
		if (state[frontEdge.t] == AdvancingState::ACCEPTED)		return false;
		int nb = triangle.adj[e];
		if (nb != -1 && state[nb] != AdvancingState::ACCEPTED)	return false;

		return true;
	}

	void pushValidFrontEdge(
		const std::vector<Point>& points,
		const std::vector<Triangle>& triangles,
		const std::vector<AdvancingState>& state,
		FrontQueue& frontEdges,
		int t
	) {
		const Triangle& triangle = triangles[t];
		for (int e = 0; e < 3; e++) {
			if (state[t] != AdvancingState::ACCEPTED && (triangle.adj[e] == -1 || state[triangle.adj[e]] == AdvancingState::ACCEPTED)) {
				frontEdges.push({
					t,
					e,
					std::sqrt(dist2(points[triangle.v[e]], points[triangle.v[(e + 1) % 3]])),
					std::sqrt(circumradius2(points, triangle)),
					triangle.v[e],
					triangle.v[(e + 1) % 3],
					triangle.v[(e + 2) % 3]
					});
			}
		}
	}


	// loop/fan around vertex a, until vertex b is found
	bool findEdge(const std::vector<int>& vertexTri, const std::vector<Triangle>& triangles, int a, int b, int& tOut, int& eOut) {
		int tStart = vertexTri[a];
		int t = tStart;
		assert(tStart != -1);
		do {
			int edge = getEdgeFromVertex(triangles[t], a);
			if (edge == -1) return false;

			if (triangles[t].v[(edge + 1) % 3] == b) {
				tOut = t;
				eOut = edge;
				return true;
			}
			t = triangles[t].adj[edge];

		} while (t != tStart && t != -1);
		return false;
	}

	void flipEdge(std::vector<Triangle>& triangles, int t, int e) {

		// get opposite triangle and shared edge index
		int tOpp = triangles[t].adj[e];
		assert(tOpp != -1);

		int eOpp = -1;
		for (int i = 0; i < 3; i++) {
			if (triangles[tOpp].adj[i] == t) {
				eOpp = i;
				break;
			}
		}
		assert(eOpp != -1);


		Triangle& tOuter = triangles[t];
		Triangle& tInner = triangles[tOpp];

		int vOpp = tInner.v[(eOpp + 2) % 3];

		int v0 = tOuter.v[e];
		int v1 = tOuter.v[(e + 1) % 3];
		int v2 = tOuter.v[(e + 2) % 3];

		int tOuter1New = tOuter.adj[(e + 1) % 3];
		int tOuter2New = tOuter.adj[(e + 2) % 3];

		int tInner1New = tInner.adj[(eOpp + 1) % 3];
		int tInner2New = tInner.adj[(eOpp + 2) % 3];

		triangles[tOpp] = { {v1, v2, vOpp}, {tOuter1New, t, tInner2New} };
		triangles[t] = { {v2, v0, vOpp}, {tOuter2New, tInner1New, tOpp} };

		// there are two edges that needs to be corrected
		if (!isBoundary(tInner1New)) {
			updateEdge(triangles[tInner1New], tOpp, t);
		}

		if (!isBoundary(tOuter1New)) {
			updateEdge(triangles[tOuter1New], t, tOpp);
		}

	}

	// find circumcenter of three points using Cramer's rule
	Point getCircumcenter(const Point& a, const Point& b, const Point& c) {

		double bx = b.x - a.x;
		double by = b.y - a.y;
		double cx = c.x - a.x;
		double cy = c.y - a.y;

		double b2 = bx * bx + by * by;
		double c2 = cx * cx + cy * cy;
		double d = 2.0 * orient(a, b, c);
		return { a.x + ((cy * b2 - by * c2) / d),
				 a.y + ((bx * c2 - cx * b2) / d) };
	}


	// a triangle is skinny if it satisfies: r^2 > (B * dmin)^2
	// r is circumradius, dmin is smallest triangle side length, B is defined by user
	// square everything to avoid having to sqrt. orient can give us negative, so squaring will give us positive
	// get circumradius squared using (A * B * C)^2 / (16 * area * area) where A, B, C are side lengths of the triangle
	bool isSkinny(
		const Triangle& triangle,
		const std::vector<Point>& points,
		const Params& params
	) {
		const Point& a = points[triangle.v[0]];
		const Point& b = points[triangle.v[1]];
		const Point& c = points[triangle.v[2]];

		double ab = dist2(a, b);
		double bc = dist2(b, c);
		double ca = dist2(c, a);

		double dmin2 = std::min({ ab, bc, ca });

		return circumradius2(points, triangle) > params.B * params.B * dmin2;
	}

	// check every edge and see if any are tIndex, if so return that edge index
	int getEdgeFromNeighbour(const Triangle& tri, int tIndex) {
		for (int i = 0; i < 3; i++) {
			if (tri.adj[i] == tIndex) return i;
		}
		return -1;
	}

	int locateTriangle(
		const std::vector<Point>& points,
		const std::vector<Triangle>& triangles,
		const Point& P,
		int tStart
	) {
		int nT = tStart;
		for (int check = 0; ; check++) {
			if (nT == -1)						return -1;
			if (check > (int)triangles.size())	throwError(ErrorCase::STALLED);
			const Triangle& T = triangles[nT];
			bool isPointInside = true;
			for (int nE = 0; nE < 3; nE++) {

				int indexA = T.v[nE];
				int indexB = T.v[(nE + 1) % 3];

				double d = orient(points[indexA], points[indexB], P);
				if (d < 0) {
					nT = T.adj[nE];
					isPointInside = false;
					break;
				}
			}
			if (isPointInside) return nT;
		}
		return -1;
	}

	// ensure points already includes the point you want to insert. just pass nP, the index of that point you inserted
	bool insertVertex(
		std::vector<Point>& points,
		std::vector<Triangle>& triangles,
		std::vector<int>& touched,
		int nP,
		int nT
	) {
		Point& P = points[nP];

		nT = locateTriangle(points, triangles, P, nT);
		if (nT == -1) return false;

		Triangle& T = triangles[nT];
		// 1 triangle is overwritten and 2 new triangles are created everytime a point is inside a triangle
		int v0 = T.v[0];
		int v1 = T.v[1];
		int v2 = T.v[2];

		T.v[2] = nP;

		int t0 = T.adj[0];
		int t1 = T.adj[1];
		int t2 = T.adj[2];

		int t1New = (int)triangles.size();
		int t2New = (int)triangles.size() + 1;



		T.adj[1] = t1New;
		T.adj[2] = t2New;

		triangles.push_back({ {v1, v2, nP}, {t1, t2New, nT} });
		triangles.push_back({ {v2, v0, nP}, {t2, nT, t1New} });

		if (!isBoundary(t1)) {
			updateEdge(triangles[t1], nT, t1New);
		}

		if (!isBoundary(t2)) {
			updateEdge(triangles[t2], nT, t2New);
		}

		touched.push_back(t1New);
		touched.push_back(t2New);
		touched.push_back(nT);

		// step 6: initialize stack. check for any -1 here so we don't have to check inside step 7
		std::stack<int> tStack = buildInitialStack(t0, t1, t2);

		// step 7: check if point P is inside any circumcicle of surrounding triangle
		while (!tStack.empty()) {

			// remove top triangle from stack
			int outer = tStack.top();
			tStack.pop();

			Triangle& tOuter = triangles[outer];

			if (tOuter.v[2] == nP) continue;

			// find shared edge. recall how vertices are stored for a triangle
			// for a new triangle, nP is always the third entry so use that to check for the shared edge
			int edge = -1;
			for (int i = 0; i < 3; i++) {
				int nb = tOuter.adj[i];
				if (nb != -1 && triangles[nb].v[2] == nP) {
					edge = i;
					break;
				}
			}
			if (edge == -1) continue;

			if (!swapTest(points, P, tOuter, edge)) continue;

			// flip the edge. make sure tOuter1New and tOuter2New are defined beforehand, so we can push them to the stack
			int tOpp = tOuter.adj[edge];
			int tOuter1New = tOuter.adj[(edge + 1) % 3];
			int tOuter2New = tOuter.adj[(edge + 2) % 3];

			flipEdge(triangles, outer, edge);
			touched.push_back(outer);

			if (!isBoundary(tOuter1New)) {
				tStack.push(tOuter1New);
			}
			if (!isBoundary(tOuter2New)) {
				tStack.push(tOuter2New);
			}
		}
		return true;
	}

	std::vector<Triangle> insertPoints(std::vector<Point>& points, int size) {

		// step 3: create super triangle
		std::vector<Triangle> triangles;
		triangles.reserve(2 * size + 1);
		triangles.push_back({ { size, size + 1, size + 2 } , {-1, -1, -1} });
		points.push_back({ -100.0, -100.0 });
		points.push_back({ 100.0, -100.0 });
		points.push_back({ 0, 100.0 });

		// step 4: check if point is inside the most recently created triangle
		int nT = 0;
		std::vector<int> touched;
		for (int nP = 0; nP < size; nP++) {
			touched.clear();
			if (!insertVertex(points, triangles, touched, nP, nT)) {
				continue;
			}
			nT = (int)triangles.size() - 1;
		}

		return triangles;
	}

	SegmentState checkSegment(
		const std::vector<int>& vertexTri,
		const std::vector<Point>& points,
		const std::vector<Triangle>& triangles,
		const Segment& segment,
		int& tSol,
		int& eSol
	) {

		// check if segment exists
		int vi = segment.a;
		int vj = segment.b;
		int tStart = vertexTri[vi];
		int tCurrent = tStart;
		do {
			int edge = getEdgeFromVertex(triangles[tCurrent], vi);
			if (edge == -1) return SegmentState::Unresolved;

			// check if vj lands exactly on a point. that means the segment does already exist
			int vA = triangles[tCurrent].v[(edge + 1) % 3];
			int vB = triangles[tCurrent].v[(edge + 2) % 3];
			if (vA == vj) {
				return SegmentState::Exists;
			}

			// check if vi-vj crosses edge AB. that means the segment does not exist
			double viA = orient(points[vi], points[vA], points[vj]);
			double viB = orient(points[vi], points[vB], points[vj]);
			if (viA > 0 && viB < 0) {
				tSol = tCurrent;
				eSol = (edge + 1) % 3;
				return SegmentState::Crossing;
			}
			else if (viA == 0 && dotThreePoint(points[vj], points[vA], points[vi]) > 0) { // degenerate case where vi-vj is co-linear with vi-A
				tSol = tCurrent;
				eSol = edge;
				return SegmentState::Degenerate;
			}
			tCurrent = triangles[tCurrent].adj[edge];

		} while (tStart != tCurrent);

		return SegmentState::Unresolved;

	}

	SegmentState handleCrossing(
		const std::vector<Point>& points,
		const std::vector<Triangle>& triangles,
		const Segment& segment,
		std::vector<Segment>& segCross,
		int tSol,
		int eSol
	) {
		int vi = segment.a;
		int vj = segment.b;

		int e = eSol;
		int t = tSol;
		int eIn = e;
		int vA = triangles[t].v[e];
		int vB = triangles[t].v[(e + 1) % 3];


		segCross.clear();
		segCross.push_back({ vA, vB });

		// marching towards vj
		while (true) {

			int tNext = triangles[t].adj[e];
			for (int nE = 0; nE < 3; nE++) {
				if (triangles[tNext].adj[nE] == t) {
					eIn = nE;
					break;
				}
			}

			int vC = triangles[tNext].v[(eIn + 2) % 3];
			if (vC == vj) return SegmentState::Crossing;

			double viC = orient(points[vi], points[vC], points[vj]);
			if (viC == 0) {
				return SegmentState::Degenerate;
			}
			else if (viC < 0) {	// left
				t = tNext;
				e = (eIn + 1) % 3;
				int vB = triangles[tNext].v[(eIn + 1) % 3];
				segCross.push_back({ vB, vC });
			}
			else if (viC > 0) {	// right
				t = tNext;
				e = (eIn + 2) % 3;
				int vA = triangles[tNext].v[eIn];
				segCross.push_back({ vC, vA });
			}
		}
	}

	void handleCrossEdge(
		std::vector<int>& vertexTri,        // needed to locate edges, and restamped after each swap
		std::vector<Point>& points,
		std::vector<Triangle>& triangles,   // step 3 mutates -- can't be const
		const Segment& segment,
		std::vector<Segment>& segCross,
		std::vector<Segment>& segNew        // output for step 4
	) {
		int vi = segment.a;
		int vj = segment.b;
		segNew.clear();

		int head = 0;
		while (head != (int)segCross.size()) {

			Segment seg = segCross[head++];

			int t, e;
			if (!findEdge(vertexTri, triangles, seg.a, seg.b, t, e)) {
				printf("SOMETHING IS WRONG IN handleCrossEdge \n");
			};

			// if segment does exist, then get the two triangles from the shared edge
			int tOpp = triangles[t].adj[e];
			assert(tOpp != -1);
			int eOpp = getEdgeFromNeighbour(triangles[tOpp], t);

			// get all vertices
			int vOpp = triangles[tOpp].v[(eOpp + 2) % 3];

			int v0 = triangles[t].v[e];
			int v1 = triangles[t].v[(e + 1) % 3];
			int v2 = triangles[t].v[(e + 2) % 3];

			// check if quad is convex, if so, do a edge flip
			double d0 = orient(points[v2], points[vOpp], points[v0]);
			double d1 = orient(points[v2], points[vOpp], points[v1]);
			if (!(d0 < 0 && d1 > 0)) {
				segCross.push_back(seg);
				continue;
			}

			flipEdge(triangles, t, e);

			// make sure to overwrite vertexTri for both triangles
			for (int i = 0; i < 3; i++) {
				vertexTri[triangles[t].v[i]] = t;
				vertexTri[triangles[tOpp].v[i]] = tOpp;
			}

			// check if the flipped edge still intersects the segment. if so, add it back
			double dA = orient(points[vi], points[v2], points[vj]);
			double dB = orient(points[vi], points[vOpp], points[vj]);
			if (dA > 0 && dB < 0 || dA < 0 && dB > 0) {
				segCross.push_back({ v2, vOpp });
			}
			else if (!((v2 == vi && vOpp == vj) || (v2 == vj && vOpp == vi))) {
				segNew.push_back({ v2, vOpp });
			}
		}
	}

	// mark all triangles that have a constrained edge
	std::vector<uint8_t> buildConstrainedEdge(
		const std::vector<int>& vertexTri,
		const std::vector<Triangle>& triangles,
		const std::vector<Segment>& segments
	) {
		std::vector<uint8_t> constrained(3 * (int)triangles.size(), 0);

		for (const Segment& seg : segments) {
			int t, e;

			// find t and e which corresponds to a triangle adjacent to a given constrained segment
			if (!findEdge(vertexTri, triangles, seg.a, seg.b, t, e)) continue;
			constrained[3 * t + e] = 1;

			// ok we got 1 adjacent triangle, now get the other triangle opposite of the constrained segment
			int tOpp = triangles[t].adj[e];
			if (isBoundary(tOpp)) continue;
			int eOpp = getEdgeFromNeighbour(triangles[tOpp], t);
			constrained[3 * tOpp + eOpp] = 1;
		}
		return constrained;
	}

	void restoreDelaunayFromSegments(
		std::vector<int>& vertexTri,
		const std::vector<Point>& points,
		std::vector<Triangle>& triangles,
		std::vector<Segment>& segNew
	) {
		bool swapped = true;
		while (swapped) {
			swapped = false;
			// for each shared segment, get the two adjacent triangles and do delaunay on them
			for (Segment& seg : segNew) {

				int t, e;
				if (!findEdge(vertexTri, triangles, seg.a, seg.b, t, e)) continue;

				int tOpp = triangles[t].adj[e];
				assert(!isBoundary(tOpp));
				int eOpp = getEdgeFromNeighbour(triangles[tOpp], t);

				int vOpp = triangles[tOpp].v[(eOpp + 2) % 3];
				int v2 = triangles[t].v[(e + 2) % 3];

				if (!swapTest(points, points[vOpp], triangles[t], e)) continue;

				flipEdge(triangles, t, e);

				for (int i = 0; i < 3; i++) {
					vertexTri[triangles[t].v[i]] = t;
					vertexTri[triangles[tOpp].v[i]] = tOpp;
				}

				seg = { v2, vOpp };
				swapped = true;

			}
		}
	}

	void constrain(
		std::vector<int>& vertexTri,
		Mesh& mesh
	) {

		std::vector<Segment> segCross;
		for (const Segment& segment : mesh.segments) {
			int tSol, eSol;
			SegmentState state = checkSegment(vertexTri, mesh.points, mesh.triangles, segment, tSol, eSol);
			std::vector<Segment> segNew;
			switch (state) {
			case SegmentState::Exists:
				continue;
			case SegmentState::Crossing:
				state = handleCrossing(mesh.points, mesh.triangles, segment, segCross, tSol, eSol);
				if (state != SegmentState::Crossing) break;
				handleCrossEdge(vertexTri, mesh.points, mesh.triangles, segment, segCross, segNew);
				restoreDelaunayFromSegments(vertexTri, mesh.points, mesh.triangles, segNew);
				// TEMP scaffolding -- dump the crossing list for hand checking until step 3 lands
				printf("    constraint %d-%d: %s, %d crossing(s)\n", segment.a, segment.b,
					state == SegmentState::Crossing ? "reached vj" : "hit a vertex (degenerate)",
					(int)segCross.size());
				for (const Segment& s : segCross) {
					printf("      %2d (%6.3f, %6.3f) -- %2d (%6.3f, %6.3f)\n",
						s.a, mesh.points[s.a].x, mesh.points[s.a].y,
						s.b, mesh.points[s.b].x, mesh.points[s.b].y);
				}
				break;
			case SegmentState::Degenerate:
				break;
			}
		}
	}

	NormVariables buildNormVariables(
		const std::vector<Point>& points
	) {
		NormVariables normVar;
		auto resultX = std::minmax_element(points.begin(), points.end(),
			[](const Point& a, const Point& b) { return a.x < b.x; });
		auto resultY = std::minmax_element(points.begin(), points.end(),
			[](const Point& a, const Point& b) { return a.y < b.y; });

		normVar.pxMin = resultX.first->x;
		normVar.pyMin = resultY.first->y;

		normVar.dx = resultX.second->x - resultX.first->x;
		normVar.dy = resultY.second->y - resultY.first->y;
		normVar.dMax = std::max(normVar.dx, normVar.dy);

		return normVar;
	}

	std::vector<Point> normalize(
		const std::vector<Point>& points,
		const NormVariables& normVar,
		int size
	) {
		std::vector<Point> normPoints(size);
		for (int n = 0; n < size; n++) {
			normPoints[n].x = (points[n].x - normVar.pxMin) / normVar.dMax;
			normPoints[n].y = (points[n].y - normVar.pyMin) / normVar.dMax;
		}
		return normPoints;
	}

	std::vector<Point> unnormalize(
		const std::vector<Point>& normPoints,
		const NormVariables& normVar
	) {
		std::vector<Point> points(normPoints.size());
		for (int n = 0; n < (int)normPoints.size(); n++) {
			points[n].x = normPoints[n].x * normVar.dMax + normVar.pxMin;
			points[n].y = normPoints[n].y * normVar.dMax + normVar.pyMin;
		}
		return points;
	}

	std::vector<int> buildSortedPointIndices(
		const std::vector<Point>& points,
		const NormVariables& normVar,
		int size
	) {
		std::vector<int> bins(size, 0.0);
		std::vector<int> indices(size);

		// create bins, each holding some points
		int nBins = (int)lround(pow((double)size, 0.25));
		for (int n = 0; n < size; n++) {

			int i = int(0.99 * nBins * (points[n].x - normVar.pxMin) / normVar.dx);
			int j = int(0.99 * nBins * (points[n].y - normVar.pyMin) / normVar.dy);

			bins[n] = i % 2 == 0 ? i * nBins + j : (i + 1) * nBins - j - 1;
			indices[n] = n;
		}

		// sort indices based off bin number. then sort the points based off the sorted indices
		std::sort(indices.begin(), indices.end(), [&bins](int a, int b) {return bins[a] < bins[b]; });

		return indices;
	}

	void removeFloodFill(
		std::vector<Triangle>& triangles,
		std::vector<Point>& points,
		const std::vector<uint8_t>& constrained
	) {

		int size = (int)points.size() - 3;
		int triSize = (int)triangles.size();

		// mark all triangles that share the same vertex as the supertriangle
		// we know for sure those triangles are outside, and needs to be removed
		std::stack<int> removal;
		std::vector<int8_t> parity(triSize, -1);
		for (int t = 0; t < triSize; t++) {
			const Triangle& triangle = triangles[t];
			if (triangle.v[0] >= size || triangle.v[1] >= size || triangle.v[2] >= size) {
				removal.push(t);
				parity[t] = 0;
			}
		}

		// using the above triangles, start a flood fill where it checks all exterior triangles and marks them as outside
		// partiy keeps track of inside/outside. unvisited = -1. inside = 1. outside = 0.
		while (!removal.empty()) {
			int t = removal.top();
			removal.pop();
			for (int e = 0; e < 3; e++) {
				const Triangle& triangle = triangles[t];
				int nb = triangle.adj[e];
				if (isBoundary(nb)) continue;						// skip boundaries

				int8_t p = constrained[3 * t + e] == 1 ? 1 - parity[t] : parity[t];
				if (parity[nb] != -1) continue;						// ensure the neighbor is unvisited
				parity[nb] = p;										// set parity, which marks as visited
				removal.push(nb);
			}
		}

		// new indices contains triangles that are NOT outside
		int keep = 0;
		std::vector<int> newIndices(triSize, -1);
		for (int t = 0; t < triSize; t++) {
			Triangle& triangle = triangles[t];
			//if (triangle.v[0] >= size || triangle.v[1] >= size || triangle.v[2] >= size) continue;	 // uncomment to enable only super triangle removal
			if (parity[t] != 1) continue;	// uncomment to enable both super triangle removal + floodfill
			newIndices[t] = keep++;
		}

		// make sure triangles are mapped to the new triangle indices
		for (int t = 0; t < triSize; t++) {
			if (newIndices[t] == -1) continue;
			Triangle triangle = triangles[t];
			for (int i = 0; i < 3; i++) {

				int tIndex = triangle.adj[i];
				triangle.adj[i] = isBoundary(tIndex) ? -1 : newIndices[tIndex];
			}
			triangles[newIndices[t]] = triangle;
		}
		triangles.resize(keep);
		points.resize(size);
	}


	void Ruppert(
		std::vector<Triangle>& triangles,
		std::vector<Point>& points,
		std::vector<Segment>& segments,
		const Params& params
	) {

		std::vector<int> queue;
		std::vector<int> touched;

		// store all skinny triangles
		for (int t = 0; t < (int)triangles.size(); t++) {
			if (isSkinny(triangles[t], points, params)) queue.push_back(t);
		}

		while (!queue.empty()) {

			int t = queue.back();
			queue.pop_back();
			if (!isSkinny(triangles[t], points, params)) continue;	// ensures that we are looking at skinny triangles

			const Triangle& triangle = triangles[t];
			Point center = getCircumcenter(points[triangle.v[0]], points[triangle.v[1]], points[triangle.v[2]]);
			points.push_back(center);

			if (!insertVertex(points, triangles, touched, (int)points.size() - 1, t)) {

				points.pop_back();
				touched.clear();
				continue;
			}

			// if any of the modified triangles are skinny, push it back into queue for interrogation
			for (int nT : touched) {
				if (isSkinny(triangles[nT], points, params)) queue.push_back(nT);
			}
			touched.clear();

		}
	}

	bool getPointRebay(
		const std::vector<Point>& points,
		const std::vector<Triangle>& triangles,
		std::vector<double>& h,
		const FrontEdge& frontEdge,
		Point& X
	) {

		// Rebay 1993 sec 6.2: front edge PQ, midpoint M, p = |PQ|/2, q = |C_A M|,
		// new point X on the Voronoi segment at distance d from M along unit vector e
		Point P = points[frontEdge.v0];
		Point Q = points[frontEdge.v1];
		Point apex = points[frontEdge.v2];

		double lenPQ = frontEdge.len;
		double p = 0.5 * lenPQ;
		const Point& M = { 0.5 * (P.x + Q.x), 0.5 * (P.y + Q.y) };
		double ex = -(Q.y - P.y) / lenPQ;
		double ey = (Q.x - P.x) / lenPQ;
		double hM = 0.5 * (h[frontEdge.v0] + h[frontEdge.v1]);
		double rhoM = hM / (std::sqrt(3));
		double rhoHat = std::max(rhoM, p);
		double d = rhoHat + std::sqrt(rhoHat * rhoHat - p * p);
		const Point& CA = getCircumcenter(P, Q, apex);
		double q = (CA.x - M.x) * ex + (CA.y - M.y) * ey;
		if (q <= 0.0) return false;
		d = std::min(d, q);				// Rebay's upper limit rhoHat <= (p^2+q^2)/(2q) reduces to d <= q
		X = { M.x + d * ex, M.y + d * ey };

		return true;
	}

	// sample h at an arbitrary point. use barycentric weight
	bool sampleH(
		const std::vector<Point>& points,
		const std::vector<Triangle>& triangles,
		const std::vector<double>& h,
		const Point& P,
		int tStart,
		double& hOut
	) {

		int t = locateTriangle(points, triangles, P, tStart);
		if (t == -1) return false;

		const Triangle& T = triangles[t];
		const Point& A = points[T.v[0]];
		const Point& B = points[T.v[1]];
		const Point& C = points[T.v[2]];

		double area = orient(A, B, C);

		double wA = orient(B, C, P) / area;
		double wB = orient(C, A, P) / area;
		double wC = orient(A, B, P) / area;

		hOut = wA * h[T.v[0]] + wB * h[T.v[1]] + wC * h[T.v[2]];
		return true;
	}

	bool getPointEngwirda(
		const std::vector<Point>& points,
		const std::vector<Triangle>& triangles,
		std::vector<double>& h,
		const FrontEdge& frontEdge,
		const Params& params,
		Point& X
	) {

		Point P = points[frontEdge.v0];
		Point Q = points[frontEdge.v1];
		Point apex = points[triangles[frontEdge.t].v[(frontEdge.e + 2) % 3]];
		const Point& M = { 0.5 * (P.x + Q.x), 0.5 * (P.y + Q.y) };
		double lenPQ = frontEdge.len;
		double p = 0.5 * lenPQ;
		double ex = -(Q.y - P.y) / lenPQ;
		double ey = (Q.x - P.x) / lenPQ;


		// type 1
		Point X1 = getCircumcenter(P, Q, apex);
		double d1 = (X1.x - M.x) * ex + (X1.y - M.y) * ey;
		if (d1 <= 0.0) return false;

		// type 2
		double hM1, hM2;
		double hM = 0.5 * (h[frontEdge.v0] + h[frontEdge.v1]);

		double d2 = std::sqrt(hM * hM - p * p);
		if (hM < p) return false;
		for (int i = 0; i < 5; i++) {
			const Point& C = { M.x + d2 * ex, M.y + d2 * ey };
			const Point& PCM = { 0.5 * (P.x + C.x), 0.5 * (P.y + C.y) };
			const Point& QCM = { 0.5 * (Q.x + C.x), 0.5 * (Q.y + C.y) };

			if (!sampleH(points, triangles, h, PCM, frontEdge.t, hM1)) break;
			if (!sampleH(points, triangles, h, QCM, frontEdge.t, hM2)) break;
			if (hM1 < p || hM2 < p) break;

			double a1 = std::sqrt(hM1 * hM1 - p * p);
			double a2 = std::sqrt(hM2 * hM2 - p * p);
			d2 = 0.5 * (a1 + a2);
		}
		Point X2 = { M.x + d2 * ex, M.y + d2 * ey };
		if (d2 <= 0.0) return false;

		// type 3
		double a3 = p / (std::tan(0.5 * std::asin(1.0 / (2.0 * params.B))));
		Point X3 = { M.x + a3 * ex, M.y + a3 * ey };
		double d3 = (X3.x - M.x) * ex + (X3.y - M.y) * ey;
		if (d3 <= 0.0) return false;

		if		(d2 <= d1 && d2 <= d3 && d2 >= p)	X = X2;
		else if (d3 <= d1)							X = X3;
		else										X = X1;

		return true;

	}

	void advancingFront(
		std::vector<Point>& points,
		std::vector<Triangle>& triangles,
		std::vector<double>& h,
		std::vector<AdvancingState>& state,
		FrontQueue& frontEdges,
		const Params& params
	) {
		std::vector<int> touched;
		std::vector<int> dirty;
		std::vector<int> mark;
		int epoch = 0;

		while (!frontEdges.empty()) {

			FrontEdge frontEdge = frontEdges.top();		// copy before pop -- top() is a reference into the heap
			frontEdges.pop();
			if (!isValidFrontEdge(triangles, state, frontEdge)) continue;

			double hP = 0.0;
			Point X;
			switch (params.scheme) {
			case InsertionScheme::REBAY:
				if (!getPointRebay(points, triangles, h, frontEdge, X)) continue;
				break;
			case InsertionScheme::ENGWIRDA:
				if (!getPointEngwirda(points, triangles, h, frontEdge, params, X)) continue;
				break;
			}

			sampleH(points, triangles, h, X, frontEdge.t, hP);
			//hP = 0.5 * (h[frontEdge.v0] + h[frontEdge.v1]);
			points.push_back(X);
			if (!insertVertex(points, triangles, touched, (int)points.size() - 1, frontEdge.t)) {
				points.pop_back();
				touched.clear();
				continue;
			}

			h.push_back(hP);
			state.resize(triangles.size(), AdvancingState::WAITING);		// size of state = size of triangles, always
			mark.resize(triangles.size(), -1);

			++epoch;
			dirty.clear();

			// build dirty vector, which includes triangles that have to be reclassified
			// use mark and epoch. mark stores the iteration in which the triangle was stored
			// older triangles stay in mark, but their epoch does not match, so they are not called
			for (int t : touched) {
				if (mark[t] != epoch) {
					mark[t] = epoch;
					dirty.push_back(t);
				}
				const Triangle& triangle = triangles[t];
				for (int e = 0; e < 3; e++) {
					int tAdj = triangle.adj[e];
					if (tAdj == -1) continue;
					if (mark[tAdj] != epoch) {
						mark[tAdj] = epoch;
						dirty.push_back(tAdj);
					}
				}
			}

			// accept dirty triangles
			for (int t : dirty) {
				acceptTest(points, triangles, h, state, params, t);
			}

			// activate dirty triangles
			for (int t : dirty) {
				activeTest(triangles, state, t);
			}

			// update frontal edge
			for (int t : dirty) {
				pushValidFrontEdge(points, triangles, state, frontEdges, t);
			}

			touched.clear();
		}
		for (int i = 0; i < (int)triangles.size(); i++) {
			if (isSkinny(triangles[i], points, params)) {
				printf("%d\n", i);
			}
		}

	}

	void buildFrontEdges(
		const std::vector<Point>& points,
		std::vector<Triangle>& triangles, 
		const std::vector<AdvancingState>& state,
		FrontQueue& frontEdges
	) {
		for (int t = 0; t < (int)triangles.size(); t++) {
			const Triangle& triangle = triangles[t];
			for (int e = 0; e < 3; e++) {
				if (state[t] != AdvancingState::ACCEPTED && (triangle.adj[e] == -1 || state[triangle.adj[e]] == AdvancingState::ACCEPTED)) {
					frontEdges.push({
						t,
						e,
						std::sqrt(dist2(points[triangle.v[e]], points[triangle.v[(e + 1) % 3]])),
						std::sqrt(circumradius2(points, triangle)),
						triangle.v[e],
						triangle.v[(e + 1) % 3],
						triangle.v[(e + 2) % 3]
					});
				}
			}
		}
	}

	void lipschitzSmoothing(
		const std::vector<Point>& points,
		const std::vector<Triangle>& triangles,
		std::vector<double>& h,
		const Params& params
	) {

		// gradation constraint (Lipschitz smoothing)
		bool changed = false;
		int stallCount = 0;
		do {
			changed = false;
			if (stallCount++ > 100) throwError(ErrorCase::STALLED);
			for (int t = 0; t < (int)triangles.size(); t++) {
				const Triangle& triangle = triangles[t];
				for (int e = 0; e < 3; e++) {
					int v = triangle.v[e];
					int w = triangle.v[(e + 1) % 3];

					// iterate to ensure lipschitz condition holds
					double lenVW = std::sqrt(dist2(points[v], points[w]));
					if (h[v] > h[w] + params.beta * lenVW) {
						h[v] = h[w] + params.beta * lenVW;
						changed = true;
					}
					if (h[w] > h[v] + params.beta * lenVW) {
						h[w] = h[v] + params.beta * lenVW;
						changed = true;
					}
				}
			}
		} while (changed);

	}

	void frontalInit(
		std::vector<Point>& points,
		std::vector<Triangle>& triangles,
		std::vector<Segment>& segments,
		std::vector<double>& h,
		std::vector<AdvancingState>& state,
		FrontQueue& frontEdges,
		const Params& params
	) {

		std::vector<double> lSum(points.size());
		std::vector<int> count(points.size());

		// initialize size field
		for (const Segment& seg : segments) {

			double l = std::sqrt(dist2(points[seg.a], points[seg.b]));
			lSum[seg.a] += l;
			lSum[seg.b] += l;
			count[seg.a] += 1;
			count[seg.b] += 1;

		}

		for (int i = 0; i < (int)h.size(); i++) {
			if (count[i] == 0) continue;
			h[i] = lSum[i] / count[i];
		}

		lipschitzSmoothing(points, triangles, h, params);

		// populate triangle states. two passes.
		// first pass accepts all triangles adjacent to a boundary
		// second pass activates any triangle if it is adjacent to any accepted triangles
		// accept triangles (using squared variables)
		for (int t = 0; t < (int)triangles.size(); t++) {
			acceptTest(points, triangles, h, state, params, t);
		}

		// activate triangles
		for (int t = 0; t < (int)triangles.size(); t++) {
			activeTest(triangles, state, t);
		}

		// build initial front edges. all boundary edges are active
		for (int t = 0; t < (int)triangles.size(); t++) {
			pushValidFrontEdge(points, triangles, state, frontEdges, t);
		};
	}

	Mesh generateMesh(
		const std::vector<Point>& points,
		const std::vector<Segment>& segments,
		int size,
		const Params& params
	) {

		//ScopedTimer timer;
		//timer.start();


		// step 1: normalize coordinates
		NormVariables normVar = buildNormVariables(points);
		std::vector<Point> normPoints = normalize(points, normVar, size);

		// step 2: put points into bins and sort by bin number
		std::vector<int> indices = buildSortedPointIndices(points, normVar, size);
		std::vector<Point> p;
		for (int n : indices) {
			p.push_back(normPoints[n]);
		}

		// the sort above renumbered every point, so caller-supplied segment indices have to
		// follow it before they can name anything in the triangulation
		std::vector<int> newIndex(size);
		for (int k = 0; k < size; k++) {
			newIndex[indices[k]] = k;
		}

		// steps 3-4: super triangle, then insert every point into the triangle containing it
		Mesh mesh;
		mesh.triangles = insertPoints(p, size);
		mesh.points = std::move(p);

		// store triangle indicies that the vertex belongs to. vertexTri[v] = t
		// a single vertex can belong to multiple triangle, so the last one that writes will win
		std::vector<int> vertexTri(size + 3, -1);
		for (int t = 0; t < (int)mesh.triangles.size(); t++) {
			for (int i = 0; i < 3; i++) {
				vertexTri[mesh.triangles[t].v[i]] = t;
			}
		}

		mesh.segments.reserve(segments.size());
		for (const Segment& s : segments) {
			mesh.segments.push_back({ newIndex[s.a], newIndex[s.b] });
		}

		// apply constraints
		constrain(vertexTri, mesh);

		// remove triangles in regions that are not needed. also removes the supertriangle
		std::vector<uint8_t> constrained = buildConstrainedEdge(vertexTri, mesh.triangles, mesh.segments);
		removeFloodFill(mesh.triangles, mesh.points, constrained);

		// frontal delaunay
		// initialize sizing field and states for advancing front
		std::vector<double> h(mesh.points.size(), std::numeric_limits<double>::max());
		std::vector<AdvancingState> state(mesh.triangles.size(), AdvancingState::WAITING);
		FrontQueue frontEdges;

		frontalInit(mesh.points, mesh.triangles, mesh.segments, h, state, frontEdges, params);

		// refinement
		advancingFront(mesh.points, mesh.triangles, h, state, frontEdges, params);
		//Ruppert(mesh.triangles, mesh.points, mesh.segments, params);
		mesh.points = unnormalize(mesh.points, normVar);

		return mesh;
	}
}
