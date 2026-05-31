#pragma once

#include <cstddef>     // std::size_t
#include <cstdint>     // std::uint8_t
#include <functional>  // std::hash
#include <string>	   // std::string




enum class FieldType : uint8_t {
	UVelocity,
	VVelocity,
	Pressure,
	Energy,
	Concentration
};

struct FieldTypeHash {
	size_t operator()(FieldType field) const {
		return (size_t)(field);
	}
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
	double val = 0.0;
	bool enabled = true;
};

struct BoundaryConditionConfig {
	BoundaryCondition inlet;
	BoundaryCondition outlet;
	BoundaryCondition outer;
	BoundaryCondition centerline;
};

// ======================================================================
// -----------------------BOUNDARY EDGES---------------------------------
// ======================================================================
enum class EdgeOrient : uint8_t {
	Vertical,
	Horizontal
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

enum class BoundaryType {
	WALL,
	VELOCITY_INLET,
	PRESSURE_OUTLET
};

enum class BoundarySource {
	Domain,
	Obstacle
};

struct BoundarySegment {

	// position variables and segment ID
	GridVertex a;
	GridVertex b;
	int id = -1;

};


struct BoundarySegmentGroup {

	// group ID
	int id = -1;

	// naming variables
	std::string name;
	char nameBuffer[128] = {};

	// vector of all segment IDs in this group
	std::vector<int> segmentIDs;
	std::vector<MeshEdge> edges;

	std::unordered_map<FieldType, BoundaryCondition, FieldTypeHash> bcs;

	// boundary source
	BoundaryType type = BoundaryType::WALL;

};