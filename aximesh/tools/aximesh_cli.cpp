#include "aximesh/aximesh.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {

// ---------------------------------------------------------------------------
// tests for steps 5-7 of Sloan's algorithm, driven through AxiMesh::insertPoints
//
//   5. insert P: find the triangle enclosing it, delete it, connect P to its
//      three vertices (1 -> 3, so the count grows by two)
//   6. stack the triangles adjacent to the edges opposite P (at most three)
//   7. while the stack is not empty, pop a triangle; if P is strictly inside its
//      circumcircle, swap the shared diagonal and stack whatever is now opposite P
//
// Every case runs the same invariant suite. What separates them is how much of
// step 7 they force: nothing at all, one swap, a cascade through 7.3, or a
// cocircular tie that must not swap forever.
// ---------------------------------------------------------------------------

using AxiMesh::Point;
using AxiMesh::Triangle;
using AxiMesh::Segment;
using Tri = std::array<int, 3>;

double cross(const Point& A, const Point& B, const Point& P) {
	return (B.x - A.x) * (P.y - A.y) - (B.y - A.y) * (P.x - A.x);
}

// The convex hull by monotone chain: its |2A| and how many points sit on it. Any
// triangulation of a point set tiles its hull exactly, so the area is what the triangles
// must add up to once the super triangle is gone, and the boundary count pins down both
// 2n - h - 2 triangles and h hull edges. Collinear points are kept: they add no area but
// they are boundary points, so the strict < below must not pop them.
struct Hull {
	int points;
	double area2;
};

Hull convexHull(const std::vector<Point>& points) {
	int n = (int)points.size();
	std::vector<Point> p = points;
	std::sort(p.begin(), p.end(), [](const Point& a, const Point& b) {
		return a.x < b.x || (a.x == b.x && a.y < b.y); });

	std::vector<Point> hull(2 * n);
	int k = 0;
	for (int i = 0; i < n; i++) {
		while (k >= 2 && cross(hull[k - 2], hull[k - 1], p[i]) < 0.0) k--;
		hull[k++] = p[i];
	}
	for (int i = n - 2, lower = k + 1; i >= 0; i--) {
		while (k >= lower && cross(hull[k - 2], hull[k - 1], p[i]) < 0.0) k--;
		hull[k++] = p[i];
	}
	hull.resize(k ? k - 1 : 0);		// the closing point repeats the first

	double area2 = 0.0;
	for (int i = 0; i < (int)hull.size(); i++) {
		const Point& A = hull[i];
		const Point& B = hull[(i + 1) % hull.size()];
		area2 += A.x * B.y - B.x * A.y;
	}
	return { (int)hull.size(), fabs(area2) };
}

// Taken relative to C so the super triangle's +/-100 coordinates do not eat the
// precision of the subtraction.
bool circumcircle(const Point& A, const Point& B, const Point& C, Point& O, double& r2) {
	double ax = A.x - C.x, ay = A.y - C.y;
	double bx = B.x - C.x, by = B.y - C.y;
	double d = 2.0 * (ax * by - ay * bx);
	if (fabs(d) < 1e-12) return false;

	double a2 = ax * ax + ay * ay;
	double b2 = bx * bx + by * by;
	double ux = (by * a2 - ay * b2) / d;
	double uy = (ax * b2 - bx * a2) / d;

	O = { C.x + ux, C.y + uy };
	r2 = ux * ux + uy * uy;
	return true;
}

// Step 7.2 swaps only when P is STRICTLY inside, so the relative slack here has to
// let a cocircular vertex read as outside -- otherwise the square below flips forever.
bool insideCircumcircle(const Point& A, const Point& B, const Point& C, const Point& P) {
	Point O;
	double r2;
	if (!circumcircle(A, B, C, O, r2)) return false;
	double d2 = (P.x - O.x) * (P.x - O.x) + (P.y - O.y) * (P.y - O.y);
	return d2 < r2 * (1.0 - 1e-9);
}

// Triangles are compared as a set, so it does not matter which slot a split or a swap
// lands on. Rotating to start at the lowest vertex keeps the winding, so a triangle
// that comes out inside-out still fails to match.
Tri canonical(const Triangle& t) {
	int i = t.v[1] < t.v[0] ? 1 : 0;
	if (t.v[2] < t.v[i]) i = 2;
	return { t.v[i], t.v[(i + 1) % 3], t.v[(i + 2) % 3] };
}

// ---------------------------------------------------------------------------
// output
// ---------------------------------------------------------------------------

const size_t lineWidth = 78;

struct Style {
	const char* pass = "";
	const char* fail = "";
	const char* dim = "";
	const char* head = "";
	const char* off = "";
};
Style style;

// Redirect to a file and GetConsoleMode fails, so the escapes stay out of the log.
void enableColour() {
#ifdef _WIN32
	HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
	DWORD mode = 0;
	if (!GetConsoleMode(out, &mode)) return;
	if (!SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) return;
	style = { "\x1b[32m", "\x1b[31m", "\x1b[90m", "\x1b[1m", "\x1b[0m" };
#endif
}

void printRule(char c) {
	std::string rule(lineWidth, c);
	printf("%s\n", rule.c_str());
}

void printWrapped(const std::string& text, const std::string& indent) {
	size_t room = lineWidth - indent.size();
	for (size_t pos = 0; pos < text.size(); ) {
		size_t end = pos + room;
		if (end < text.size()) {
			size_t space = text.rfind(' ', end);
			if (space != std::string::npos && space > pos) end = space;
		}
		else end = text.size();
		printf("%s%s\n", indent.c_str(), text.substr(pos, end - pos).c_str());
		for (pos = end; pos < text.size() && text[pos] == ' '; pos++);
	}
}

void printTriList(const char* label, const char* colour, const std::vector<Tri>& tris) {
	char head[32];
	snprintf(head, sizeof(head), "    %-10s%2d  ", label, (int)tris.size());

	std::string line = head;
	std::string pad(strlen(head), ' ');
	for (const Tri& t : tris) {
		char item[24];
		snprintf(item, sizeof(item), "(%d,%d,%d)", t[0], t[1], t[2]);
		if (line.size() > pad.size() && line.size() + strlen(item) + 1 > lineWidth) {
			printf("%s%s%s\n", colour, line.c_str(), style.off);
			line = pad;
		}
		else if (line.size() > pad.size()) line += " ";
		line += item;
	}
	printf("%s%s%s\n", colour, line.c_str(), style.off);
}

// adj[i] is the triangle across edge v[i] -> v[i+1]; the neighbour must carry that
// same edge reversed and must link back.
bool linkOk(const std::vector<Triangle>& triangles, int t, int i) {
	int u = triangles[t].adj[i];
	if (u < 0) return true;
	if (u >= (int)triangles.size()) return false;
	int a = triangles[t].v[i], b = triangles[t].v[(i + 1) % 3];
	for (int j = 0; j < 3; j++)
		if (triangles[u].v[j] == b && triangles[u].v[(j + 1) % 3] == a
			&& triangles[u].adj[j] == t) return true;
	return false;
}

// Undirected: a constraint is satisfied whichever way round the triangle that owns it winds.
bool edgeExists(const std::vector<Triangle>& triangles, int a, int b) {
	for (const Triangle& t : triangles)
		for (int i = 0; i < 3; i++)
			if ((t.v[i] == a && t.v[(i + 1) % 3] == b)
				|| (t.v[i] == b && t.v[(i + 1) % 3] == a)) return true;
	return false;
}

void printConnectivity(const std::vector<Triangle>& triangles) {
	printf("  connectivity%s      v0  v1  v2     adj0  adj1  adj2%s\n", style.dim, style.off);
	for (int t = 0; t < (int)triangles.size(); t++) {
		printf("    [%2d]      ", t);
		for (int i = 0; i < 3; i++) printf("%4d", triangles[t].v[i]);
		printf("   ");
		for (int i = 0; i < 3; i++) {
			int u = triangles[t].adj[i];
			bool ok = linkOk(triangles, t, i);
			char cell[16];
			if (u < 0) snprintf(cell, sizeof(cell), ".");
			else snprintf(cell, sizeof(cell), "%d%s", u, ok ? "" : "*");
			printf("%s%6s%s", ok ? "" : style.fail, cell, ok ? "" : style.off);
		}
		printf("\n");
	}
}

struct Report {
	int passed = 0;
	int total = 0;
	bool incomplete = false;			// bailed out before the geometry checks ran
	std::vector<std::string> lines;		// buffered so the tally can print above them

	void check(const char* name, bool ok, const std::string& detail = {}) {
		total++;
		passed += ok ? 1 : 0;
		char line[320];
		snprintf(line, sizeof(line), "    %s%s%s  %-34s%s%s%s",
			ok ? style.pass : style.fail, ok ? "PASS" : "FAIL", style.off,
			name, style.dim, detail.c_str(), style.off);
		lines.push_back(line);
	}

	void flush() {
		printf("  checks      %s%d/%d passed%s\n",
			passed == total ? style.pass : style.fail, passed, total, style.off);
		for (const std::string& l : lines) printf("%s\n", l.c_str());
	}
};

void printLegend() {
	printRule('=');
	printf(" %saximesh -- Sloan steps 5-7, driven through insertPoints%s\n", style.head, style.off);
	printRule('=');
	printf(" %sreading the output%s\n", style.head, style.off);
	printWrapped("expected / actual -- one triple per TRIANGLE, holding its three vertex "
		"indices (not its neighbours). Each is wound CCW and rotated to start at its "
		"lowest index, and the list is sorted, so slot and insertion order never matter.",
		"   ");
	printWrapped("missing / extra -- the difference between those two lists: triangles "
		"expected but not produced, and produced but not expected.", "   ");
	printWrapped("connectivity -- printed only when a case fails. Per triangle: its three "
		"vertices, then its three neighbours, where adj[i] is the triangle across edge "
		"v[i]->v[i+1]. '.' is a hull edge with no neighbour; '*' marks a link that is not "
		"reciprocal, i.e. the neighbour does not carry that edge reversed and point back.",
		"   ");
	printf("\n");
}

// ---------------------------------------------------------------------------
// invariants -- everything that holds for any triangulation the two entry points produce,
// whatever route the swaps took. Both suites run this, so generateMesh gets exactly
// the same scrutiny as the hand-built cases.
//
// Three of the checks are counts rather than properties, and those are the three the two
// suites disagree on: insertPoints hands back the super triangle intact, generateMesh runs
// step 8 and hands back the hull of the real points. Expect carries just those three so
// everything below them stays shared.
// ---------------------------------------------------------------------------

struct Expect {
	int triangles;			// insertPoints 2n+1; after step 8, 2n-h-2
	int hullEdges;			// insertPoints 3 (the super triangle's own); after step 8, h
	double area2;			// |2A| of whichever hull the mesh fills
};

Report checkTriangulation(const std::vector<Point>& points,
	const std::vector<Triangle>& triangles, const std::vector<Segment>& segments,
	const Expect& expect) {

	Report r;
	char detail[256];

	// Before step 8 this is 2n+1: splits grow the count by two and swaps leave it alone.
	// After it, 2n-h-2 -- which holds for ANY triangulation of the point set, so it still
	// does not assume which route the swaps took.
	snprintf(detail, sizeof(detail), "want %d, got %d", expect.triangles, (int)triangles.size());
	r.check("triangle count", (int)triangles.size() == expect.triangles, detail);

	// A bad index would make every geometric check below read out of bounds.
	bool indicesValid = true;
	for (const Triangle& t : triangles)
		for (int i = 0; i < 3; i++)
			if (t.v[i] < 0 || t.v[i] >= (int)points.size()) indicesValid = false;
	r.check("vertex indices in range", indicesValid);

	if (!indicesValid) {
		r.incomplete = true;
		return r;
	}

	// Splitting a CCW triangle gives CCW pieces, and so does swapping a diagonal.
	bool allCCW = true;
	double areaSum = 0.0;
	for (const Triangle& t : triangles) {
		double area2 = cross(points[t.v[0]], points[t.v[1]], points[t.v[2]]);
		if (area2 <= 0.0) allCCW = false;
		areaSum += area2;
	}
	r.check("every triangle wound CCW", allCCW);

	// The pieces still tile that hull exactly -- no gaps, no overlaps. This is the check that
	// would catch step 8 dropping a triangle it should have kept.
	snprintf(detail, sizeof(detail), "want %.10g, got %.10g, off by %+.4g",
		expect.area2, areaSum, areaSum - expect.area2);
	r.check("areas sum to the hull",
		fabs(areaSum - expect.area2) < 1e-6 * expect.area2, detail);

	// A triangle listed as its own neighbour sends the next walk round in circles.
	int selfLinks = 0;
	for (int t = 0; t < (int)triangles.size(); t++)
		for (int i = 0; i < 3; i++)
			if (triangles[t].adj[i] == t) selfLinks++;
	*detail = '\0';
	if (selfLinks) snprintf(detail, sizeof(detail), "%d such links", selfLinks);
	r.check("no triangle adjacent to itself", selfLinks == 0, detail);

	// Step 5 has to repoint the two neighbours it steals edges from, and step 7 has to
	// repoint two more per swap -- this is the check that catches a missed one.
	int brokenLinks = 0;
	for (int t = 0; t < (int)triangles.size(); t++)
		for (int i = 0; i < 3; i++)
			if (!linkOk(triangles, t, i)) brokenLinks++;
	*detail = '\0';
	if (brokenLinks) snprintf(detail, sizeof(detail), "%d of %d links, marked * below",
		brokenLinks, 3 * (int)triangles.size());
	r.check("adjacency links reciprocal", brokenLinks == 0, detail);

	// A closed hull has exactly one boundary edge per boundary point, and nothing interior
	// may read -1. After step 8 this is the check that catches a neighbour link left pointing
	// at a triangle that was dropped instead of being reset to the boundary sentinel.
	int hullEdges = 0;
	for (const Triangle& t : triangles)
		for (int i = 0; i < 3; i++) if (t.adj[i] < 0) hullEdges++;
	snprintf(detail, sizeof(detail), "want %d, got %d", expect.hullEdges, hullEdges);
	r.check("hull edge count", hullEdges == expect.hullEdges, detail);

	// Every constraint has to come out as an edge of some triangle. This is the only check
	// that CDT adds -- the six above are about the mesh being a mesh, and constraints change
	// which edges exist, not whether the connectivity is sound.
	if (!segments.empty()) {
		int missing = 0;
		std::string firstMissing;
		for (const Segment& s : segments) {
			if (edgeExists(triangles, s.a, s.b)) continue;
			if (!missing++) {
				snprintf(detail, sizeof(detail), "%d-%d absent", s.a, s.b);
				firstMissing = detail;
			}
		}
		*detail = '\0';
		if (missing > 1)
			snprintf(detail, sizeof(detail), "%s, and %d more of %d",
				firstMissing.c_str(), missing - 1, (int)segments.size());
		else if (missing) snprintf(detail, sizeof(detail), "%s", firstMissing.c_str());
		r.check("every segment survives as an edge", missing == 0, detail);
	}

	// The property steps 6-7 exist to establish. Order independent, so it holds whatever
	// route the swaps took -- and it is the only check the cocircular case can be given.
	//
	// A CDT is deliberately NOT globally Delaunay: a constraint can block visibility and
	// leave a vertex sitting inside a circumcircle legitimately. Skipped rather than
	// weakened, until the check knows how to ask whether the vertex is actually visible.
	if (!segments.empty()) {
		r.check("no vertex inside any circumcircle", true, "skipped -- constrained case");
		return r;
	}

	int violations = 0;
	std::string firstViolation;
	for (int t = 0; t < (int)triangles.size(); t++)
		for (int p = 0; p < (int)points.size(); p++) {
			const Triangle& tri = triangles[t];
			if (p == tri.v[0] || p == tri.v[1] || p == tri.v[2]) continue;
			if (!insideCircumcircle(points[tri.v[0]], points[tri.v[1]], points[tri.v[2]],
				points[p])) continue;
			if (!violations++) {
				snprintf(detail, sizeof(detail), "vertex %d sits inside (%d,%d,%d)",
					p, tri.v[0], tri.v[1], tri.v[2]);
				firstViolation = detail;
			}
		}
	*detail = '\0';
	if (violations > 1)
		snprintf(detail, sizeof(detail), "%s, and %d more", firstViolation.c_str(), violations - 1);
	else if (violations) snprintf(detail, sizeof(detail), "%s", firstViolation.c_str());
	r.check("no vertex inside any circumcircle", violations == 0, detail);

	return r;
}

void reportResult(Report& r, const std::vector<Triangle>& triangles) {
	r.flush();
	if (r.incomplete)
		printf("    %sgeometry checks skipped -- they would read out of bounds%s\n",
			style.dim, style.off);
	if (r.passed != r.total) {
		printf("\n");
		printConnectivity(triangles);
	}
	printf("\n");
}

int printSummary(const char* title, const std::vector<const char*>& names,
	const std::vector<Report>& reports) {

	printRule('=');
	printf(" %s%s%s\n", style.head, title, style.off);
	printRule('=');

	int passed = 0;
	for (int i = 0; i < (int)reports.size(); i++) {
		const Report& r = reports[i];
		bool ok = r.passed == r.total && !r.incomplete;
		passed += ok ? 1 : 0;
		printf("  %s%s%s  %-16s %d/%d checks%s%s%s\n", ok ? style.pass : style.fail,
			ok ? "PASS" : "FAIL", style.off, names[i], r.passed, r.total,
			style.dim, r.incomplete ? "  (rest skipped)" : "", style.off);
	}
	printf("  %s%d/%d cases passed%s\n\n", passed == (int)reports.size() ? style.pass : style.fail,
		passed, (int)reports.size(), style.off);
	return passed;
}

// ---------------------------------------------------------------------------
// cases
// ---------------------------------------------------------------------------

struct Case {
	const char* name;
	std::vector<Point> points;
	std::vector<Tri> expected;		// empty: cocircular, more than one answer is correct
	const char* note;
};

// Points go straight to insertPoints, skipping normalize/bin-sort, so the insertion
// order -- and therefore the expected answer -- is exact. Vertices 0..n-1 are the input
// points in order; n, n+1, n+2 are the super triangle. Each expected list was cross
// checked against a brute-force Delaunay triangulation of all n+3 vertices.
std::vector<Case> cases() {
	return {
		{"one point",
		 {{0.30, 0.20}},
		 {{0,1,2}, {0,2,3}, {0,3,1}},
		 "step 5 alone -- one split, every neighbour is -1 so step 6 stacks nothing"},

		{"no swap needed",
		 {{0.25, 0.25}, {0.75, 0.25}, {0.50, 0.75}},
		 {{0,1,2}, {0,2,5}, {0,3,4}, {0,4,1}, {0,5,3}, {1,4,5}, {1,5,2}},
		 "steps 5-7 -- step 6 stacks three real triangles and step 7 must reject all three"},

		{"one swap",
		 {{1.0, 1.0}, {-1.0, 0.5}},
		 {{0,1,3}, {0,3,4}, {0,4,1}, {1,2,3}, {1,4,2}},
		 "steps 5-7 -- two points is the smallest set that can swap at all: P1 lands inside "
		 "the circumcircle of (0,2,3), so edge 2-0 is swapped for edge 1-3"},

		{"cascade",
		 {{1.0, 0.0}, {0.6, 0.9}, {-0.4, 1.0}, {-1.0, 0.1},
		  {-0.5, -0.8}, {0.5, -0.7}, {0.05, 0.05}},
		 {{0,1,6}, {0,5,8}, {0,6,5}, {0,8,9}, {0,9,1}, {1,2,6}, {1,9,2}, {2,3,6},
		  {2,9,3}, {3,4,6}, {3,7,4}, {3,9,7}, {4,5,6}, {4,7,8}, {4,8,5}},
		 "steps 5-7 -- irregular hexagon then its centre; 9 swaps, and 3 of them are only "
		 "reachable because step 7.3 re-stacks (P5 alone swaps three deep)"},

		{"cocircular",
		 {{0.2, 0.2}, {0.8, 0.2}, {0.8, 0.8}, {0.2, 0.8}},
		 {},
		 "steps 5-7 -- the four corners are cocircular, so either diagonal is correct and "
		 "step 7.2 must treat 'on the circle' as outside; a >= there hangs this case"},
	};
}

// ---------------------------------------------------------------------------

Report runCase(const Case& c, int index, int count) {

	const int size = (int)c.points.size();
	char inputs[32];
	if (size == 1) snprintf(inputs, sizeof(inputs), "vertex 0");
	else snprintf(inputs, sizeof(inputs), "vertices 0-%d", size - 1);

	printRule('-');
	printf(" %s[%d/%d] %s%s  --  %d point%s, %s%s%s = input, %s%d-%d%s = super triangle\n",
		style.head, index, count, c.name, style.off, size, size == 1 ? "" : "s",
		style.head, inputs, style.off, style.head, size, size + 2, style.off);
	printRule('-');
	printWrapped(c.note, "  ");
	printf("\n");

	// insertPoints appends the three super vertices, so points grows to size + 3
	std::vector<Point> points = c.points;
	std::vector<Triangle> triangles = AxiMesh::insertPoints(points, size);

	std::vector<Tri> actual;
	for (const Triangle& t : triangles) actual.push_back(canonical(t));
	std::sort(actual.begin(), actual.end());

	std::vector<Tri> expected = c.expected;
	std::sort(expected.begin(), expected.end());

	if (expected.empty())
		printf("    %-10s     %sany Delaunay triangulation is correct here%s\n",
			"expected", style.dim, style.off);
	else
		printTriList("expected", "", expected);
	printTriList("actual", "", actual);

	if (!expected.empty()) {
		std::vector<Tri> missing, extra;
		std::set_difference(expected.begin(), expected.end(), actual.begin(), actual.end(),
			std::back_inserter(missing));
		std::set_difference(actual.begin(), actual.end(), expected.begin(), expected.end(),
			std::back_inserter(extra));
		if (!missing.empty()) printTriList("missing", style.fail, missing);
		if (!extra.empty()) printTriList("extra", style.fail, extra);
	}
	printf("\n");

	// insertPoints stops before step 8, so the mesh still fills the super triangle
	// the super triangle is the whole hull here, and insertPoints left its three
	// vertices at the tail of points -- so its |2A| comes from the mesh, not a literal
	const double superArea2 = fabs(cross(points[size], points[size + 1], points[size + 2]));
	Report r = checkTriangulation(points, triangles, {}, { 2 * size + 1, 3, superArea2 });

	if (!r.incomplete && !expected.empty()) {
		char detail[64];
		*detail = '\0';
		if (actual != expected) snprintf(detail, sizeof(detail), "see missing/extra above");
		r.check("triangle set matches expected", actual == expected, detail);
	}

	reportResult(r, triangles);
	return r;
}

// ---------------------------------------------------------------------------
// end-to-end cases through generateMesh
// ---------------------------------------------------------------------------

struct MeshCase {
	const char* name;
	std::vector<Point> points;
	std::vector<Segment> segments;		// empty: unconstrained, the whole suite applies
	const char* expected;
};

// Any triangulation of a point set has exactly 2n - h - 2 triangles and h boundary edges,
// so both are checkable without knowing which triangulation you produced.
Expect expectFor(const MeshCase& c) {
	const Hull h = convexHull(c.points);
	return { 2 * (int)c.points.size() - h.points - 2, h.points, h.area2 };
}

const double s3 = 0.86602540378443865;		// sin(60 deg)

// Segments are given in CALLER indices -- generateMesh remaps them through the bin sort,
// and Mesh::segments comes back renumbered to match the triangulation.
std::vector<MeshCase> meshCases() {
	return {
		{"triangle",
		 {{0.0, 0.0}, {1.0, 0.0}, {0.0, 1.0}}, {},
		 "one triangle, nothing to swap"},

		{"square",
		 {{0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}}, {},
		 "cocircular -- either diagonal is correct, must not swap forever"},

		{"rhombus-tall",
		 {{-1.0, 0.0}, {0.0, -2.0}, {1.0, 0.0}, {0.0, 2.0}}, {},
		 "diagonal must be horizontal, (-1,0)-(1,0)"},

		{"rhombus-wide",
		 {{-1.0, 0.0}, {0.0, -0.5}, {1.0, 0.0}, {0.0, 0.5}}, {},
		 "diagonal must be vertical, (0,-0.5)-(0,0.5)"},

		{"collinear-hull",
		 {{0.0, 0.0}, {1.0, 0.0}, {2.0, 0.0}, {2.0, 1.0}, {0.0, 1.0}}, {},
		 "(1,0) sits on a hull edge and must survive as a vertex"},

		{"hexagon-fan",
		 {{1.0, 0.0}, {0.5, s3}, {-0.5, s3}, {-1.0, 0.0},
		  {-0.5, -s3}, {0.5, -s3}, {0.0, 0.0}}, {},
		 "six triangles, every one sharing the centre point"},

		// ---- constrained ----

		{"boundary-only",
		 {{0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}},
		 {{0,1}, {1,2}, {2,3}, {3,0}},
		 "the four hull edges are already edges, so constraint insertion must be a no-op -- "
		 "this is the 'segment already present, do nothing' path"},

		{"square-diagonal",
		 {{0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}},
		 {{0,1}, {1,2}, {2,3}, {3,0}, {0,2}},
		 "cocircular square with diagonal 0-2 forced; the constraint is still Delaunay-legal, "
		 "so only the insertion machinery is under test, not the conflict handling"},

		{"rhombus-forced",
		 {{-1.0, 0.0}, {0.0, -0.5}, {1.0, 0.0}, {0.0, 0.5}},
		 {{0,1}, {1,2}, {2,3}, {3,0}, {0,2}},
		 "the real CDT case -- the Delaunay diagonal here is vertical (1-3), and 0-2 is forced "
		 "instead, so a flip has to happen that the Delaunay test would immediately undo"},

		{"blocked-edge",
		 {{0.0, 0.0}, {4.0, 0.0}, {2.0, 0.2}, {2.0, -2.0}},
		 {{0,1}},
		 "vertex 2 sits just above the midpoint of 0-1 and inside the circumcircle of (0,3,1), "
		 "so the unconstrained pass picks diagonal 2-3 instead; recovering 0-1 needs a flip "
		 "that swapTest actively rejects"},

		{"zigzag",
		 {{0.0, 0.0}, {6.0, 0.0}, {1.0, 0.8}, {2.0, -0.7},
		  {3.0, 0.9}, {4.0, -0.6}, {5.0, 0.7}},
		 {{0,1}},
		 "constraint 0-1 spans the full width while the other five points alternate above and "
		 "below it, so every diagonal of the unconstrained triangulation crosses it -- the first "
		 "case where step 3's queue has to loop instead of resolving in a single flip"},

		{"notched-domain",
		 {{0.0, 0.0}, {2.0, 0.0}, {2.0, 2.0}, {1.0, 1.0}, {0.0, 2.0}},
		 {{0,1}, {1,2}, {2,3}, {3,4}, {4,0}},
		 "non-convex outline whose five edges all survive the unconstrained pass anyway -- it "
		 "guards the flood fill later rather than testing recovery. The notch vertex (1,1) is "
		 "INSIDE the convex hull, so h is 4 and the mesh still covers the whole square; the "
		 "flood fill is what turns that into h=5 and 3 triangles"},
	};
}

// generateMesh normalizes and bin-sorts before it reaches insertPoints, so vertex indices
// no longer match caller order -- but every invariant is index agnostic, so the same suite
// applies unchanged. Step 8 has already dropped the super triangle by the time this sees
// the mesh, so the expected counts are the hull's rather than 2n+1.
Report runMeshCase(const MeshCase& c, int index, int count) {

	const int size = (int)c.points.size();
	const Expect expect = expectFor(c);

	printRule('-');
	printf(" %s[%d/%d] %s%s  --  %d points, %d segments, %s%d on the hull -> %d triangles%s\n",
		style.head, index, count, c.name, style.off, size, (int)c.segments.size(),
		style.dim, expect.hullEdges, expect.triangles, style.off);
	printRule('-');
	printWrapped(c.expected, "  ");
	printf("\n");

	AxiMesh::Mesh mesh = AxiMesh::generateMesh(c.points, c.segments, size);

	Report r = checkTriangulation(mesh.points, mesh.triangles, mesh.segments, expect);
	reportResult(r, mesh.triangles);
	return r;
}

}  // namespace

int main() {

	enableColour();
	printLegend();

	std::vector<Case> insertCases = cases();
	std::vector<Report> reports;
	std::vector<const char*> names;
	for (int i = 0; i < (int)insertCases.size(); i++) {
		reports.push_back(runCase(insertCases[i], i + 1, (int)insertCases.size()));
		names.push_back(insertCases[i].name);
	}

	printRule('=');
	printf(" %sgenerateMesh end to end%s\n", style.head, style.off);
	printRule('=');
	printf("\n");

	std::vector<MeshCase> mesh = meshCases();
	std::vector<Report> meshReports;
	std::vector<const char*> meshNames;
	for (int i = 0; i < (int)mesh.size(); i++) {
		meshReports.push_back(runMeshCase(mesh[i], i + 1, (int)mesh.size()));
		meshNames.push_back(mesh[i].name);
	}

	int passed = printSummary("summary -- steps 5-7, through insertPoints", names, reports);
	int meshPassed = printSummary("summary -- steps 1-8, through generateMesh", meshNames, meshReports);

	return (passed == (int)reports.size() && meshPassed == (int)meshReports.size()) ? 0 : 1;
}
