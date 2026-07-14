#include "mesh.h"
#include "console.h"

#include "gmsh.h_cwrap"
#include "time_manager.h"
#include "solver_struct.h"
#include "printer.h"
#include <glm/trigonometric.hpp>
#include <algorithm>
#include <numeric>
#include <queue>
#include <unordered_set>
#include <unordered_map>

#include "math_func.h"


Mesh::Mesh(Config& config) : g(config.g) {
	initializeUnstructuredDomain(2,2);
}

PointKey makePointKey(Vec2 p, double tol) {
	return PointKey{
		(long long)std::llround(p.z / tol),
		(long long)std::llround(p.r / tol)
	};
}

// Shortest distance from point p to the segment a-b (in z-r space).
static double distancePointToSegment(Vec2 p, Vec2 a, Vec2 b) {
	double abz = b.z - a.z;
	double abr = b.r - a.r;
	double apz = p.z - a.z;
	double apr = p.r - a.r;

	double denom = abz * abz + abr * abr;
	double t = (denom > 1e-30) ? (apz * abz + apr * abr) / denom : 0.0;
	t = std::max(0.0, std::min(1.0, t));

	double cz = a.z + t * abz;
	double cr = a.r + t * abr;

	double dz = p.z - cz;
	double dr = p.r - cr;

	return std::sqrt(dz * dz + dr * dr);
}

namespace {
	constexpr double sketchMeshTwoPi = 6.28318530717958647692;

	struct SketchSegmentDraft {
		std::vector<Vec2> controlPoints;
		SketchEntityType sourceType = SketchEntityType::Line;
		int entityID = -1;
		int edgeIndex = -1;
		double startT = 0.0;
		double endT = 1.0;
	};

	struct SketchLoopDraft {
		std::vector<int> segmentIndices;
		std::vector<Vec2> orderedPoints;
		double area = 0.0;
	};

	double sketchDistanceSquared(Vec2 a, Vec2 b) {
		double dz = b.z - a.z;
		double dr = b.r - a.r;
		return dz * dz + dr * dr;
	}

	bool sketchPointsMatch(Vec2 a, Vec2 b, double tol) {
		return sketchDistanceSquared(a, b) <= tol * tol;
	}

	double normalizeSketchAngle(double angle) {
		angle = std::fmod(angle, sketchMeshTwoPi);
		if (angle < 0.0) {
			angle += sketchMeshTwoPi;
		}
		return angle;
	}

	double positiveSketchAngleSpan(double startAngle, double endAngle) {
		double start = normalizeSketchAngle(startAngle);
		double end = normalizeSketchAngle(endAngle);
		while (end < start) {
			end += sketchMeshTwoPi;
		}
		return end - start;
	}

	Vec2 sketchPointOnCircle(Vec2 center, double radius, double angle) {
		return Vec2{
			center.z + radius * std::cos(angle),
			center.r + radius * std::sin(angle)
		};
	}

	int curveSampleCount(double curveLength, double targetSpacing, int minimum) {
		if (targetSpacing <= 1e-30) {
			return minimum;
		}

		return std::max(minimum, (int)std::ceil(curveLength / targetSpacing));
	}

	std::vector<Vec2> sampleSketchCircle(
		Vec2 center,
		double radius,
		int segmentCount
	) {
		segmentCount = std::max(segmentCount, 16);

		std::vector<Vec2> points;
		points.reserve(segmentCount + 1);

		for (int i = 0; i <= segmentCount; i++) {
			double angle =
				sketchMeshTwoPi * (double)i / (double)segmentCount;
			points.push_back(sketchPointOnCircle(center, radius, angle));
		}

		return points;
	}

	std::vector<Vec2> sampleSketchArc(
		Vec2 center,
		double radius,
		double startAngle,
		double endAngle,
		int segmentCount
	) {
		segmentCount = std::max(segmentCount, 2);

		double start = normalizeSketchAngle(startAngle);
		double span = positiveSketchAngleSpan(startAngle, endAngle);

		std::vector<Vec2> points;
		points.reserve(segmentCount + 1);

		for (int i = 0; i <= segmentCount; i++) {
			double t = (double)i / (double)segmentCount;
			points.push_back(
				sketchPointOnCircle(center, radius, start + span * t)
			);
		}

		return points;
	}

	double polygonSignedArea(const std::vector<Vec2>& points) {
		if (points.size() < 3) {
			return 0.0;
		}

		double area = 0.0;
		for (std::size_t i = 0; i < points.size(); i++) {
			const Vec2& a = points[i];
			const Vec2& b = points[(i + 1) % points.size()];
			area += a.z * b.r - b.z * a.r;
		}

		return 0.5 * area;
	}

	void appendDraft(
		std::vector<SketchSegmentDraft>& drafts,
		std::vector<Vec2> points,
		SketchEntityType type,
		int entityID,
		int edgeIndex,
		double startT = 0.0,
		double endT = 1.0
	) {
		if (points.size() < 2) {
			return;
		}

		if (pathLength(points) <= 1e-12) {
			return;
		}

		SketchSegmentDraft draft{};
		draft.controlPoints = std::move(points);
		draft.sourceType = type;
		draft.entityID = entityID;
		draft.edgeIndex = edgeIndex;
		draft.startT = startT;
		draft.endT = endT;

		drafts.push_back(std::move(draft));
	}

	bool reconstructSketchLoop(
		const std::vector<SketchSegmentDraft>& drafts,
		const std::vector<int>& component,
		double tol,
		std::vector<Vec2>& orderedPoints
	) {
		if (component.empty()) {
			return false;
		}

		std::vector<int> remaining = component;
		int firstIndex = remaining.back();
		remaining.pop_back();

		orderedPoints = drafts[firstIndex].controlPoints;

		while (!remaining.empty()) {
			Vec2 currentEnd = orderedPoints.back();

			auto matchIt = std::find_if(
				remaining.begin(),
				remaining.end(),
				[&](int draftIndex) {
					const auto& points = drafts[draftIndex].controlPoints;
					return sketchPointsMatch(points.front(), currentEnd, tol) ||
						sketchPointsMatch(points.back(), currentEnd, tol);
				}
			);

			if (matchIt == remaining.end()) {
				return false;
			}

			std::vector<Vec2> points = drafts[*matchIt].controlPoints;
			if (sketchPointsMatch(points.back(), currentEnd, tol)) {
				std::reverse(points.begin(), points.end());
			}

			orderedPoints.insert(
				orderedPoints.end(),
				points.begin() + 1,
				points.end()
			);

			remaining.erase(matchIt);
		}

		return sketchPointsMatch(orderedPoints.front(), orderedPoints.back(), tol);
	}

	std::vector<SketchLoopDraft> findSketchLoops(
		const std::vector<SketchSegmentDraft>& drafts,
		double tol
	) {
		std::vector<SketchLoopDraft> loops;
		std::vector<bool> consumed(drafts.size(), false);

		std::unordered_map<PointKey, std::vector<int>, PointKeyHash> endpointMap;

		for (int i = 0; i < (int)drafts.size(); i++) {
			const auto& points = drafts[i].controlPoints;

			if (sketchPointsMatch(points.front(), points.back(), tol)) {
				SketchLoopDraft loop{};
				loop.segmentIndices.push_back(i);
				loop.orderedPoints = points;
				loop.area = std::abs(polygonSignedArea(loop.orderedPoints));

				if (loop.area > 1e-18) {
					loops.push_back(loop);
					consumed[i] = true;
				}

				continue;
			}

			endpointMap[makePointKey(points.front(), tol)].push_back(i);
			endpointMap[makePointKey(points.back(), tol)].push_back(i);
		}

		std::vector<bool> visited(drafts.size(), false);

		for (int i = 0; i < (int)drafts.size(); i++) {
			if (consumed[i] || visited[i]) {
				continue;
			}

			std::vector<int> component;
			std::queue<int> pending;
			pending.push(i);
			visited[i] = true;

			while (!pending.empty()) {
				int current = pending.front();
				pending.pop();
				component.push_back(current);

				const auto& points = drafts[current].controlPoints;
				PointKey endpoints[2] = {
					makePointKey(points.front(), tol),
					makePointKey(points.back(), tol)
				};

				for (const PointKey& endpoint : endpoints) {
					auto it = endpointMap.find(endpoint);
					if (it == endpointMap.end()) {
						continue;
					}

					for (int next : it->second) {
						if (!visited[next] && !consumed[next]) {
							visited[next] = true;
							pending.push(next);
						}
					}
				}
			}

			std::unordered_map<PointKey, int, PointKeyHash> degree;
			for (int draftIndex : component) {
				const auto& points = drafts[draftIndex].controlPoints;
				degree[makePointKey(points.front(), tol)]++;
				degree[makePointKey(points.back(), tol)]++;
			}

			bool closed = !degree.empty();
			for (const auto& [_, count] : degree) {
				if (count != 2) {
					closed = false;
					break;
				}
			}

			if (!closed) {
				continue;
			}

			SketchLoopDraft loop{};
			loop.segmentIndices = component;
			if (!reconstructSketchLoop(drafts, component, tol, loop.orderedPoints)) {
				continue;
			}

			loop.area = std::abs(polygonSignedArea(loop.orderedPoints));
			if (loop.area > 1e-18) {
				loops.push_back(std::move(loop));
			}
		}

		return loops;
	}

	EdgeOrient inferControlPathOrientation(
		const std::vector<Vec2>& points,
		double tol
	) {
		bool hasHorizontal = false;
		bool hasVertical = false;
		bool hasOther = false;

		for (int i = 0; i < (int)points.size() - 1; i++) {
			Vec2 a = points[i];
			Vec2 b = points[i + 1];

			double dz = b.z - a.z;
			double dr = b.r - a.r;
			double length2 = dz * dz + dr * dr;

			if (length2 <= tol * tol) {
				continue;
			}

			if (std::abs(dr) <= tol) {
				hasHorizontal = true;
			}
			else if (std::abs(dz) <= tol) {
				hasVertical = true;
			}
			else {
				hasOther = true;
			}
		}

		if (hasOther || (hasHorizontal && hasVertical)) {
			return EdgeOrient::Both;
		}

		if (hasVertical) {
			return EdgeOrient::Vertical;
		}

		return EdgeOrient::Horizontal;
	}

	EdgeOrient orientationFromFlags(
		bool hasHorizontal,
		bool hasVertical,
		bool hasOther
	) {
		if (hasOther || (hasHorizontal && hasVertical)) {
			return EdgeOrient::Both;
		}

		if (hasVertical) {
			return EdgeOrient::Vertical;
		}

		return EdgeOrient::Horizontal;
	}
}

bool Mesh::hasDomainBoundarySegments() const {
	for (const BoundarySegment& seg : boundarySegments) {
		if (seg.source == BoundarySource::Domain) {
			return true;
		}
	}
	return false;
}

int Mesh::createObstacleBoundaryGroup(const std::string& name) {
	BoundarySegmentGroup obstacle{};

	obstacle.id = getAvailableBoundaryGroupID();
	obstacle.name = name;
	obstacle.type = BoundaryType::WALL;

	std::snprintf(
		obstacle.nameBuffer,
		sizeof(obstacle.nameBuffer),
		"%s",
		obstacle.name.c_str()
	);

	boundaryGroups.push_back(obstacle);

	return obstacle.id;
}

void Mesh::clearUnstructuredGeometry() {
	unstructuredPoints.clear();
	unstructuredTriangles.clear();

	boundaryVertices.clear();
	boundaryEdges.clear();
	boundarySegments.clear();
	boundaryGroups.clear();

	selectedBoundaryIDs.clear();
	highlightedBoundarySegmentIDs.clear();

	vertices.clear();
	indices.clear();
	gridLineVertices.clear();
}

void Mesh::initializeUnstructuredDomain(
	int nzPoints,
	int nrPoints
) {
	currentMeshType = MeshType::Unstructured;

	clearUnstructuredGeometry();

	// ----------------------------
	// 1. Create base point cloud
	// ----------------------------
	for (int i = 0; i < nrPoints; i++) {
		double r = g.R * static_cast<double>(i) /
			static_cast<double>(nrPoints - 1);

		for (int j = 0; j < nzPoints; j++) {
			double z = g.L * static_cast<double>(j) /
				static_cast<double>(nzPoints - 1);

			unstructuredPoints.push_back({ z, r });
		}
	}

	// ----------------------------
	// 2. Create boundary vertices
	// For this initial grid:
	// boundaryVertexID == pointID
	// ----------------------------
	for (int n = 0; n < static_cast<int>(unstructuredPoints.size()); n++) {
		BoundaryVertex v{};
		v.id = n;
		v.pointID = n;
		v.pos = unstructuredPoints[n];
		v.hasGridVertex = false;

		boundaryVertices.push_back(v);
	}

	// ----------------------------
	// 3. Add four unnamed side segments
	// ----------------------------
	createDefaultUnstructuredDomainBoundarySegments(
		nzPoints,
		nrPoints
	);
}

void Mesh::createDefaultUnstructuredDomainBoundarySegments(
	int nzPoints,
	int nrPoints
) {

	if (hasDomainBoundarySegments()) {
		return;
	}

	auto pointID = [&](int i, int j) {
		return i * nzPoints + j;
		};

	// r = 0 side
	std::vector<int> bottomVertices;
	for (int j = 0; j < nzPoints; j++) {
		bottomVertices.push_back(pointID(0, j));
	}

	addBoundarySegmentFromVertices(
		bottomVertices,
		BoundarySource::Domain
	);

	// r = R side
	std::vector<int> topVertices;
	for (int j = 0; j < nzPoints; j++) {
		topVertices.push_back(pointID(nrPoints - 1, j));
	}

	addBoundarySegmentFromVertices(
		topVertices,
		BoundarySource::Domain
	);

	// z = 0 side
	std::vector<int> leftVertices;
	for (int i = 0; i < nrPoints; i++) {
		leftVertices.push_back(pointID(i, 0));
	}

	addBoundarySegmentFromVertices(
		leftVertices,
		BoundarySource::Domain
	);

	// z = L side
	std::vector<int> rightVertices;
	for (int i = 0; i < nrPoints; i++) {
		rightVertices.push_back(pointID(i, nzPoints - 1));
	}

	addBoundarySegmentFromVertices(
		rightVertices,
		BoundarySource::Domain
	);
}

int Mesh::addBoundarySegmentFromVertices(
	const std::vector<int>& vertexIDs,
	BoundarySource source
) {
	if (vertexIDs.size() < 2) {
		return -1;
	}

	int segmentID = (int)(boundarySegments.size());

	BoundarySegment segment{};
	segment.id = segmentID;
	segment.groupID = -1;
	segment.loopID = -1;
	segment.source = source;

	for (int vertexID : vertexIDs) {
		if (vertexID < 0 ||
			vertexID >= (int)(boundaryVertices.size())) {
			continue;
		}

		segment.controlPoints.push_back(
			boundaryVertices[vertexID].pos
		);
	}

	if (segment.controlPoints.size() < 2) {
		return -1;
	}

	for (int k = 0; k < (int)(vertexIDs.size()) - 1; k++) {
		int v0 = vertexIDs[k];
		int v1 = vertexIDs[k + 1];

		if (v0 < 0 || v1 < 0) {
			continue;
		}

		if (v0 >= (int)(boundaryVertices.size()) ||
			v1 >= (int)(boundaryVertices.size())) {
			continue;
		}

		int edgeID = (int)(boundaryEdges.size());

		BoundaryEdge edge{};
		edge.id = edgeID;
		edge.v0 = v0;
		edge.v1 = v1;
		edge.segmentID = segmentID;
		edge.groupID = -1;
		edge.source = source;
		edge.hasMeshEdge = false;

		boundaryEdges.push_back(edge);
		segment.edgeIDs.push_back(edgeID);
	}

	if (segment.edgeIDs.empty()) {
		return -1;
	}

	boundarySegments.push_back(segment);

	return segmentID;
}

void Mesh::addCircularObstacle(
	Vec2 center,
	double radius,
	int nObstaclePoints
) {
	nObstaclePoints = std::max(nObstaclePoints, 8);

	int segmentID = (int)(boundarySegments.size());

	BoundarySegment segment{};
	segment.id = segmentID;
	segment.groupID = -1;
	segment.loopID = getAvailableLoopID();
	segment.source = BoundarySource::Obstacle;

	segment.sizing.enabled = true;
	segment.sizing.targetSpacing = 2.0 * PI * radius / (double)(nObstaclePoints);
	segment.sizing.bias = 1.0;

	std::vector<int> vertexIDs;
	vertexIDs.reserve(nObstaclePoints);

	for (int k = 0; k < nObstaclePoints; k++) {
		double theta =
			2.0 * PI * (double)(k) /
			(double)(nObstaclePoints);

		Vec2 p{};
		p.z = center.z + radius * std::cos(theta);
		p.r = center.r + radius * std::sin(theta);

		segment.controlPoints.push_back(p);

		int vertexID = addUnstructuredBoundaryVertex(p);
		vertexIDs.push_back(vertexID);
	}

	if (!segment.controlPoints.empty()) {
		segment.controlPoints.push_back(segment.controlPoints.front());
	}

	for (int k = 0; k < nObstaclePoints; k++) {
		int v0 = vertexIDs[k];
		int v1 = vertexIDs[(k + 1) % nObstaclePoints];

		if (v0 == v1) {
			continue;
		}

		int edgeID = (int)(boundaryEdges.size());

		BoundaryEdge edge{};
		edge.id = edgeID;
		edge.v0 = v0;
		edge.v1 = v1;
		edge.segmentID = segmentID;
		edge.groupID = -1;
		edge.source = BoundarySource::Obstacle;
		edge.hasMeshEdge = false;

		boundaryEdges.push_back(edge);
		segment.edgeIDs.push_back(edgeID);
	}

	boundarySegments.push_back(segment);
}

bool Mesh::convertSketchToUnstructuredMesh(const SketchModel& sketch) {
	std::vector<SketchSegmentDraft> drafts;

	double modelScale = std::max(g.L, g.R);
	modelScale = std::max(modelScale, 1.0);

	double tol = modelScale * 1e-9;
	double curveSpacing = std::max(modelScale / 80.0, tol * 10.0);

	for (const SketchLine& line : sketch.lines) {
		if (line.construction) {
			continue;
		}

		const SketchPoint* p0 = sketch.findPoint(line.p0);
		const SketchPoint* p1 = sketch.findPoint(line.p1);

		if (!p0 || !p1) {
			continue;
		}

		appendDraft(
			drafts,
			{ p0->pos, p1->pos },
			SketchEntityType::Line,
			line.id,
			-1
		);
	}

	for (const SketchRectangle& rect : sketch.rectangles) {
		if (rect.construction) {
			continue;
		}

		Vec2 corners[4] = {
			Vec2{ rect.min.z, rect.min.r },
			Vec2{ rect.max.z, rect.min.r },
			Vec2{ rect.max.z, rect.max.r },
			Vec2{ rect.min.z, rect.max.r }
		};

		for (int edge = 0; edge < 4; edge++) {
			appendDraft(
				drafts,
				{ corners[edge], corners[(edge + 1) % 4] },
				SketchEntityType::Rectangle,
				rect.id,
				edge
			);
		}
	}

	for (const SketchCircle& circle : sketch.circles) {
		if (circle.construction || circle.radius <= tol) {
			continue;
		}

		int segments = std::max(
			nseg,
			curveSampleCount(sketchMeshTwoPi * circle.radius, curveSpacing, 32)
		);

		appendDraft(
			drafts,
			sampleSketchCircle(circle.center, circle.radius, segments),
			SketchEntityType::Circle,
			circle.id,
			-1
		);
	}

	for (const SketchArc& arc : sketch.arcs) {
		if (arc.construction || arc.radius <= tol) {
			continue;
		}

		double span = positiveSketchAngleSpan(arc.startAngle, arc.endAngle);
		if (span <= 1e-9) {
			continue;
		}

		int segments = curveSampleCount(arc.radius * span, curveSpacing, 8);

		appendDraft(
			drafts,
			sampleSketchArc(
				arc.center,
				arc.radius,
				arc.startAngle,
				arc.endAngle,
				segments
			),
			SketchEntityType::Arc,
			arc.id,
			-1
		);
	}

	std::vector<SketchLoopDraft> loops = findSketchLoops(drafts, tol);

	if (loops.empty()) {
		if (console) {
			console->addLine(
				"Sketch to mesh failed: no closed sketch loops were found."
			);
		}
		return false;
	}

	auto domainIt = std::max_element(
		loops.begin(),
		loops.end(),
		[](const SketchLoopDraft& a, const SketchLoopDraft& b) {
			return a.area < b.area;
		}
	);

	int domainLoopIndex = (int)std::distance(loops.begin(), domainIt);

	// Snapshot the existing boundary groups together with the geometry of the
	// segments they are assigned to. Boundary naming is done in the Mesh tab,
	// so these groups must survive re-conversion even though the segments are
	// rebuilt (with new IDs) from the sketch.
	struct PreservedBoundaryGroup {
		BoundarySegmentGroup group;
		std::vector<Vec2> segmentSamplePoints;
	};

	std::vector<PreservedBoundaryGroup> preservedGroups;

	for (const BoundarySegmentGroup& existing : boundaryGroups) {
		PreservedBoundaryGroup preserved;
		preserved.group = existing;

		for (int segmentID : existing.segmentIDs) {
			const BoundarySegment* segment = getBoundarySegmentByID(segmentID);
			if (segment && segment->controlPoints.size() >= 2) {
				const std::vector<Vec2>& cp = segment->controlPoints;
				size_t mid = (cp.size() - 1) / 2;

				// Interior point of the segment (never a junction), so the
				// match below is unambiguous.
				Vec2 sample{
					0.5 * (cp[mid].z + cp[mid + 1].z),
					0.5 * (cp[mid].r + cp[mid + 1].r)
				};

				preserved.segmentSamplePoints.push_back(sample);
			}
		}

		preservedGroups.push_back(std::move(preserved));
	}

	currentMeshType = MeshType::Unstructured;
	clearUnstructuredGeometry();
	nextLoopID = 0;
	nextGroupID = 0;

	std::vector<int> segmentIDByDraft(drafts.size(), -1);

	for (int loopIndex = 0; loopIndex < (int)loops.size(); loopIndex++) {
		const SketchLoopDraft& loop = loops[loopIndex];

		bool isDomainLoop = loopIndex == domainLoopIndex;
		BoundarySource source =
			isDomainLoop ? BoundarySource::Domain : BoundarySource::Obstacle;
		int loopID = isDomainLoop ? -1 : getAvailableLoopID();

		for (int draftIndex : loop.segmentIndices) {
			const SketchSegmentDraft& draft = drafts[draftIndex];

			BoundarySegment segment{};
			segment.id = (int)boundarySegments.size();
			segment.controlPoints = draft.controlPoints;
			segment.groupID = -1;
			segment.loopID = loopID;
			segment.source = source;

			segment.sizing.enabled = true;
			segment.sizing.mode = BoundarySizingMode::EdgeCount;
			segment.sizing.edgeCount =
				std::max(1, (int)segment.controlPoints.size() - 1);
			segment.sizing.bias = 1.0;

			segmentIDByDraft[draftIndex] = segment.id;
			boundarySegments.push_back(std::move(segment));
		}
	}

	// Re-create the preserved boundary groups, re-mapping each one onto the
	// freshly rebuilt segments by matching segment geometry. Segment IDs are
	// not stable across re-conversion, but the geometry is.
	double groupMatchTol = modelScale * 1e-6;

	for (PreservedBoundaryGroup& preserved : preservedGroups) {
		BoundarySegmentGroup group = preserved.group;
		group.segmentIDs.clear();
		group.totalLength = 0.0f;

		std::unordered_set<int> matchedSegmentIDs;

		for (const Vec2& samplePoint : preserved.segmentSamplePoints) {
			int bestSegmentID = -1;
			double bestDist = groupMatchTol;

			for (const BoundarySegment& segment : boundarySegments) {
				const std::vector<Vec2>& cp = segment.controlPoints;

				for (size_t k = 0; k + 1 < cp.size(); k++) {
					double d = distancePointToSegment(samplePoint, cp[k], cp[k + 1]);
					if (d < bestDist) {
						bestDist = d;
						bestSegmentID = segment.id;
					}
				}
			}

			if (bestSegmentID >= 0) {
				matchedSegmentIDs.insert(bestSegmentID);
			}
		}

		if (matchedSegmentIDs.empty()) {
			continue;
		}

		group.segmentIDs.assign(
			matchedSegmentIDs.begin(),
			matchedSegmentIDs.end()
		);

		std::sort(group.segmentIDs.begin(), group.segmentIDs.end());

		std::snprintf(
			group.nameBuffer,
			sizeof(group.nameBuffer),
			"%s",
			group.name.c_str()
		);

		bool hasHorizontal = false;
		bool hasVertical = false;
		bool hasOther = false;
		double orientationTol = std::max(std::max(g.L, g.R), 1.0) * 1e-8;

		for (int segmentID : group.segmentIDs) {
			BoundarySegment* segment = getBoundarySegmentByID(segmentID);
			if (!segment) {
				continue;
			}

			segment->groupID = group.id;

			EdgeOrient orient =
				inferControlPathOrientation(segment->controlPoints, orientationTol);

			if (orient == EdgeOrient::Horizontal) {
				hasHorizontal = true;
			}
			else if (orient == EdgeOrient::Vertical) {
				hasVertical = true;
			}
			else {
				hasOther = true;
			}
		}

		group.includesOrientation =
			orientationFromFlags(hasHorizontal, hasVertical, hasOther);

		boundaryGroups.push_back(std::move(group));
	}

	if (!domainIt->orderedPoints.empty()) {
		double maxZ = 0.0;
		double maxR = 0.0;

		for (const Vec2& p : domainIt->orderedPoints) {
			maxZ = std::max(maxZ, p.z);
			maxR = std::max(maxR, p.r);
		}

		if (maxZ > 1e-12) {
			g.L = maxZ;
		}

		if (maxR > 1e-12) {
			g.R = maxR;
		}
	}

	rebuildBoundaryDiscretization();

	for (BoundarySegmentGroup& group : boundaryGroups) {
		group.totalLength = 0.0f;

		for (int segmentID : group.segmentIDs) {
			BoundarySegment* segment = getBoundarySegmentByID(segmentID);
			if (!segment) {
				continue;
			}

			group.totalLength += (float)pathLength(segment->controlPoints);
		}
	}

	isReady = false;

	if (console) {
		console->addCompletionMessage(
			"Converted sketch loops into mesh boundary segments"
		);
	}

	return true;
}

bool Mesh::convertSketchToStructuredMesh(const SketchModel& sketch) {
	const bool preserveStructuredGroups =
		currentMeshType == MeshType::Structured;
	std::vector<BoundarySegmentGroup> preservedStructuredGroups;

	if (preserveStructuredGroups) {
		preservedStructuredGroups = boundaryGroups;
	}

	// Reuse the shared sketch -> boundary pipeline. It builds the boundary
	// segments / vertices / edges and boundary groups (used for BCs) and sets
	// g.L / g.R from the sketch's extent. It flips currentMeshType to
	// Unstructured as a side effect, so restore it afterwards.
	if (!convertSketchToUnstructuredMesh(sketch)) {
		return false;
	}

	currentMeshType = MeshType::Structured;

	// Build the rectangular grid spanning the sketch's extent.
	createGrid();

	const int nr = g.nr;
	const int nz = g.nz;

	// Rasterize: every cell whose center is outside the sketched domain (or
	// inside an obstacle) becomes solid. The solver consumes this through
	// obstacleIndices -> createActiveCell -> activeCell.
	g.obstacleIndices.clear();
	g.activeCell.assign(static_cast<size_t>(nr) * nz, 1);

	for (int i = 0; i < nr; i++) {
		for (int j = 0; j < nz; j++) {
			Vec2 center{ g.z[j], g.r[i] };

			if (!pointInsideDomain(center)) {
				int n = i * nz + j;
				g.activeCell[n] = 0;
				g.obstacleIndices.insert(n);
			}
		}
	}

	// The Mesh Inspector reconstructs the selectable wall boundary from these
	// fluid/solid interface faces. Without this the rasterized walls (any wall not
	// on the outer grid border) can't be shown or picked in the inspector.
	rebuildSelectableObstacleEdges();

	if (preserveStructuredGroups) {
		auto isFluid = [&](int i, int j) {
			if (i < 0 || i >= nr || j < 0 || j >= nz) {
				return false;
			}

			return g.activeCell[i * nz + j] != 0;
			};

		auto isCurrentBoundaryEdge = [&](const MeshEdge& edge) {
			if (edge.orient == EdgeOrient::Horizontal) {
				if (edge.j < 0 || edge.j >= nz || edge.i < 0 || edge.i > nr) {
					return false;
				}

				if (edge.i == 0) {
					return isFluid(0, edge.j);
				}

				if (edge.i == nr) {
					return isFluid(nr - 1, edge.j);
				}

				return isFluid(edge.i - 1, edge.j) != isFluid(edge.i, edge.j);
			}

			if (edge.i < 0 || edge.i >= nr || edge.j < 0 || edge.j > nz) {
				return false;
			}

			if (edge.j == 0) {
				return isFluid(edge.i, 0);
			}

			if (edge.j == nz) {
				return isFluid(edge.i, nz - 1);
			}

			return isFluid(edge.i, edge.j - 1) != isFluid(edge.i, edge.j);
			};

		boundaryGroups.clear();

		for (BoundarySegmentGroup group : preservedStructuredGroups) {
			group.segmentIDs.clear();
			group.totalLength = 0.0f;

			group.edges.erase(
				std::remove_if(
					group.edges.begin(),
					group.edges.end(),
					[&](const MeshEdge& edge) {
						return !isCurrentBoundaryEdge(edge);
					}
				),
				group.edges.end()
			);

			if (!group.edges.empty()) {
				boundaryGroups.push_back(std::move(group));
			}
		}
	}

	isReady = true;

	if (console) {
		console->addCompletionMessage(
			"Rasterized sketch onto structured grid"
		);
	}

	return true;
}

void Mesh::rebuildSelectableObstacleEdges() {
	selectableOuterEdges.clear();

	const int nr = g.nr;
	const int nz = g.nz;

	if (static_cast<int>(g.activeCell.size()) != nr * nz) {
		return;
	}

	auto isFluid = [&](int i, int j) {
		if (i < 0 || i >= nr || j < 0 || j >= nz) {
			return false;
		}
		return g.activeCell[i * nz + j] != 0;
	};

	// Interior axial faces (constant z) between cells (i, jFace - 1) and (i, jFace).
	// MeshEdge convention matches makeAxialEdge()/buildDomainBoundaryEdges().
	for (int i = 0; i < nr; i++) {
		for (int jFace = 1; jFace < nz; jFace++) {
			if (isFluid(i, jFace - 1) != isFluid(i, jFace)) {
				selectableOuterEdges.insert(
					MeshEdge{ EdgeOrient::Vertical, i, jFace }
				);
			}
		}
	}

	// Interior radial faces (constant r) between cells (iFace - 1, j) and (iFace, j).
	for (int iFace = 1; iFace < nr; iFace++) {
		for (int j = 0; j < nz; j++) {
			if (isFluid(iFace - 1, j) != isFluid(iFace, j)) {
				selectableOuterEdges.insert(
					MeshEdge{ EdgeOrient::Horizontal, iFace, j }
				);
			}
		}
	}
}

bool Mesh::pointInsideDomain(const Vec2& p) const {

	// Even-odd ray cast along +z at height p.r against the discretized boundary
	// loops (domain outline plus obstacle loops). Holes are handled naturally:
	// a point inside an obstacle ends up with an even crossing count.
	bool inside = false;
	const int nv = static_cast<int>(boundaryVertices.size());

	for (const BoundaryEdge& e : boundaryEdges) {
		if (e.v0 < 0 || e.v1 < 0 || e.v0 >= nv || e.v1 >= nv) {
			continue;
		}

		const Vec2& a = boundaryVertices[e.v0].pos;
		const Vec2& b = boundaryVertices[e.v1].pos;

		bool straddles = (a.r > p.r) != (b.r > p.r);
		if (!straddles) {
			continue;
		}

		double zCross = a.z + (p.r - a.r) * (b.z - a.z) / (b.r - a.r);

		if (p.z < zCross) {
			inside = !inside;
		}
	}

	return inside;
}

void Mesh::runGmshTriangulation() {
	unstructuredTriangles.clear();

	gmsh::initialize();
	gmsh::clear();

	gmsh::model::add("AxiSimMesh");

	std::unordered_map<int, int> pointTagByBoundaryVertex;
	double defaultMeshSize = std::min(g.L, g.R) / 10.0;

	if (defaultMeshSize <= 1e-30) {
		defaultMeshSize = std::max(g.L, g.R) / 20.0;
	}

	if (defaultMeshSize <= 1e-30) {
		defaultMeshSize = 1.0;
	}

	std::vector<double> boundaryVertexSize(
		boundaryVertices.size(),
		defaultMeshSize
	);

	// Seed each boundary vertex's target mesh size from the length of the
	// boundary edges the discretization actually produced. This makes the
	// interior grade to whatever the user prescribed on each boundary (edge
	// count OR target spacing), rather than only honoring target spacing.
	for (const BoundaryEdge& edge : boundaryEdges) {
		if (edge.v0 < 0 || edge.v1 < 0) {
			continue;
		}

		if (edge.v0 >= static_cast<int>(boundaryVertexSize.size()) ||
			edge.v1 >= static_cast<int>(boundaryVertexSize.size())) {
			continue;
		}

		const Vec2& p0 = boundaryVertices[edge.v0].pos;
		const Vec2& p1 = boundaryVertices[edge.v1].pos;

		double dz = p1.z - p0.z;
		double dr = p1.r - p0.r;
		double len = std::sqrt(dz * dz + dr * dr);

		if (len <= 1e-30) {
			continue;
		}

		boundaryVertexSize[edge.v0] = std::min(boundaryVertexSize[edge.v0], len);
		boundaryVertexSize[edge.v1] = std::min(boundaryVertexSize[edge.v1], len);
	}

	for (const BoundaryVertex& v : boundaryVertices) {
		int tag = v.id + 1;
		double lc = defaultMeshSize;

		if (v.id >= 0 && v.id < static_cast<int>(boundaryVertexSize.size())) {
			lc = boundaryVertexSize[v.id];
		}

		gmsh::model::geo::addPoint(v.pos.z, v.pos.r, 0.0, lc, tag);
		pointTagByBoundaryVertex[v.id] = tag;
	}

	std::vector<int> outerLines;
	std::vector<int> holeLoops;
	std::unordered_map<int, int> segmentLoopIDByID;
	std::unordered_map<int, std::vector<int>> holeLinesByLoopID;

	for (const BoundarySegment& segment : boundarySegments) {
		segmentLoopIDByID[segment.id] = segment.loopID;
	}

	for (const BoundaryEdge& edge : boundaryEdges) {
		int lineTag = edge.id + 1;

		int p0 = pointTagByBoundaryVertex[edge.v0];
		int p1 = pointTagByBoundaryVertex[edge.v1];

		gmsh::model::geo::addLine(p0, p1, lineTag);

		// Keep exactly the boundary discretization we prescribed: one mesh
		// element per boundary edge. Without this gmsh re-subdivides each line
		// to the background mesh size, so a wall set to 20 edges comes back
		// with 60+ boundary faces.
		gmsh::model::geo::mesh::setTransfiniteCurve(lineTag, 2);

		if (edge.source == BoundarySource::Domain) {
			outerLines.push_back(lineTag);
		}
		else {
			auto loopIt = segmentLoopIDByID.find(edge.segmentID);

			if (loopIt != segmentLoopIDByID.end() && loopIt->second >= 0) {
				holeLinesByLoopID[loopIt->second].push_back(lineTag);
			}
		}
	}

	int outerLoop = gmsh::model::geo::addCurveLoop(outerLines, -1, true);

	for (const auto& [loopID, holeLines] : holeLinesByLoopID) {
		if (holeLines.empty()) {
			continue;
		}

		int holeLoop = gmsh::model::geo::addCurveLoop(holeLines, -1, true);
		holeLoops.push_back(holeLoop);
	}

	std::vector<int> surfaceLoops = { outerLoop };
	surfaceLoops.insert(surfaceLoops.end(), holeLoops.begin(), holeLoops.end());

	gmsh::model::geo::addPlaneSurface(surfaceLoops, 1);
	gmsh::model::geo::synchronize();

	gmsh::option::setNumber("Mesh.Algorithm", 6); // Frontal-Delaunay
	applyRegionOfInfluenceFields(defaultMeshSize);

	gmsh::model::mesh::generate(2);

	std::vector<std::size_t> nodeTags;
	std::vector<double> coords;
	std::vector<double> params;

	gmsh::model::mesh::getNodes(nodeTags, coords, params);

	unstructuredPoints.clear();

	std::unordered_map<std::size_t, int> pointIDByNodeTag;

	for (std::size_t n = 0; n < nodeTags.size(); n++) {
		Vec2 p{};
		p.z = coords[3 * n + 0];
		p.r = coords[3 * n + 1];

		int pointID = static_cast<int>(unstructuredPoints.size());
		unstructuredPoints.push_back(p);

		pointIDByNodeTag[nodeTags[n]] = pointID;
	}

	// Map each boundary vertex onto its gmsh mesh node by position.
	//
	// gmsh mesh-node tags are NOT guaranteed to match the geometry-point
	// entity tags we assigned, so matching boundary vertices by tag silently
	// fails and leaves every boundary vertex pointing at a stale node. That
	// breaks the edge-key lookup in createUnstructuredMesh, so no boundary
	// face receives a group ID and no inlet/wall BC is ever applied. Matching
	// by coordinate is robust to gmsh's node numbering (the geometry-point
	// node sits at exactly the coordinates we provided).
	double remapScale = std::max(std::max(g.L, g.R), 1.0);
	double remapTol = 1e-9 * remapScale;

	std::unordered_map<PointKey, int, PointKeyHash> nodeByPosition;
	nodeByPosition.reserve(unstructuredPoints.size());

	for (int pid = 0; pid < static_cast<int>(unstructuredPoints.size()); pid++) {
		nodeByPosition[makePointKey(unstructuredPoints[pid], remapTol)] = pid;
	}

	for (BoundaryVertex& vertex : boundaryVertices) {
		auto nodeIt = nodeByPosition.find(makePointKey(vertex.pos, remapTol));

		if (nodeIt != nodeByPosition.end()) {
			vertex.pointID = nodeIt->second;
		}
	}

	std::vector<std::size_t> elemTags;
	std::vector<std::size_t> triNodeTags;

	gmsh::model::mesh::getElementsByType(2, elemTags, triNodeTags);

	for (std::size_t k = 0; k + 2 < triNodeTags.size(); k += 3) {
		Triangle tri{};
		tri.v0 = pointIDByNodeTag[triNodeTags[k + 0]];
		tri.v1 = pointIDByNodeTag[triNodeTags[k + 1]];
		tri.v2 = pointIDByNodeTag[triNodeTags[k + 2]];

		unstructuredTriangles.push_back(tri);
	}

	gmsh::finalize();
}

void Mesh::applyRegionOfInfluenceFields(double defaultMeshSize) {
	std::vector<double> fieldTags;

	for (const MeshRegionOfInfluence& region : regionsOfInfluence) {
		if (!region.enabled ||
			region.targetSpacing <= 1e-30) {
			continue;
		}

		int fieldTag = -1;

		if (region.shape == MeshRegionShape::Rectangle) {
			double zMin = std::min(region.min.z, region.max.z);
			double zMax = std::max(region.min.z, region.max.z);
			double rMin = std::min(region.min.r, region.max.r);
			double rMax = std::max(region.min.r, region.max.r);

			if (zMax - zMin <= 1e-30 || rMax - rMin <= 1e-30) {
				continue;
			}

			fieldTag = gmsh::model::mesh::field::add("Box");
			gmsh::model::mesh::field::setNumber(fieldTag, "XMin", zMin);
			gmsh::model::mesh::field::setNumber(fieldTag, "XMax", zMax);
			gmsh::model::mesh::field::setNumber(fieldTag, "YMin", rMin);
			gmsh::model::mesh::field::setNumber(fieldTag, "YMax", rMax);
			gmsh::model::mesh::field::setNumber(fieldTag, "ZMin", -1.0);
			gmsh::model::mesh::field::setNumber(fieldTag, "ZMax", 1.0);
		}
		else {
			if (region.radius <= 1e-30) {
				continue;
			}

			fieldTag = gmsh::model::mesh::field::add("Ball");
			gmsh::model::mesh::field::setNumber(fieldTag, "XCenter", region.center.z);
			gmsh::model::mesh::field::setNumber(fieldTag, "YCenter", region.center.r);
			gmsh::model::mesh::field::setNumber(fieldTag, "ZCenter", 0.0);
			gmsh::model::mesh::field::setNumber(fieldTag, "Radius", region.radius);
		}

		gmsh::model::mesh::field::setNumber(fieldTag, "VIn", region.targetSpacing);

		double outsideSpacing = region.outsideSpacing;
		if (outsideSpacing <= 1e-30) {
			outsideSpacing = defaultMeshSize;
		}

		gmsh::model::mesh::field::setNumber(fieldTag, "VOut", outsideSpacing);

		if (region.transitionThickness > 1e-30) {
			gmsh::model::mesh::field::setNumber(
				fieldTag,
				"Thickness",
				region.transitionThickness
			);
		}

		fieldTags.push_back(static_cast<double>(fieldTag));
	}

	if (fieldTags.empty()) {
		return;
	}

	int backgroundFieldTag = static_cast<int>(fieldTags.front());

	if (fieldTags.size() > 1) {
		backgroundFieldTag = gmsh::model::mesh::field::add("Min");
		gmsh::model::mesh::field::setNumbers(
			backgroundFieldTag,
			"FieldsList",
			fieldTags
		);
	}

	gmsh::model::mesh::field::setAsBackgroundMesh(backgroundFieldTag);
}

float Mesh::displayZ(double z) const {
	return static_cast<float>(2.0 * z / g.L - 1.0);
}

float Mesh::displayR(double r) const {
	return static_cast<float>(2.0 * r / g.R - 1.0);
}

inline Vec2 triangleCentroid(Vec2 a, Vec2 b, Vec2 c) {
	return Vec2{
		(a.z + b.z + c.z) / 3.0,
		(a.r + b.r + c.r) / 3.0
	};
}

inline double triangleArea2D(Vec2 a, Vec2 b, Vec2 c) {
	double dz1 = b.z - a.z;
	double dr1 = b.r - a.r;

	double dz2 = c.z - a.z;
	double dr2 = c.r - a.r;

	return 0.5 * std::abs(dz1 * dr2 - dr1 * dz2);
}

inline int cellID(int i, int j, int nz) {
	return i * nz + j;
}

inline double axialAreaFull(double r0, double r1) {
	// Full circular annulus area normal to z direction
	return PI * (r1 * r1 - r0 * r0);
}

inline double radialAreaFull(double r, double dz) {
	// Full cylindrical surface area normal to r direction
	return 2.0 * PI * r * dz;
}

inline double cellVolumeFull(double r0, double r1, double dz) {
	// Full axisymmetric cell volume
	return PI * (r1 * r1 - r0 * r0) * dz;
}

MeshEdge makeAxialEdge(int i, int jFace) {
	MeshEdge edge{};

	edge.i = i;
	edge.j = jFace;
	edge.orient = EdgeOrient::Vertical; // rename to match your code

	return edge;
}

MeshEdge makeRadialEdge(int iFace, int j) {
	MeshEdge edge{};

	edge.i = iFace;
	edge.j = j;
	edge.orient = EdgeOrient::Horizontal; // rename to match your code

	return edge;
}

bool isFluidCell(
	int i,
	int j,
	int nr,
	int nz,
	const std::vector<uint8_t>& activeCell
) {
	if (i < 0 || i >= nr) return false;
	if (j < 0 || j >= nz) return false;

	return activeCell[cellID(i, j, nz)] != 0;
}

std::unordered_map<MeshEdge, int, MeshEdgeHash>
createBoundaryEdgeLookup(const std::vector<BoundarySegmentGroup>& groups) {
	std::unordered_map<MeshEdge, int, MeshEdgeHash> lookup;

	for (const BoundarySegmentGroup& group : groups) {
		for (const MeshEdge& edge : group.edges) {

			auto [it, inserted] = lookup.emplace(edge, group.id);

			if (!inserted) {
				// Same edge was already assigned to another group.
				// You can either overwrite, warn, or keep the first one.
				it->second = group.id;
			}
		}
	}

	return lookup;
}

bool Mesh::isClosedControlPath(const BoundarySegment& seg) const {
	if (seg.controlPoints.size() < 3) {
		return false;
	}

	Vec2 a = seg.controlPoints.front();
	Vec2 b = seg.controlPoints.back();

	double dz = b.z - a.z;
	double dr = b.r - a.r;

	return dz * dz + dr * dr < 1e-24;
}

void Mesh::rebuildBoundaryDiscretization() {
	boundaryVertices.clear();
	boundaryEdges.clear();
	unstructuredPoints.clear();

	std::unordered_map<PointKey, int, PointKeyHash> vertexLookup;

	double scale = std::max(g.L, g.R);
	scale = std::max(scale, 1.0);

	double tol = 1e-10 * scale;

	for (BoundarySegment& seg : boundarySegments) {
		rebuildSegmentDiscretization(seg, vertexLookup, tol);
	}
}

void Mesh::rebuildSegmentDiscretization(
	BoundarySegment& seg,
	std::unordered_map<PointKey, int, PointKeyHash>& vertexLookup,
	double tol
) {
	seg.edgeIDs.clear();

	if (seg.controlPoints.size() < 2) {
		return;
	}

	BoundarySizing sizing = getSizingForSegment(seg);

	double length = pathLength(seg.controlPoints);

	bool closed = isClosedControlPath(seg);


	int nEdges = getNumberOfEdgesForSegment(
		seg,
		sizing,
		length,
		closed
	);



	double bias = sizing.bias;

	if (bias <= 1e-12) {
		bias = 1.0;
	}

	std::vector<int> vertexIDs;
	vertexIDs.reserve(nEdges + 1);

	int nVertices = closed ? nEdges : nEdges + 1;

	for (int k = 0; k < nVertices; k++) {
		double s = (double)(k) / (double)(nEdges);
		double t = biasedT(s, bias);

		Vec2 p = interpolatePath(seg.controlPoints, t);

		PointKey key = makePointKey(p, tol);

		int vertexID = -1;

		auto it = vertexLookup.find(key);

		if (it != vertexLookup.end()) {
			vertexID = it->second;
		}
		else {
			int pointID = (int)(unstructuredPoints.size());
			unstructuredPoints.push_back(p);

			vertexID = (int)(boundaryVertices.size());

			BoundaryVertex bv{};
			bv.id = vertexID;
			bv.pointID = pointID;
			bv.pos = p;
			bv.hasGridVertex = false;

			boundaryVertices.push_back(bv);

			vertexLookup[key] = vertexID;
		}

		vertexIDs.push_back(vertexID);
	}

	int edgeCount = nEdges;

	for (int k = 0; k < edgeCount; k++) {
		int v0 = vertexIDs[k];

		int v1 = closed ?
			vertexIDs[(k + 1) % (int)(vertexIDs.size())] :
			vertexIDs[k + 1];

		if (v0 == v1) {
			continue;
		}

		Vec2 p0 = boundaryVertices[v0].pos;
		Vec2 p1 = boundaryVertices[v1].pos;

		double dz = p1.z - p0.z;
		double dr = p1.r - p0.r;

		if (dz * dz + dr * dr < 1e-24) {
			continue;
		}

		int edgeID = (int)(boundaryEdges.size());

		BoundaryEdge edge{};
		edge.id = edgeID;
		edge.v0 = v0;
		edge.v1 = v1;
		edge.segmentID = seg.id;
		edge.groupID = seg.groupID;
		edge.source = seg.source;
		edge.hasMeshEdge = false;

		boundaryEdges.push_back(edge);
		seg.edgeIDs.push_back(edgeID);
	}
}

int Mesh::getNumberOfEdgesForSegment(
	const BoundarySegment& seg,
	const BoundarySizing& sizing,
	double length,
	bool closed
) const {
	int nEdges = 1;

	if (sizing.enabled) {
		if (sizing.mode == BoundarySizingMode::TargetSpacing) {
			if (sizing.targetSpacing > 1e-30) {
				nEdges = (int)(std::ceil(length / sizing.targetSpacing));
			}
		}
		else if (sizing.mode == BoundarySizingMode::EdgeCount) {
			nEdges = sizing.edgeCount;
		}
	}

	nEdges = std::max(nEdges, 1);

	if (closed) {
		nEdges = std::max(nEdges, 15);
	}

	return nEdges;
}

BoundarySizing Mesh::getSizingForSegment(const BoundarySegment& seg) const {
	if (seg.groupID >= 0) {
		for (const BoundarySegmentGroup& group : boundaryGroups) {
			if (group.id == seg.groupID) {
				if (group.sizing.enabled &&
					group.sizing.mode != BoundarySizingMode::None) {
					return group.sizing;
				}

				break;
			}
		}
	}

	return seg.sizing;
}

std::unordered_set<int> Mesh::getSegmentIDsInSameLoop(int segmentID) const {
	std::unordered_set<int> ids;

	const BoundarySegment* target = nullptr;

	for (const BoundarySegment& seg : boundarySegments) {
		if (seg.id == segmentID) {
			target = &seg;
			break;
		}
	}

	if (!target) {
		return ids;
	}

	// If this segment is not part of a loop, just return itself.
	if (target->loopID < 0) {
		ids.insert(segmentID);
		return ids;
	}

	for (const BoundarySegment& seg : boundarySegments) {
		if (seg.loopID == target->loopID) {
			ids.insert(seg.id);
		}
	}

	return ids;
}

int getBoundaryGroupID(
	const std::unordered_map<MeshEdge, int, MeshEdgeHash>& lookup,
	const MeshEdge& edge
) {
	auto it = lookup.find(edge);

	if (it == lookup.end()) {
		return -1;
	}

	return it->second;
}

void Mesh::generate() {
	Clock::time_point startTime = startTimer();

	if (currentMeshType == MeshType::Structured) {
		createGrid();
		createGridVertices();

		// multiBlock is assembled from geometry in buildStructuredMultiBlock (called
		// from the Generate path). Draw its edges when present, else the plain grid.
		if (isMultiBlock && !multiBlock.blocks.empty())
			createMultiBlockLineVertices(multiBlock);
		else
			createGridLineVertices();

		createCylinderVertices();
	}
	else {
		isMultiBlock = false;

		rebuildBoundaryDiscretization();

		runGmshTriangulation();

		FVMesh fvMesh = createUnstructuredMesh(
			unstructuredPoints,
			unstructuredTriangles,
			boundaryVertices,
			boundaryEdges
		);

		createUnstructuredVertices(
			unstructuredPoints,
			unstructuredTriangles
		);

		createUnstructuredLineVertices(
			unstructuredPoints,
			fvMesh
		);
	}

	console->addCompletionMessage("Completed generating buffers");

	isReady = true;

	float endTime = endTimer(startTime);
	console->addCompletionTime("Mesh", endTime);
}

int Mesh::addUnstructuredBoundaryVertex(Vec2 p) {
	int pointID = static_cast<int>(unstructuredPoints.size());
	unstructuredPoints.push_back(p);

	int vertexID = static_cast<int>(boundaryVertices.size());

	BoundaryVertex bv{};
	bv.id = vertexID;
	bv.pointID = pointID;
	bv.pos = p;
	bv.hasGridVertex = false;

	boundaryVertices.push_back(bv);

	return vertexID;
}


FVMesh Mesh::createUnstructuredMesh(
	const std::vector<Vec2>& points,
	const std::vector<Triangle>& triangles,
	const std::vector<BoundaryVertex>& boundaryVertices,
	const std::vector<BoundaryEdge>& boundaryEdges
) const {
	FVMesh mesh;

	mesh.nr = 0;
	mesh.nz = 0;

	mesh.cells.resize(triangles.size());

	// -------------------------
	// 1. Create cells
	// -------------------------
	for (int c = 0; c < (int)(triangles.size()); c++) {
		const Triangle& tri = triangles[c];

		Vec2 a = points[tri.v0];
		Vec2 b = points[tri.v1];
		Vec2 d = points[tri.v2];

		FVCell& cell = mesh.cells[c];

		cell.center = triangleCentroid(a, b, d);
		cell.area2D = triangleArea2D(a, b, d);
		cell.volume = 2.0 * PI * cell.center.r * cell.area2D;

		cell.active = true;
		cell.solid = false;
		cell.faceIDs.clear();
	}

	// triangle edge -> face ID
	std::unordered_map<EdgeKey, int, EdgeKeyHash> edgeToFace;

	auto addTriangleEdge = [&](int ownerCellID, int v0, int v1) {
		EdgeKey key(v0, v1);

		auto it = edgeToFace.find(key);

		if (it == edgeToFace.end()) {
			FVFace face{};

			face.owner = ownerCellID;
			face.neighbor = -1;
			face.boundaryGroupID = -1;

			face.v0 = v0;
			face.v1 = v1;

			Vec2 p0 = points[v0];
			Vec2 p1 = points[v1];

			face.center = Vec2{
				0.5 * (p0.z + p1.z),
				0.5 * (p0.r + p1.r)
			};

			double dz = p1.z - p0.z;
			double dr = p1.r - p0.r;

			face.length2D = std::sqrt(dz * dz + dr * dr);
			face.area = 2.0 * PI * face.center.r * face.length2D;

			Vec2 normal{ dr, -dz };

			double mag = std::sqrt(normal.z * normal.z + normal.r * normal.r);

			if (mag > 1e-30) {
				normal.z /= mag;
				normal.r /= mag;
			}

			Vec2 toFace{
				face.center.z - mesh.cells[ownerCellID].center.z,
				face.center.r - mesh.cells[ownerCellID].center.r
			};

			double dot = normal.z * toFace.z + normal.r * toFace.r;

			if (dot < 0.0) {
				normal.z *= -1.0;
				normal.r *= -1.0;
			}

			face.normal = normal;

			int faceID = (int)(mesh.faces.size());
			mesh.faces.push_back(face);

			edgeToFace[key] = faceID;
			mesh.cells[ownerCellID].faceIDs.push_back(faceID);
		}
		else {
			int faceID = it->second;

			mesh.faces[faceID].neighbor = ownerCellID;
			mesh.faces[faceID].boundaryGroupID = -1;

			mesh.cells[ownerCellID].faceIDs.push_back(faceID);
		}
		};

	// -------------------------
	// 2. Create faces
	// -------------------------
	for (int c = 0; c < (int)(triangles.size()); c++) {
		const Triangle& tri = triangles[c];

		addTriangleEdge(c, tri.v0, tri.v1);
		addTriangleEdge(c, tri.v1, tri.v2);
		addTriangleEdge(c, tri.v2, tri.v0);
	}

	// -------------------------
	// 3. Classify boundary faces into boundary groups
	// -------------------------
	// gmsh refines our boundary lines (it inserts nodes along each addLine),
	// so a boundary triangle edge does not share endpoints with the coarse
	// boundary-edge endpoints and an index-based match fails. Instead classify
	// each boundary face by which boundary edge its midpoint lies on.
	double matchScale = std::max(std::max(g.L, g.R), 1.0);
	double matchTol = 1e-4 * matchScale;

	for (FVFace& face : mesh.faces) {
		if (face.neighbor >= 0) {
			continue; // interior face
		}

		int bestGroup = -1;
		double bestDist = matchTol;

		for (const BoundaryEdge& edge : boundaryEdges) {
			if (edge.groupID < 0) continue;
			if (!edgeInRange(edge, boundaryVertices.size())) continue;

			Vec2 a = boundaryVertices[edge.v0].pos;
			Vec2 b = boundaryVertices[edge.v1].pos;

			double d = distancePointToSegment(face.center, a, b);

			if (d < bestDist) {
				bestDist = d;
				bestGroup = edge.groupID;
			}
		}

		face.boundaryGroupID = bestGroup;
	}

	return mesh;
}

std::vector<FVFace> createStructuredFVFaces(
	int nr,
	int nz,
	const std::vector<uint8_t>& activeCell,
	const std::vector<double>& rFace,
	const std::vector<double>& zFace,
	const std::vector<double>& r,
	const std::vector<double>& z,
	const std::vector<BoundarySegmentGroup>& boundaryGroups
) {
	std::vector<FVFace> faces;
	auto boundaryLookup = createBoundaryEdgeLookup(boundaryGroups);

	for (int i = 0; i < nr; i++) {
		for (int jFace = 0; jFace < nz + 1; jFace++) {

			int jLeft = jFace - 1;
			int jRight = jFace;

			bool leftFluid = isFluidCell(i, jLeft, nr, nz, activeCell);
			bool rightFluid = isFluidCell(i, jRight, nr, nz, activeCell);

			if (!leftFluid && !rightFluid) {
				continue; // skip faces between two solid cells
			}

			FVFace face;

			face.center = Vec2(zFace[jFace], r[i]);
			
			double r0 = rFace[i];
			double r1 = rFace[i + 1];

			face.area = PI * (r1 * r1 - r0 * r0);

			MeshEdge edge = makeAxialEdge(i, jFace);

			if (leftFluid && rightFluid) {			// interior fluid
				face.owner = cellID(i, jLeft, nz);
				face.neighbor = cellID(i, jRight, nz);
				face.normal = Vec2(1.0, 0.0); // normal points from left to right

				face.boundaryGroupID = -1; // not a boundary face

			}
			else if (leftFluid && !rightFluid) {
				face.owner = cellID(i, jLeft, nz);	// boundary on right side
				face.neighbor = -1; // boundary face
				face.normal = Vec2(1.0, 0.0); // normal points outward from fluid cell

				face.boundaryGroupID = getBoundaryGroupID(boundaryLookup, edge);

			}
			else if (!leftFluid && rightFluid) { // boundary on left side
				face.owner = cellID(i, jRight, nz);
				face.neighbor = -1; // boundary face
				face.normal = Vec2(-1.0, 0.0); // normal points outward from fluid cell

				face.boundaryGroupID = getBoundaryGroupID(boundaryLookup, edge);

			}

			faces.push_back(face);
		}
	}

	for (int iFace = 0; iFace <= nr; iFace++) {
		for (int j = 0; j < nz; j++) {
			int iLower = iFace - 1;
			int iUpper = iFace;

			bool lowerFluid = isFluidCell(iLower, j, nr, nz, activeCell);
			bool upperFluid = isFluidCell(iUpper, j, nr, nz, activeCell);

			if (!lowerFluid && !upperFluid) {
				continue; // skip faces between two solid cells
			}

			FVFace face;

			face.center = Vec2(z[j], rFace[iFace]);
			face.area = 2.0 * PI * rFace[iFace] * (zFace[j + 1] - zFace[j]);

			MeshEdge edge = makeRadialEdge(iFace, j);

			if (lowerFluid && upperFluid) {
				face.owner = cellID(iLower, j, nz);
				face.neighbor = cellID(iUpper, j, nz);
				face.normal = Vec2(0.0, 1.0);
				face.boundaryGroupID = -1;

			}
			else if (lowerFluid && !upperFluid) {
				face.owner = cellID(iLower, j, nz);
				face.neighbor = -1;
				face.normal = Vec2(0.0, 1.0);

				face.boundaryGroupID = getBoundaryGroupID(boundaryLookup, edge);
			}
			else if (!lowerFluid && upperFluid) {
				face.owner = cellID(iUpper, j, nz);
				face.neighbor = -1;
				face.normal = Vec2(0.0, -1.0);

				face.boundaryGroupID = getBoundaryGroupID(boundaryLookup, edge);
			}

			faces.push_back(face);
		}
	}
	return faces;
}

std::vector<FVCell> createStructuredFVCells(
	int nr,
	int nz,
	const std::vector<uint8_t>& activeCell,
	const std::vector<double>& rFace,
	const std::vector<double>& zFace,
	const std::vector<double>& r,
	const std::vector<double>& z,
	const std::vector<FVFace>& faces) {

	std::vector<FVCell> cells;
	cells.resize(nr * nz);

	for (int i = 0; i < nr; i++) {
		for (int j = 0; j < nz; j++) {

			int n = cellID(i, j, nz);

			FVCell& cell = cells[n];


			cell.center = Vec2(z[j], r[i]);

			double r0 = rFace[i];
			double r1 = rFace[i + 1];
			double dz = zFace[j + 1] - zFace[j];

			cell.volume = PI * (r1 * r1 - r0 * r0) * dz;

			cell.active = activeCell[n] != 0;
			cell.solid = activeCell[n] == 0;

			cell.faceIDs.clear();
		}
	}

	// iterate through each face, which has an owner and neighbor cell indices
	// if the owner or neighbor is a valid cell index, push the face index to the corresponding cell's faceIDs vector
	for (int f = 0; f < (int)faces.size(); f++) {

		const FVFace& face = faces[f];

		if (face.owner >= 0) {
			cells[face.owner].faceIDs.push_back(f);	// push back face indices
		}

		if (face.neighbor >= 0) {
			cells[face.neighbor].faceIDs.push_back(f); // push back face indices
		}
	}

	return cells;
}


FVMesh Mesh::createStructuredMesh(const std::vector<uint8_t>& activeCell) const {

	FVMesh fvMesh;

	std::vector<FVFace> faces = createStructuredFVFaces (
		g.nr,
		g.nz,
		activeCell,
		g.rFace,
		g.zFace,
		g.r,
		g.z,
		boundaryGroups
	);

	std::vector<FVCell> cells = createStructuredFVCells(
		g.nr,
		g.nz,
		activeCell,
		g.rFace,
		g.zFace,
		g.r,
		g.z,
		faces
	);	

	fvMesh.nr = g.nr;
	fvMesh.nz = g.nz;
	fvMesh.faces = std::move(faces);
	fvMesh.cells = std::move(cells);


	return fvMesh;
}

void Mesh::updateAfterLoadingFile() {

	// Rebuild the render/connectivity buffers that are derived from the saved
	// source-of-truth data, so we don't have to persist them and risk staleness.
	if (currentMeshType == MeshType::Unstructured &&
		!unstructuredPoints.empty() &&
		!unstructuredTriangles.empty()) {

		FVMesh fvMesh = createUnstructuredMesh(
			unstructuredPoints,
			unstructuredTriangles,
			boundaryVertices,
			boundaryEdges
		);

		createUnstructuredVertices(unstructuredPoints, unstructuredTriangles);
		createUnstructuredLineVertices(unstructuredPoints, fvMesh);
	}

	isReady = true;
	//console->addLine("Successfully loaded mesh");	// console does not exist at this point (i think), so uncommenting will crash

}

BoundarySegment* Mesh::getBoundarySegmentByID(int id) {

	for (BoundarySegment& seg : boundarySegments) {

		if (seg.id == id) {
			return &seg;
		}
	}

	return nullptr;
}

int Mesh::getAvailableLoopID() {
	return nextLoopID++;
}

int Mesh::getAvailableBoundaryGroupID() const {
	int id = 0;
	for (const auto& g : boundaryGroups)
		id = std::max(id, g.id + 1);
	return id;
}


void Mesh::highlightSegmentsInGroup(const BoundarySegmentGroup& group) {
	highlightedBoundarySegmentIDs.clear();

	for (int segmentID : group.segmentIDs) {
		highlightedBoundarySegmentIDs.insert(segmentID);
	}
}

std::optional<BoundarySegmentGroup> Mesh::createBoundaryGroupFromSelection() {
	if (selectedBoundaryIDs.empty()) {
		return {};
	}

	BoundarySegmentGroup group{};

	group.id = getAvailableBoundaryGroupID();
	group.name = "Boundary " + std::to_string(group.id);

	group.segmentIDs.assign(
		selectedBoundaryIDs.begin(),
		selectedBoundaryIDs.end()
	);

	std::snprintf(
		group.nameBuffer,
		sizeof(group.nameBuffer),
		"%s",
		group.name.c_str()
	);

	// new boundary groups default to target-spacing sizing, with a spacing
	// scaled to the domain so it doesn't over-refine on the first assignment
	group.sizing.enabled = true;
	group.sizing.mode = BoundarySizingMode::TargetSpacing;

	double sizingScale = std::min(g.L, g.R);
	if (sizingScale <= 0.0) {
		sizingScale = std::max(g.L, g.R);
	}
	if (sizingScale <= 0.0) {
		sizingScale = 1.0;
	}
	group.sizing.targetSpacing = std::max(sizingScale / 40.0, 1e-6);

	for (int segmentID : group.segmentIDs) {
		BoundarySegment* seg = getBoundarySegmentByID(segmentID);

		if (!seg) {
			continue;
		}

		seg->groupID = group.id;

		for (int edgeID : seg->edgeIDs) {
			if (edgeID >= 0 &&
				edgeID < static_cast<int>(boundaryEdges.size())) {
				boundaryEdges[edgeID].groupID = group.id;
			}
		}
	}

	return group;
}

void Mesh::createGrid() {

	g.dz.clear();
	g.dr.clear();
	g.r.clear();
	g.z.clear();
	g.zFace.clear();
	g.rFace.clear();

	int nr = g.nr;
	int nz = g.nz;

	std::vector<double>& dz = g.dz;
	std::vector<double>& dr = g.dr;

	std::vector<double>& r = g.r;
	std::vector<double>& z = g.z;

	std::vector<double>& rFace = g.rFace;
	std::vector<double>& zFace = g.zFace;

	rFace = linspace(0.0, g.R, nr + 1, g.rBias);
	zFace = linspace(0.0, g.L, nz + 1, g.zBias);

	// radial location
	for (int i = 0; i < nr; i++) {
		double idr = rFace[i + 1] - rFace[i];
		dr.push_back(idr);
		r.push_back(0.5 * (rFace[i + 1] + rFace[i]));
	}

	for (int j = 0; j < nz; j++) {
		double jdz = zFace[j + 1] - zFace[j];
		dz.push_back(jdz);
		z.push_back(0.5 * (zFace[j + 1] + zFace[j]));
	}
}

FVMesh Mesh::createFVMesh(const std::vector<uint8_t>& activeCell) const {
	if (currentMeshType == MeshType::Structured) {
		return createStructuredMesh(activeCell);
	}

	return createUnstructuredMesh(
		unstructuredPoints,
		unstructuredTriangles,
		boundaryVertices,
		boundaryEdges
	);
}

void Mesh::createGridVertices() {

	gridVertices.clear();

	int nr = g.nr;
	int nz = g.nz;

	const std::vector<double>& rFace = g.rFace;
	const std::vector<double>& zFace = g.zFace;

	for (int i = 0; i < nr; i++) {
		for (int j = 0; j < nz; j++) {

			float x0 = static_cast<float>(2.0 * zFace[j] / g.L - 1.0);
			float x1 = static_cast<float>(2.0 * zFace[j + 1] / g.L - 1.0);

			float y0 = static_cast<float>(2.0 * rFace[i] / g.R - 1.0);
			float y1 = static_cast<float>(2.0 * rFace[i + 1] / g.R - 1.0);

			// Triangle 1
			gridVertices.push_back(x0); gridVertices.push_back(y0);
			gridVertices.push_back(x1); gridVertices.push_back(y0);
			gridVertices.push_back(x1); gridVertices.push_back(y1);

			// Triangle 2
			gridVertices.push_back(x0); gridVertices.push_back(y0);
			gridVertices.push_back(x1); gridVertices.push_back(y1);
			gridVertices.push_back(x0); gridVertices.push_back(y1);
		}
	}
}

void Mesh::createUnstructuredVertices(
	const std::vector<Vec2>& points,
	const std::vector<Triangle>& triangles
) {
	vertices.clear();
	indices.clear();

	for (const Vec2& p : points) {
		glm::vec3 coord{
			displayZ(p.z),
			displayR(p.r),
			0.0f
		};

		vertices.push_back({ coord });
	}

	for (const Triangle& tri : triangles) {
		indices.push_back(tri.v0);
		indices.push_back(tri.v1);
		indices.push_back(tri.v2);
	}
}

void Mesh::createUnstructuredLineVertices(
	const std::vector<Vec2>& points,
	const FVMesh& mesh
) {
	gridLineVertices.clear();

	for (const FVFace& face : mesh.faces) {
		if (face.v0 < 0 || face.v1 < 0) {
			continue;
		}

		const Vec2& p0 = points[face.v0];
		const Vec2& p1 = points[face.v1];

		gridLineVertices.push_back(displayZ(p0.z));
		gridLineVertices.push_back(displayR(p0.r));

		gridLineVertices.push_back(displayZ(p1.z));
		gridLineVertices.push_back(displayR(p1.r));
	}
}

void Mesh::createGridLineVertices() {
	gridLineVertices.clear();

	const std::vector<double>& rFace = g.rFace;
	const std::vector<double>& zFace = g.zFace;

	int nr = g.nr;
	int nz = g.nz;

	// Vertical grid lines, constant z
	for (int j = 0; j <= nz; j++) {

		float x = (float)(2.0 * zFace[j] / g.L - 1.0f);

		float y0 = -1.0f;
		float y1 = 1.0f;

		gridLineVertices.push_back(x); gridLineVertices.push_back(y0);
		gridLineVertices.push_back(x); gridLineVertices.push_back(y1);
	}

	// Horizontal grid lines, constant r
	for (int i = 0; i <= nr; i++) {

		float y = (float)(2.0 * rFace[i] / g.R - 1.0f);

		float x0 = -1.0f;
		float x1 = 1.0f;

		gridLineVertices.push_back(x0); gridLineVertices.push_back(y);
		gridLineVertices.push_back(x1); gridLineVertices.push_back(y);
	}
}

void Mesh::createCylinderVertices() {

	vertices.clear();
	indices.clear();

	int nr = g.nr;
	int nz = g.nz;

	const std::vector<double>& rFace = g.rFace;
	const std::vector<double>& zFace = g.zFace;

	vertices.reserve((nr + 1) * (nz + 1));
	indices.reserve(nr * nz * 6);

	// get all vertices and colors for 2D concentration field
	glm::vec3 coord;
	for (int i = 0; i < nr + 1; i++) {
		for (int j = 0; j < nz + 1; j++) {

			float x = (float)zFace[j];
			float y = (float)rFace[i];

			coord = { x, y, 0.0f };
			vertices.push_back({ coord });
		}
	}

	// get all indices for 2D concentration field
	for (int i = 0; i < nr; i++) {
		for (int j = 0; j < nz; j++) {

			int botLeft = i * (nz + 1) + j;
			int botRight = botLeft + 1;
			int topLeft = (i + 1) * (nz + 1) + j;
			int topRight = topLeft + 1;

			indices.push_back(topLeft);
			indices.push_back(botLeft);
			indices.push_back(botRight);

			indices.push_back(botRight);
			indices.push_back(topRight);
			indices.push_back(topLeft);

		}
	}
}

// ======================================================================
// -----------------------MULTIBLOCK-------------------------------------
// ======================================================================
void Mesh::createMultiBlockLineVertices(const MultiBlockMesh& mb) {
	gridLineVertices.clear();

	auto pushSeg = [&](const MBNode& a, const MBNode& b) {
		gridLineVertices.push_back(displayZ(a.z));  gridLineVertices.push_back(displayR(a.r));
		gridLineVertices.push_back(displayZ(b.z));  gridLineVertices.push_back(displayR(b.r));
		};

	for (const Block& blk : mb.blocks) {
		forEachBlockGridSegment(blk, pushSeg);
	}
}

void Mesh::buildMultiBlockInspectMesh(FVMesh& out,
	std::vector<std::array<Vec2, 4>>& quads) const {

	out = FVMesh{};
	quads.clear();
	if (multiBlock.blocks.empty()) return;

	// Cells + faces (centers, volumes, normals, connectivity) come from the same
	// packer the solver upload uses, so inspector geometry matches the real mesh.
	FVMeshHostPacked packed{};
	toPackedMesh(multiBlock, packed);

	out.cells.resize(packed.nCells);
	for (int c = 0; c < packed.nCells; c++) {
		FVCell& cell = out.cells[c];
		cell.center = Vec2{ packed.cellCenterZ[c], packed.cellCenterR[c] };
		cell.volume = packed.cellVolume[c];
		cell.active = packed.cellActive[c] != 0;
		cell.solid  = packed.cellSolid[c] != 0;
		cell.faceIDs.reserve(packed.cellFaceStart[c + 1] - packed.cellFaceStart[c]);
		for (int k = packed.cellFaceStart[c]; k < packed.cellFaceStart[c + 1]; k++)
			cell.faceIDs.push_back(packed.cellFaceIDs[k]);
	}

	out.faces.resize(packed.nFaces);
	for (int f = 0; f < packed.nFaces; f++) {
		FVFace& face = out.faces[f];
		face.owner = packed.faceOwner[f];
		face.neighbor = packed.faceNeighbor[f];
		face.normal = Vec2{ packed.faceNormalZ[f], packed.faceNormalR[f] };
		face.center = Vec2{ packed.faceCenterZ[f], packed.faceCenterR[f] };
		face.area = packed.faceArea[f];
		face.boundaryGroupID = packed.faceBoundaryGroupID[f];
	}

	// Corner vertices per cell, in the same global order toPackedMesh assigns
	// (block.cellGlobal(i,j)), for point-in-cell picking and highlight drawing.
	quads.assign(packed.nCells, std::array<Vec2, 4>{});
	for (const Block& b : multiBlock.blocks) {
		for (int i = 0; i < b.nr; i++) {
			for (int j = 0; j < b.nz; j++) {
				const int gc = b.cellGlobal(i, j);
				auto V = [&](int I, int J) { const MBNode& n = b.node(I, J); return Vec2{ n.z, n.r }; };
				quads[gc] = { V(i, j), V(i, j + 1), V(i + 1, j + 1), V(i + 1, j) };
			}
		}
	}
}

void Mesh::computeTrellisLines(const SketchModel& sketch,
	std::vector<double>& zLines, std::vector<double>& rLines) const {

	std::vector<double> zs, rs;

	for (const SketchRectangle& rc : sketch.rectangles) {
		if (rc.construction) continue;
		zs.push_back(rc.min.z); zs.push_back(rc.max.z);
		rs.push_back(rc.min.r); rs.push_back(rc.max.r);
	}
	for (const SketchLine& ln : sketch.lines) {
		if (ln.construction) continue;
		const SketchPoint* p0 = sketch.findPoint(ln.p0);
		const SketchPoint* p1 = sketch.findPoint(ln.p1);
		if (!p0 || !p1) continue;
		zs.push_back(p0->pos.z); zs.push_back(p1->pos.z);
		rs.push_back(p0->pos.r); rs.push_back(p1->pos.r);
	}

	const double tol = std::max(g.L, g.R) * 1e-6;
	auto uniqueSorted = [tol](std::vector<double>& v, std::vector<double>& out) {
		std::sort(v.begin(), v.end());
		out.clear();
		for (double x : v)
			if (out.empty() || x - out.back() > tol) out.push_back(x);
	};

	uniqueSorted(zs, zLines);
	uniqueSorted(rs, rLines);
}

void Mesh::ensureBandSizes(size_t nZBands, size_t nRBands) {
	if (zBandCells.size() != nZBands) zBandCells.resize(nZBands, 20);
	if (rBandCells.size() != nRBands) rBandCells.resize(nRBands, 20);
}

bool Mesh::sketchSupportsStructured(const SketchModel& sketch, std::string& reason) const {
	auto anyReal = [](const auto& items) {
		for (const auto& it : items) if (!it.construction) return true;
		return false;
	};

	if (anyReal(sketch.circles)) {
		reason = "Structured grid can't represent circles - remove them or switch to an unstructured mesh.";
		return false;
	}
	if (anyReal(sketch.arcs)) {
		reason = "Structured grid can't represent arcs - remove them or switch to an unstructured mesh.";
		return false;
	}

	// Non-construction lines must be axis-aligned (rectilinear) so trellis blocks
	// conform to them. Domain membership (closed loops, axis-bounded edges,
	// obstacles) is left to pointInsideDomain, exactly like the raster path -- so an
	// edge that lies on the r=0 axis need not be drawn.
	const double tol = std::max(g.L, g.R) * 1e-9;
	bool hasLine = false;
	for (const SketchLine& ln : sketch.lines) {
		if (ln.construction) continue;
		hasLine = true;
		const SketchPoint* p0 = sketch.findPoint(ln.p0);
		const SketchPoint* p1 = sketch.findPoint(ln.p1);
		if (!p0 || !p1) {
			reason = "A line references a missing point.";
			return false;
		}
		const double dz = std::fabs(p1->pos.z - p0->pos.z);
		const double dr = std::fabs(p1->pos.r - p0->pos.r);
		if (dz > tol && dr > tol) {
			reason = "Outline edges must be horizontal or vertical (rectilinear).";
			return false;
		}
	}

	for (const SketchRectangle& rc : sketch.rectangles) {
		if (rc.construction) continue;
		if (rc.max.z - rc.min.z <= 0.0 || rc.max.r - rc.min.r <= 0.0) {
			reason = "A rectangle has zero width or height.";
			return false;
		}
	}

	if (!anyReal(sketch.rectangles) && !hasLine) {
		reason = "Draw a rectangle or a closed rectilinear outline to build a structured grid.";
		return false;
	}
	return true;
}

void Mesh::buildStructuredMultiBlock(const SketchModel& sketch) {
	MultiBlockMesh m;

	std::vector<double> zL, rL;
	computeTrellisLines(sketch, zL, rL);

	int total = 0, kept = 0;
	if (zL.size() >= 2 && rL.size() >= 2) {
		ensureBandSizes(zL.size() - 1, rL.size() - 1);
		total = static_cast<int>((zL.size() - 1) * (rL.size() - 1));

		int id = 1;
		for (size_t i = 0; i + 1 < zL.size(); i++) {
			for (size_t j = 0; j + 1 < rL.size(); j++) {
				// Keep this trellis cell only if its center lies inside the domain,
				// so notches / holes drop out (their cells fall outside the outline).
				const Vec2 center{ 0.5 * (zL[i] + zL[i + 1]), 0.5 * (rL[j] + rL[j + 1]) };
				if (!pointInsideDomain(center)) continue;
				kept++;

				const std::vector<MeshZone> ax  = { { zL[i + 1] - zL[i], std::max(zBandCells[i], 1), Grading::Uniform } };
				const std::vector<MeshZone> rad = { { rL[j + 1] - rL[j], std::max(rBandCells[j], 1), Grading::Uniform } };
				m.blocks.push_back(makeRectBlock(id++, zL[i], rL[j], ax, rad));
			}
		}
	}

	// TEMP diagnostic: shows whether the trellis saw the geometry and dropped holes.
	if (console) {
		console->addLine(("Trellis: zlines=" + std::to_string(zL.size())
			+ " rlines=" + std::to_string(rL.size())
			+ " kept=" + std::to_string(kept) + "/" + std::to_string(total)
			+ " blocks (boundaryVerts=" + std::to_string(boundaryVertices.size()) + ")\n").c_str());
	}

	if (m.blocks.empty()) {
		// No geometry: one block spanning the whole domain [0,L] x [0,R].
		const std::vector<MeshZone> ax  = { { g.L, std::max(g.nz, 1), Grading::Uniform } };
		const std::vector<MeshZone> rad = { { g.R, std::max(g.nr, 1), Grading::Uniform } };
		m.blocks.push_back(makeRectBlock(1, 0.0, 0.0, ax, rad));
	}

	// Trellis blocks share full edges, so every seam is conformal by construction.
	std::string reason;
	const double tol = std::max(g.L, g.R) * 1e-6;
	if (!autoDetectInterfaces(m, reason, tol)) {
		if (console) console->addLine(("Multiblock warning: " + reason + "\n").c_str());
		m.interfaces.clear();
	}

	m.assignGlobalNumbering();
	multiBlock = std::move(m);
	isMultiBlock = true;
}
