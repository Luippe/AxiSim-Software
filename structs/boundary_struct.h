#pragma once

#include <cstddef>     // std::size_t
#include <cstdint>     // std::uint8_t
#include <functional>  // std::hash
#include <string>	   // std::string
#include <unordered_map>
#include <vector>
#include <type_traits>
#include <variant>

#include "core_struct.h"

// One material layer of a multi-layer wall (e.g. a membrane or coating) that a
// solved scalar -- concentration or temperature -- must pass through. A stack of
// layers acts as a series transfer resistance on the wall flux.
//   D = layer diffusivity / conductivity
//   d = layer thickness
//   k = D / d, the layer permeance (transfer coefficient); kept in sync with
//       D and d by the editor so the solver can read it directly.
// NOTE: layers are edited and persisted today; the solver does not yet consume
// them. This is the hook where wall-resistance physics would read `k`.
struct Layer {

	double k = 0.0;
	double d = 0.0;
	double R = 0.0;

};


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
	Vec2 p0;
	Vec2 p1;
	double width = 0.0;
	double height = 0.0;

};

// ======================================================================
// -----------------------BOUNDARY CONDITIONS----------------------------
// ======================================================================
// make sure to update bcTypeToString, getDefaultBCType, etc if adding more to this
enum BCType {	
	DIRICHLET,
	NEUMANN,
	FULLY_DEVELOPED,
	MICHAELIS_MENTEN,
	HILL,

	NONE
};


// Per-type boundary-condition parameters. The active variant alternative IS the
// BC type; each alternative carries exactly the scalars that type needs. Every
// alternative keeps `value` as its primary scalar so value()/valueRef() stay
// total, while multi-parameter kinetics add their own fields (km, n).
struct DirichletParams       { static constexpr BCType bcType = DIRICHLET;        double value = 0.0; };
struct NeumannParams         { static constexpr BCType bcType = NEUMANN;          double value = 0.0; };
struct FullyDevelopedParams  { static constexpr BCType bcType = FULLY_DEVELOPED;  double value = 0.0; };
// Kinetics types carry an optional substrate-inhibition factor
// (1 - V2*c^m / (K2^m + c^m)). It is only active when `inhibition` is set, in
// which case the user supplies the inhibition exponent m, half-inhibition
// constant K2, and maximum inhibition fraction V2.
struct MichaelisMentenParams { static constexpr BCType bcType = MICHAELIS_MENTEN; double Vmax = 0.0; double Km = 0.0; bool inhibition = false; double m = 1.0; double K2 = 0.0; double V2 = 0.0; };
struct HillParams			 { static constexpr BCType bcType = HILL;             double Vmax = 0.0; double Km = 0.0; double n = 1.0; bool inhibition = false; double m = 1.0; double K2 = 0.0; double V2 = 0.0; };
struct NoneParams            { static constexpr BCType bcType = NONE;             double value = 0.0; };

using BCParams = std::variant<
	DirichletParams, NeumannParams, FullyDevelopedParams,
	MichaelisMentenParams, HillParams, NoneParams>;

// Detect whether a params alternative carries a `value` member. C++17-friendly
// (CUDA TUs compile this header as C++17, so no requires-expression here).
template <class T, class = void>
struct bcHasValue : std::false_type {};
template <class T>
struct bcHasValue<T, std::void_t<decltype(T::value)>> : std::true_type {};

struct BoundaryCondition {
	BCParams params;        // active alternative = BC type; holds that type's parameters
	bool enabled = true;
	double bcSink = 0.0;    // valueRef() target for params with no `value` (MM, Hill)

	// --- bridge accessors so the rest of the codebase keeps a simple interface ---

	// BC type, derived from the active variant alternative.
	BCType type() const {
		return std::visit([](const auto& p) { return p.bcType; }, params);
	}

	// Switch BC type, swapping to the matching alternative. The primary scalar is
	// carried over so editing a value then changing type does not lose it.
	void setType(BCType t) {
		if (type() == t) return;
		double v = value();
		switch (t) {
		case DIRICHLET:        params = DirichletParams{};       break;
		case NEUMANN:          params = NeumannParams{};         break;
		case FULLY_DEVELOPED:  params = FullyDevelopedParams{};  break;
		case MICHAELIS_MENTEN: params = MichaelisMentenParams{}; break;
		case HILL:             params = HillParams{};            break;
		case NONE:             params = NoneParams{};            break;
		}
		valueRef() = v;
	}

	// Primary scalar for single-value BC types (Dirichlet target / Neumann flux).
	// Kinetics types (Michaelis-Menten, Hill) have no single value -- they expose
	// their own named params -- so value() reads 0 and valueRef() returns a sink.
	// Edit those params via std::visit on `params` (see the row editor), not valueRef().
	double value() const {
		return std::visit([](const auto& p) -> double {
			using T = std::decay_t<decltype(p)>;
			if constexpr (bcHasValue<T>::value) return p.value;
			else return 0.0;
		}, params);
	}
	double& valueRef() {
		return std::visit([this](auto& p) -> double& {
			using T = std::decay_t<decltype(p)>;
			if constexpr (bcHasValue<T>::value) return p.value;
			else return bcSink;
		}, params);
	}
	void setValue(double v) { valueRef() = v; }
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
	SYMMETRY,
	FAR_FIELD
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

enum class MeshRegionShape {
	Circle,
	Rectangle
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

struct MeshRegionOfInfluence {
	int id = -1;
	bool enabled = true;
	MeshRegionShape shape = MeshRegionShape::Circle;

	Vec2 center{ 0.0, 0.0 };
	double radius = 0.1;
	Vec2 min{ 0.0, 0.0 };
	Vec2 max{ 0.0, 0.0 };

	double targetSpacing = 0.01;
	double outsideSpacing = 0.0;
	double transitionThickness = 0.0;
	bool overrideBoundarySpacing = false;
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

	// Per-variable multi-layer wall stack. Only Concentration and Static
	// Temperature carry layers, and only while their field is being solved on a
	// Wall boundary; orphaned entries are pruned by the editor.
	std::unordered_map<
		BoundaryVariable,
		std::vector<Layer>,
		BoundaryVariableHash
	> layers;
};

struct SolutionField {
	std::vector<double> field;
	std::vector<double> dr, dz;
	BoundaryVariable boundaryVariable;
};

struct SolutionScalar {

	double ocr;

};

