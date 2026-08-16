#include "aximesh/aximesh.h"

#include <algorithm>
#include <array>
#include <stack>
#include <utility>

#include <chrono>
#include <cstdio>

void check() {
	printf("RUNNING HERE!!!\n");
}

using AxiMesh::Point;
using AxiMesh::Triangle;
using AxiMesh::Segment;

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


bool isPointInTriangle(const Triangle& T, const Point& P, const std::vector<Point>& points) {
	for (int nE = 0; nE < 3; nE++) {

		int indexA = T.v[nE];
		int indexB = T.v[(nE + 1) % 3];
		const Point& A = points[indexA];
		const Point& B = points[indexB];

		double d = (B.x - A.x) * (P.y - A.y) - (B.y - A.y) * (P.x - A.x);
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

bool isBoundary(int index) {
	return index == -1;
}

void updateEdge(Triangle& T, int check, int replace) {
	for (int i = 0; i < 3; i++) {
		if (T.adj[i] == check) {
			T.adj[i] = replace;
			break;
		}
	}
}


std::vector<Triangle> AxiMesh::insertPoints(std::vector<Point>& points, int size) {

	// step 3: create super triangle
	std::vector<Triangle> triangles;
	triangles.reserve(2 * size + 1);
	triangles.push_back({ { size, size + 1, size + 2 } , {-1, -1, -1} });
	points.push_back({ -100.0, -100.0 });
	points.push_back({ 100.0, -100.0 });
	points.push_back({ 0, 100.0 });

	// step 4: check if point is inside the most recently created triangle
	int nT = 0;
	int nP = 0;
	while (nP < size) {

		Point& P = points[nP];
		Triangle& T = triangles[nT];

		bool isPointInside = false;
		for (int nE = 0; nE < 3; nE++) {

			int indexA = T.v[nE];
			int indexB = T.v[(nE + 1) % 3];
			Point& A = points[indexA];
			Point& B = points[indexB];

			double d = (B.x - A.x) * (P.y - A.y) - (B.y - A.y) * (P.x - A.x);
			isPointInside = d >= 0;
			if (!isPointInside) {
				nT = T.adj[nE];
				break;
			};
		}

		// if point is inside the triangle, make 3 lines from the triangle to the point, creating 3 triangles
		if (!isPointInside) continue;

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

			// the vertices are nP, v0, v1, and v2
			// for the new triangles, make sure nP is the third point; optional (i think), but also good for consistency

			int inner = tOuter.adj[edge];
			Triangle& tInner = triangles[inner];

			int v0 = tOuter.v[edge];
			int v1 = tOuter.v[(edge + 1) % 3];
			int v2 = tOuter.v[(edge + 2) % 3];

			int tOuter1New = tOuter.adj[(edge + 1) % 3];
			int tOuter2New = tOuter.adj[(edge + 2) % 3];

			int tInner1New = tInner.adj[1];
			int tInner2New = tInner.adj[2];

			triangles[inner] = { {v1, v2, nP}, {tOuter1New, outer, tInner2New} };
			triangles[outer] = { {v2, v0, nP}, {tOuter2New, tInner1New, inner} };


			// there are two edges that needs to be corrected
			if (!isBoundary(tInner1New)) {
				updateEdge(triangles[tInner1New], inner, outer);
			}

			if (!isBoundary(tOuter1New)) {
				updateEdge(triangles[tOuter1New], outer, inner);
			}

			if (!isBoundary(tOuter1New)) tStack.push(tOuter1New);
			if (!isBoundary(tOuter2New)) tStack.push(tOuter2New);

		}

		// only move to the next point after a successful split
		nP += 1;

	}

	return triangles;
}

void constrain(const std::vector<Segment>& segments) {

}

AxiMesh::Mesh AxiMesh::generateMesh(
	const std::vector<double>& px,
	const std::vector<double>& py,
	const std::vector<Segment>& segments,
	int size
) {

	//ScopedTimer timer;
	//timer.start();

	std::vector<double> npx(size, 0.0);
	std::vector<double> npy(size, 0.0);


	// step 1: normalize coordinates
	auto resultX = std::minmax_element(px.begin(), px.end());
	auto resultY = std::minmax_element(py.begin(), py.end());

	double pxMin = *resultX.first;
	double pyMin = *resultY.first;

	double dx = *resultX.second - *resultX.first;
	double dy = *resultY.second - *resultY.first;
	double dMax = std::max(dx, dy);

	for (int n = 0; n < size; n++) {
		npx[n] = (px[n] - pxMin) / dMax;
		npy[n] = (py[n] - pyMin) / dMax;
	}
	
	// step 2: put points into bins and sort by bin number
	int nBins = (int)lround(pow((double)size, 0.25));
	std::vector<int> bins(size, 0.0);
	std::vector<int> indices(size);

	for (int n = 0; n < size; n++) {

		int i = int(0.99 * nBins * (px[n] - pxMin) / dx);
		int j = int(0.99 * nBins * (py[n] - pyMin) / dy);

		bins[n] = i % 2 == 0 ? i * nBins + j  : (i + 1) * nBins - j - 1;
		indices[n] = n;
	}

	// sort indices based off bin number. then sort the points based off the sorted indices
	std::vector<Point> points;

	std::sort(indices.begin(), indices.end(), [&bins](int a, int b) {return bins[a] < bins[b]; });

	for (int n : indices) {
		points.push_back({ npx[n], npy[n] });
	}

	// the sort above renumbered every point, so caller-supplied segment indices have to
	// follow it before they can name anything in the triangulation
	std::vector<int> newIndex(size);
	for (int k = 0; k < size; k++) {
		newIndex[indices[k]] = k;
	}

	// steps 3-4: super triangle, then insert every point into the triangle containing it
	Mesh mesh;
	mesh.triangles = insertPoints(points, size);	// appends the super vertices to points
	mesh.points = std::move(points);

	mesh.segments.reserve(segments.size());
	for (const Segment& s : segments) {
		mesh.segments.push_back({ newIndex[s.a], newIndex[s.b] });
	}

	// TODO: insert mesh.segments as constraints here (Sloan 1993), then flood fill from the
	// super triangle through unconstrained edges to drop everything outside the domain

	//timer.end();
	return mesh;
}