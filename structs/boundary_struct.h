#pragma once

#include <cstddef>     // std::size_t
#include <cstdint>     // std::uint8_t
#include <functional>  // std::hash
#include <string>	   // std::string
#include <unordered_map>

enum class BoundaryVariable : uint8_t {
	UVelocity,
	VVelocity,
	Pressure,

	StaticTemperature,
	Concentration,

	None
};

struct BoundaryVariableHash {
	std::size_t operator()(BoundaryVariable variable) const {
		return (size_t)(variable);
	}
};

struct PendingCircle {

	bool pending = false;
	double radius = 0.0;


};

struct PendingRect {

	bool pending = false;
	double width = 0.0;
	double height = 0.0;

};

// ======================================================================
// -----------------------BOUNDARY CONDITIONS----------------------------
// ======================================================================
enum BCType {
	DIRICHLET,
	NEUMANN,
	FULLY_DEVELOPED,
	NONE
};


struct BoundaryCondition {
	BCType type = DIRICHLET;
	double value = 0.0;
	bool enabled = true;
};

struct Vec2 {
	double z = 0.0;
	double r = 0.0;
};

struct EdgeKey {
	int a;
	int b;

	EdgeKey(int v0, int v1) {
		a = std::min(v0, v1);
		b = std::max(v0, v1);
	}

	bool operator==(const EdgeKey& other) const {
		return a == other.a && b == other.b;
	}
};

struct EdgeKeyHash {
	std::size_t operator()(const EdgeKey& e) const {
		std::size_t h1 = std::hash<int>{}(e.a);
		std::size_t h2 = std::hash<int>{}(e.b);
		return h1 ^ (h2 << 1);
	}
};

struct CDTConstraintEdge {
	std::size_t v0;
	std::size_t v1;
};

struct FVCell {
	Vec2 center;

	double area2D = 0.0;
	double volume = 0.0;

	std::vector<int> faceIDs;

	bool active = true;
	bool solid = false;
};

struct FVFace {
	int owner = -1;
	int neighbor = -1;

	int v0 = -1;
	int v1 = -1;

	Vec2 normal;
	Vec2 center;

	double length2D = 0.0;
	double area = 0.0;

	int boundaryGroupID = -1;

	bool isBoundary() const {
		return neighbor < 0;
	}
};

struct FVMesh {

	int nr = 0;
	int nz = 0;

	std::vector<FVCell> cells;
	std::vector<FVFace> faces;

	int numCells() const {
		return (int)cells.size();
	}

	int numFaces() const {
		return (int)faces.size();
	}

};

// ======================================================================
// -----------------------BOUNDARY EDGES---------------------------------
// ======================================================================
enum class EdgeOrient : uint8_t {
	Vertical,
	Horizontal,
	Both
};

struct MeshEdge {
	EdgeOrient orient;

	int i;
	int j;
};

inline bool operator==(const MeshEdge& a, const MeshEdge& b) {
	return a.orient == b.orient &&
		a.i == b.i &&
		a.j == b.j;
}

struct MeshEdgeHash {
	std::size_t operator()(const MeshEdge& e) const {
		std::size_t h1 = std::hash<int>{}(static_cast<int>(e.orient));
		std::size_t h2 = std::hash<int>{}(e.i);
		std::size_t h3 = std::hash<int>{}(e.j);

		return h1 ^ (h2 << 1) ^ (h3 << 2);
	}
};

struct GridVertex {
	int i;
	int j;
};

inline bool operator==(const GridVertex& a, const GridVertex& b) {
	return a.i == b.i && a.j == b.j;
}

struct GridVertexHash {
	std::size_t operator()(const GridVertex& v) const {
		std::size_t h1 = std::hash<int>{}(v.i);
		std::size_t h2 = std::hash<int>{}(v.j);

		return h1 ^ (h2 << 1);
	}
};

struct PointKey {
	long long z;
	long long r;

	bool operator==(const PointKey& other) const {
		return z == other.z && r == other.r;
	}
};

struct PointKeyHash {
	std::size_t operator()(const PointKey& key) const {
		std::size_t h1 = std::hash<long long>{}(key.z);
		std::size_t h2 = std::hash<long long>{}(key.r);
		return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
	}
};




enum class MeshType {
	Structured,
	Unstructured
};

struct Triangle {

	int v0 = -1;
	int v1 = -1;
	int v2 = -1;

};

enum class BoundaryType {
	WALL,
	VELOCITY_INLET,
	PRESSURE_OUTLET,
	SYMMETRY
};

enum class BoundarySource {
	Domain,
	Obstacle
};

struct BoundaryVertex {
	int id = -1;

	// For unstructured meshes / CDT.
	// Index into unstructuredPoints.
	int pointID = -1;

	Vec2 pos;

	// Structured-grid only.
	GridVertex grid{ -1, -1 };
	bool hasGridVertex = false;
};

enum class BoundarySizingMode {
	EdgeCount,
	TargetSpacing,
	None
};

struct BoundarySizing {
	bool enabled = false;

	BoundarySizingMode mode = BoundarySizingMode::None;

	// Used when mode == TargetSpacing
	double targetSpacing = 0.001;

	// Used when mode == EdgeCount
	int edgeCount = 20;

	// 1.0 = uniform
	// > 1.0 = clustered near start
	// < 1.0 = clustered near end
	double bias = 1.0;
};

struct BoundaryEdge {
	int id = -1;

	// Indices into boundaryVertices.
	int v0 = -1;
	int v1 = -1;

	// Owning selectable boundary segment.
	int segmentID = -1;

	// Named boundary group, if assigned.
	int groupID = -1;

	BoundarySource source = BoundarySource::Domain;

	// Structured-grid only.
	bool hasMeshEdge = false;
	MeshEdge meshEdge{};
};

struct BoundarySegment {
	int id = -1;

	std::vector<Vec2> controlPoints;
	std::vector<int> edgeIDs;

	BoundarySizing sizing;

	int groupID = -1;
	int loopID = -1;

	BoundarySource source = BoundarySource::Domain;
};

struct BoundarySegmentGroup {
	int id = -1;

	std::string name;
	char nameBuffer[128] = {};

	BoundaryType type = BoundaryType::WALL;

	// Universal group membership.
	std::vector<int> segmentIDs;

	// Structured-only derived lookup.
	std::vector<MeshEdge> edges;

	EdgeOrient includesOrientation = EdgeOrient::Horizontal;
	float totalLength = 0.0f;

	// sizing
	BoundarySizing sizing;

	// bcs
	std::unordered_map<
		BoundaryVariable,
		BoundaryCondition,
		BoundaryVariableHash
	> bcs;
};

struct SolutionField {
	std::vector<double> field;
	std::vector<double> dr, dz;
	BoundaryVariable boundaryVariable;
};