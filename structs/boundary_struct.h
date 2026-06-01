#pragma once

#include <cstddef>     // std::size_t
#include <cstdint>     // std::uint8_t
#include <functional>  // std::hash
#include <string>	   // std::string


enum class BoundaryVariable : uint8_t {
	UVelocity,
	VVelocity,
	Pressure,

	StaticTemperature,
	Concentration,

	TurbulenceIntensity,
	TurbulentViscosityRatio
};

struct BoundaryVariableHash {
	std::size_t operator()(BoundaryVariable variable) const {
		return (size_t)(variable);
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
	PRESSURE_OUTLET,
	SYMMETRY
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

	// boundary type
	BoundaryType type = BoundaryType::WALL;

	// vector of all segment IDs in this group
	std::vector<int> segmentIDs;
	std::vector<MeshEdge> edges;

	// each group stores
	std::unordered_map<
		BoundaryVariable,
		BoundaryCondition, 
		BoundaryVariableHash> bcs;

};

struct BoundaryConditionDevice {
	uint8_t* uType = nullptr;
	double* uValue = nullptr;

	uint8_t* vType = nullptr;
	double* vValue = nullptr;

	uint8_t* pType = nullptr;
	double* pValue = nullptr;

	uint8_t* tType = nullptr;
	double* tValue = nullptr;

	uint8_t* cType = nullptr;
	double* cValue = nullptr;
};