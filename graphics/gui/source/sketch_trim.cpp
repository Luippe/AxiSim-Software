#include "sketch_view.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <vector>

#include "geometry.h"
#include "math_func.h"

using namespace sketchmath;

namespace {
	constexpr float pickRadiusPx = 8.0f;

	void addUniqueParameter(std::vector<double>& values, double value) {
		if (value < -sketchEpsilon || value > 1.0 + sketchEpsilon) {
			return;
		}

		value = std::clamp(value, 0.0, 1.0);

		for (double existing : values) {
			if (std::abs(existing - value) <= 1e-7) {
				return;
			}
		}

		values.push_back(value);
	}

	bool parameterInInterval(double value, double start, double end) {
		value = normalizeAngle(value * twoPi) / twoPi;
		start = normalizeAngle(start * twoPi) / twoPi;
		end = normalizeAngle(end * twoPi) / twoPi;

		if (start <= end) {
			return value >= start - 1e-7 && value <= end + 1e-7;
		}

		return value >= start - 1e-7 || value <= end + 1e-7;
	}

	bool sameCircle(Vec2 centerA, double radiusA, Vec2 centerB, double radiusB) {
		double scale = std::max({ 1.0, std::abs(radiusA), std::abs(radiusB) });
		double tolerance = 1e-7 * scale;

		return distance(centerA, centerB) <= tolerance &&
			std::abs(radiusA - radiusB) <= tolerance;
	}

	double circleTolerance(double radiusA, double radiusB) {
		return 1e-7 * std::max({ 1.0, std::abs(radiusA), std::abs(radiusB) });
	}

	bool pointOnCircleWithinTolerance(Vec2 point, Vec2 center, double radius) {
		return std::abs(distance(point, center) - radius) <=
			circleTolerance(radius, radius);
	}

	void addCircleParameterForPoint(
		std::vector<double>& values,
		Vec2 center,
		Vec2 point
	) {
		addUniqueParameter(values, angleOfPoint(center, point) / twoPi);
	}

	void addLineLineIntersection(
		std::vector<double>& values,
		Vec2 targetA,
		Vec2 targetB,
		Vec2 cutterA,
		Vec2 cutterB
	) {
		Vec2 r = subtract(targetB, targetA);
		Vec2 s = subtract(cutterB, cutterA);
		double denominator = cross(r, s);

		if (std::abs(denominator) <= sketchEpsilon) {
			return;
		}

		Vec2 delta = subtract(cutterA, targetA);
		double t = cross(delta, s) / denominator;
		double u = cross(delta, r) / denominator;

		if (t >= -sketchEpsilon && t <= 1.0 + sketchEpsilon &&
			u >= -sketchEpsilon && u <= 1.0 + sketchEpsilon) {
			addUniqueParameter(values, t);
		}
	}

	void addLineCircleIntersections(
		std::vector<double>& values,
		Vec2 targetA,
		Vec2 targetB,
		const SketchCircle& circle
	) {
		Vec2 d = subtract(targetB, targetA);
		Vec2 f = subtract(targetA, circle.center);

		double a = dot(d, d);
		double b = 2.0 * dot(f, d);
		double c = dot(f, f) - circle.radius * circle.radius;

		if (a <= sketchEpsilon) {
			return;
		}

		double discriminant = b * b - 4.0 * a * c;
		if (discriminant < -sketchEpsilon) {
			return;
		}

		discriminant = std::max(0.0, discriminant);
		double root = std::sqrt(discriminant);

		// A tangent contact touches the circle at a single point without crossing
		// it, so it is not a real cut for trimming -- injecting a split there
		// spuriously divides the circle at the touch point. root / sqrt(a) is the
		// world-space chord between the two candidate hits; when it collapses to
		// ~0 the line is (near-)tangent, so skip it and only split genuine
		// two-point crossings.
		double chord = root / std::sqrt(a);
		if (chord <= circleTolerance(circle.radius, circle.radius)) {
			return;
		}

		addUniqueParameter(values, (-b - root) / (2.0 * a));
		addUniqueParameter(values, (-b + root) / (2.0 * a));
	}

	void addLineArcIntersections(
		std::vector<double>& values,
		Vec2 targetA,
		Vec2 targetB,
		const SketchArc& arc
	) {
		std::vector<double> candidates;
		addLineCircleIntersections(
			candidates,
			targetA,
			targetB,
			SketchCircle{ -1, arc.center, arc.radius }
		);

		for (double t : candidates) {
			Vec2 p = interpolate(targetA, targetB, t);
			if (angleOnArc(angleOfPoint(arc.center, p), arc)) {
				addUniqueParameter(values, t);
			}
		}
	}

	void addCircleLineIntersections(
		std::vector<double>& values,
		const SketchCircle& target,
		Vec2 cutterA,
		Vec2 cutterB
	) {
		std::vector<double> lineParameters;
		addLineCircleIntersections(lineParameters, cutterA, cutterB, target);

		for (double lineT : lineParameters) {
			Vec2 p = interpolate(cutterA, cutterB, lineT);
			addUniqueParameter(values, angleOfPoint(target.center, p) / twoPi);
		}
	}

	void addCircleCircleIntersections(
		std::vector<double>& values,
		const SketchCircle& target,
		const SketchCircle& cutter
	) {
		double d = distance(target.center, cutter.center);
		double tolerance = circleTolerance(target.radius, cutter.radius);

		if (d <= sketchEpsilon ||
			d > target.radius + cutter.radius + tolerance ||
			d < std::abs(target.radius - cutter.radius) - tolerance) {
			return;
		}

		double a =
			(target.radius * target.radius -
				cutter.radius * cutter.radius +
				d * d) /
			(2.0 * d);
		double h2 = target.radius * target.radius - a * a;
		if (h2 < -tolerance) {
			return;
		}

		h2 = std::max(0.0, h2);
		double h = std::sqrt(h2);

		// Tangent circles touch at a single point without crossing, which is not a
		// real cut -- skip it rather than injecting a phantom split (2h is the
		// chord between the two intersection points).
		if (2.0 * h <= tolerance) {
			return;
		}

		Vec2 dir{
			(cutter.center.z - target.center.z) / d,
			(cutter.center.r - target.center.r) / d
		};
		Vec2 base{
			target.center.z + a * dir.z,
			target.center.r + a * dir.r
		};
		Vec2 normal{ -dir.r, dir.z };

		Vec2 p0{ base.z + h * normal.z, base.r + h * normal.r };
		Vec2 p1{ base.z - h * normal.z, base.r - h * normal.r };

		addUniqueParameter(values, angleOfPoint(target.center, p0) / twoPi);
		addUniqueParameter(values, angleOfPoint(target.center, p1) / twoPi);
	}

	void addCircleArcIntersections(
		std::vector<double>& values,
		const SketchCircle& target,
		const SketchArc& cutter
	) {
		Vec2 cutterStart =
			pointOnCircle(cutter.center, cutter.radius, cutter.startAngle);
		Vec2 cutterEnd =
			pointOnCircle(cutter.center, cutter.radius, cutter.endAngle);

		if (sameCircle(target.center, target.radius, cutter.center, cutter.radius)) {
			addCircleParameterForPoint(values, target.center, cutterStart);
			addCircleParameterForPoint(values, target.center, cutterEnd);
			return;
		}

		if (pointOnCircleWithinTolerance(cutterStart, target.center, target.radius)) {
			addCircleParameterForPoint(values, target.center, cutterStart);
		}

		if (pointOnCircleWithinTolerance(cutterEnd, target.center, target.radius)) {
			addCircleParameterForPoint(values, target.center, cutterEnd);
		}

		std::vector<double> candidates;
		addCircleCircleIntersections(
			candidates,
			target,
			SketchCircle{ -1, cutter.center, cutter.radius }
		);

		for (double t : candidates) {
			Vec2 p = pointOnCircle(target.center, target.radius, t * twoPi);
			if (angleOnArc(angleOfPoint(cutter.center, p), cutter)) {
				addUniqueParameter(values, t);
			}
		}
	}

	void addArcLineIntersections(
		std::vector<double>& values,
		const SketchArc& target,
		Vec2 cutterA,
		Vec2 cutterB
	) {
		std::vector<double> candidates;
		addCircleLineIntersections(
			candidates,
			SketchCircle{ -1, target.center, target.radius },
			cutterA,
			cutterB
		);

		for (double circleT : candidates) {
			double angle = circleT * twoPi;
			if (angleOnArc(angle, target)) {
				addUniqueParameter(values, arcParameter(angle, target));
			}
		}
	}

	void addArcCircleIntersections(
		std::vector<double>& values,
		const SketchArc& target,
		const SketchCircle& cutter
	) {
		if (sameCircle(target.center, target.radius, cutter.center, cutter.radius)) {
			return;
		}

		Vec2 targetStart =
			pointOnCircle(target.center, target.radius, target.startAngle);
		Vec2 targetEnd =
			pointOnCircle(target.center, target.radius, target.endAngle);

		if (pointOnCircleWithinTolerance(targetStart, cutter.center, cutter.radius)) {
			addUniqueParameter(values, 0.0);
		}

		if (pointOnCircleWithinTolerance(targetEnd, cutter.center, cutter.radius)) {
			addUniqueParameter(values, 1.0);
		}

		std::vector<double> candidates;
		addCircleCircleIntersections(
			candidates,
			SketchCircle{ -1, target.center, target.radius },
			cutter
		);

		for (double circleT : candidates) {
			double angle = circleT * twoPi;
			if (angleOnArc(angle, target)) {
				addUniqueParameter(values, arcParameter(angle, target));
			}
		}
	}

	void addArcArcIntersections(
		std::vector<double>& values,
		const SketchArc& target,
		const SketchArc& cutter
	) {
		auto tryCutterEndpoint = [&](double angle) {
			Vec2 point = pointOnCircle(cutter.center, cutter.radius, angle);
			if (!pointOnCircleWithinTolerance(point, target.center, target.radius)) {
				return;
			}

			double targetAngle = angleOfPoint(target.center, point);
			if (angleOnArc(targetAngle, target)) {
				addUniqueParameter(values, arcParameter(targetAngle, target));
			}
		};

		if (sameCircle(target.center, target.radius, cutter.center, cutter.radius)) {
			if (angleOnArc(cutter.startAngle, target)) {
				addUniqueParameter(values, arcParameter(cutter.startAngle, target));
			}

			if (angleOnArc(cutter.endAngle, target)) {
				addUniqueParameter(values, arcParameter(cutter.endAngle, target));
			}

			return;
		}

		tryCutterEndpoint(cutter.startAngle);
		tryCutterEndpoint(cutter.endAngle);

		std::vector<double> candidates;
		addCircleCircleIntersections(
			candidates,
			SketchCircle{ -1, target.center, target.radius },
			SketchCircle{ -1, cutter.center, cutter.radius }
		);

		for (double circleT : candidates) {
			double angle = circleT * twoPi;
			Vec2 p = pointOnCircle(target.center, target.radius, angle);
			if (angleOnArc(angle, target) &&
				angleOnArc(angleOfPoint(cutter.center, p), cutter)) {
				addUniqueParameter(values, arcParameter(angle, target));
			}
		}
	}

	void addRectangleLineIntersections(
		std::vector<double>& values,
		Vec2 targetA,
		Vec2 targetB,
		const SketchRectangle& rect
	) {
		forEachRectEdge(rect, [&](int, Vec2 a, Vec2 b) {
			addLineLineIntersection(values, targetA, targetB, a, b);
		});
	}

	void addRectangleCircleIntersections(
		std::vector<double>& values,
		const SketchCircle& target,
		const SketchRectangle& rect
	) {
		forEachRectEdge(rect, [&](int, Vec2 a, Vec2 b) {
			addCircleLineIntersections(values, target, a, b);
		});
	}

	void addRectangleArcIntersections(
		std::vector<double>& values,
		const SketchArc& target,
		const SketchRectangle& rect
	) {
		forEachRectEdge(rect, [&](int, Vec2 a, Vec2 b) {
			addArcLineIntersections(values, target, a, b);
		});
	}

	std::vector<double> collectLineSplitParameters(
		const SketchModel& sketch,
		Vec2 a,
		Vec2 b,
		int skipLineID,
		int skipRectangleID
	) {
		std::vector<double> splitParameters{ 0.0, 1.0 };

		for (const SketchLine& line : sketch.lines) {
			if (line.id == skipLineID) {
				continue;
			}

			const SketchPoint* p0 = sketch.findPoint(line.p0);
			const SketchPoint* p1 = sketch.findPoint(line.p1);
			if (p0 && p1) {
				addLineLineIntersection(splitParameters, a, b, p0->pos, p1->pos);
			}
		}

		for (const SketchRectangle& rect : sketch.rectangles) {
			if (rect.id != skipRectangleID) {
				addRectangleLineIntersections(splitParameters, a, b, rect);
			}
		}

		for (const SketchCircle& circle : sketch.circles) {
			addLineCircleIntersections(splitParameters, a, b, circle);
		}

		for (const SketchArc& arc : sketch.arcs) {
			addLineArcIntersections(splitParameters, a, b, arc);
		}

		std::sort(splitParameters.begin(), splitParameters.end());
		return splitParameters;
	}

	std::vector<double> collectCircleSplitParameters(
		const SketchModel& sketch,
		const SketchCircle& target,
		int skipCircleID
	) {
		std::vector<double> splitParameters;

		for (const SketchLine& line : sketch.lines) {
			const SketchPoint* p0 = sketch.findPoint(line.p0);
			const SketchPoint* p1 = sketch.findPoint(line.p1);
			if (p0 && p1) {
				addCircleLineIntersections(splitParameters, target, p0->pos, p1->pos);
			}
		}

		for (const SketchRectangle& rect : sketch.rectangles) {
			addRectangleCircleIntersections(splitParameters, target, rect);
		}

		for (const SketchCircle& circle : sketch.circles) {
			if (circle.id != skipCircleID) {
				addCircleCircleIntersections(splitParameters, target, circle);
			}
		}

		for (const SketchArc& arc : sketch.arcs) {
			addCircleArcIntersections(splitParameters, target, arc);
		}

		std::sort(splitParameters.begin(), splitParameters.end());
		return splitParameters;
	}

	std::vector<double> collectArcSplitParameters(
		const SketchModel& sketch,
		const SketchArc& target,
		int skipArcID
	) {
		std::vector<double> splitParameters{ 0.0, 1.0 };

		for (const SketchLine& line : sketch.lines) {
			const SketchPoint* p0 = sketch.findPoint(line.p0);
			const SketchPoint* p1 = sketch.findPoint(line.p1);
			if (p0 && p1) {
				addArcLineIntersections(splitParameters, target, p0->pos, p1->pos);
			}
		}

		for (const SketchRectangle& rect : sketch.rectangles) {
			addRectangleArcIntersections(splitParameters, target, rect);
		}

		for (const SketchCircle& circle : sketch.circles) {
			addArcCircleIntersections(splitParameters, target, circle);
		}

		for (const SketchArc& arc : sketch.arcs) {
			if (arc.id != skipArcID) {
				addArcArcIntersections(splitParameters, target, arc);
			}
		}

		std::sort(splitParameters.begin(), splitParameters.end());
		return splitParameters;
	}

	std::optional<int> findLinearInterval(
		const std::vector<double>& splitParameters,
		double clickT
	) {
		for (int i = 0; i < (int)(splitParameters.size()) - 1; i++) {
			if (clickT >= splitParameters[i] - sketchEpsilon &&
				clickT <= splitParameters[i + 1] + sketchEpsilon) {
				return i;
			}
		}

		return std::nullopt;
	}

	std::optional<int> findCircleInterval(
		const std::vector<double>& splitParameters,
		double clickT
	) {
		for (int i = 0; i < (int)(splitParameters.size()); i++) {
			double start = splitParameters[i];
			double end = splitParameters[(i + 1) % splitParameters.size()];

			if (parameterInInterval(clickT, start, end)) {
				return i;
			}
		}

		return std::nullopt;
	}

	void addLineIfLong(SketchModel& sketch, Vec2 a, Vec2 b) {
		if (distance(a, b) > 1e-9) {
			sketch.addLine(a, b);
		}
	}

	void addArcIfLong(
		SketchModel& sketch,
		Vec2 center,
		double radius,
		double startAngle,
		double endAngle
	) {
		// normalizeArc keeps the span (which may have wrapped past 2*pi during a
		// circle trim) while bringing startAngle back into [0, 2*pi)
		normalizeArc(startAngle, endAngle);

		if ((endAngle - startAngle) * radius > 1e-9) {
			sketch.addArc(center, radius, startAngle, endAngle);
		}
	}

	TrimPreviewResult linePreview(
		Vec2 a,
		Vec2 b,
		SketchEntityType sourceType,
		int entityID,
		int edgeIndex,
		double startT,
		double endT
	) {
		TrimPreviewResult preview;
		preview.geometry = TrimPreviewGeometry::Line;
		preview.sourceType = sourceType;
		preview.entityID = entityID;
		preview.edgeIndex = edgeIndex;
		preview.startT = startT;
		preview.endT = endT;
		preview.a = a;
		preview.b = b;
		return preview;
	}

	TrimPreviewResult circlePreview(Vec2 center, double radius, int circleID) {
		TrimPreviewResult preview;
		preview.geometry = TrimPreviewGeometry::Circle;
		preview.sourceType = SketchEntityType::Circle;
		preview.entityID = circleID;
		preview.startT = 0.0;
		preview.endT = 1.0;
		preview.center = center;
		preview.radius = radius;
		return preview;
	}

	TrimPreviewResult arcPreview(
		Vec2 center,
		double radius,
		double startAngle,
		double endAngle,
		SketchEntityType sourceType,
		int entityID,
		int edgeIndex,
		double startT,
		double endT
	) {
		TrimPreviewResult preview;
		preview.geometry = TrimPreviewGeometry::Arc;
		preview.sourceType = sourceType;
		preview.entityID = entityID;
		preview.edgeIndex = edgeIndex;
		preview.startT = startT;
		preview.endT = endT;
		preview.center = center;
		preview.radius = radius;
		preview.startAngle = startAngle;
		preview.endAngle = endAngle;
		return preview;
	}

	SketchBound trimPreviewBound(const TrimPreviewResult& preview) {
		switch (preview.geometry) {
		case TrimPreviewGeometry::Line:
			return makeBound(preview.a, preview.b);
		case TrimPreviewGeometry::Circle:
			return {
				Vec2{
					preview.center.z - preview.radius,
					preview.center.r - preview.radius
				},
				Vec2{
					preview.center.z + preview.radius,
					preview.center.r + preview.radius
				}
			};
		case TrimPreviewGeometry::Arc: {
			Vec2 start = pointOnCircle(
				preview.center,
				preview.radius,
				preview.startAngle
			);
			Vec2 end = pointOnCircle(
				preview.center,
				preview.radius,
				preview.endAngle
			);
			SketchBound bounds = makeBound(start, end);

			for (double angle : {
				0.0,
				0.25 * twoPi,
				0.5 * twoPi,
				0.75 * twoPi
			}) {
				if (!angleOnArc(
					angle,
					SketchArc{
						-1,
						preview.center,
						preview.radius,
						preview.startAngle,
						preview.endAngle
					}
				)) {
					continue;
				}

				Vec2 point = pointOnCircle(preview.center, preview.radius, angle);
				bounds.min.z = std::min(bounds.min.z, point.z);
				bounds.min.r = std::min(bounds.min.r, point.r);
				bounds.max.z = std::max(bounds.max.z, point.z);
				bounds.max.r = std::max(bounds.max.r, point.r);
			}

			return bounds;
		}
		default:
			return {};
		}
	}

	void addPreviewIfInside(
		std::vector<TrimPreviewResult>& previews,
		const TrimPreviewResult& preview,
		SketchBound region
	) {
		if (boundsInside(trimPreviewBound(preview), region)) {
			previews.push_back(preview);
		}
	}

	struct ParamInterval {
		double start = 0.0;
		double end = 0.0;
	};

	double clampParameter(double value) {
		return std::clamp(value, 0.0, 1.0);
	}

	void addRemovedInterval(
		std::vector<ParamInterval>& intervals,
		double start,
		double end,
		bool cyclic
	) {
		start = clampParameter(start);
		end = clampParameter(end);

		if (cyclic && end < start - 1e-7) {
			if (start < 1.0 - 1e-7) {
				intervals.push_back({ start, 1.0 });
			}
			if (end > 1e-7) {
				intervals.push_back({ 0.0, end });
			}
			return;
		}

		if (end < start) {
			std::swap(start, end);
		}

		if (end - start > 1e-7) {
			intervals.push_back({ start, end });
		}
	}

	std::vector<ParamInterval> keptIntervalsFromRemoved(
		std::vector<ParamInterval> removed
	) {
		if (removed.empty()) {
			return { ParamInterval{ 0.0, 1.0 } };
		}

		for (ParamInterval& interval : removed) {
			interval.start = clampParameter(interval.start);
			interval.end = clampParameter(interval.end);
			if (interval.end < interval.start) {
				std::swap(interval.start, interval.end);
			}
		}

		std::sort(
			removed.begin(),
			removed.end(),
			[](ParamInterval a, ParamInterval b) {
				return a.start < b.start;
			}
		);

		std::vector<ParamInterval> merged;
		for (ParamInterval interval : removed) {
			if (interval.end - interval.start <= 1e-7) {
				continue;
			}

			if (merged.empty() ||
				interval.start > merged.back().end + 1e-7) {
				merged.push_back(interval);
			}
			else {
				merged.back().end = std::max(merged.back().end, interval.end);
			}
		}

		std::vector<ParamInterval> kept;
		double cursor = 0.0;
		for (ParamInterval interval : merged) {
			if (interval.start > cursor + 1e-7) {
				kept.push_back({ cursor, interval.start });
			}
			cursor = std::max(cursor, interval.end);
		}

		if (cursor < 1.0 - 1e-7) {
			kept.push_back({ cursor, 1.0 });
		}

		return kept;
	}

	std::vector<ParamInterval> removedIntervalsFor(
		const std::vector<TrimPreviewResult>& selectedSegments,
		SketchEntityType sourceType,
		int entityID,
		int edgeIndex,
		bool cyclic
	) {
		std::vector<ParamInterval> intervals;
		for (const TrimPreviewResult& segment : selectedSegments) {
			if (segment.sourceType != sourceType ||
				segment.entityID != entityID) {
				continue;
			}

			if (edgeIndex >= 0 && segment.edgeIndex != edgeIndex) {
				continue;
			}

			addRemovedInterval(
				intervals,
				segment.startT,
				segment.endT,
				cyclic
			);
		}

		return intervals;
	}

	bool sameSource(
		const TrimPreviewResult& a,
		const TrimPreviewResult& b
	) {
		return a.sourceType == b.sourceType &&
			a.entityID == b.entityID;
	}

	bool sameTrimSegment(
		const TrimPreviewResult& a,
		const TrimPreviewResult& b
	) {
		return sameSource(a, b) &&
			a.edgeIndex == b.edgeIndex &&
			std::abs(a.startT - b.startT) <= 1e-7 &&
			std::abs(a.endT - b.endT) <= 1e-7;
	}

	bool trimSourceExists(
		const SketchModel& sketch,
		const TrimPreviewResult& segment
	) {
		switch (segment.sourceType) {
		case SketchEntityType::Line:
			return sketch.findLine(segment.entityID) != nullptr;
		case SketchEntityType::Rectangle:
			return sketch.findRectangle(segment.entityID) != nullptr;
		case SketchEntityType::Circle:
			return sketch.findCircle(segment.entityID) != nullptr;
		case SketchEntityType::Arc:
			return sketch.findArc(segment.entityID) != nullptr;
		default:
			return false;
		}
	}

	// Drop segments whose source entity is gone, and collapse exact duplicates.
	std::vector<TrimPreviewResult> uniqueLiveSegments(
		const SketchModel& sketch,
		const std::vector<TrimPreviewResult>& segments
	) {
		std::vector<TrimPreviewResult> unique;

		for (const TrimPreviewResult& segment : segments) {
			if (!trimSourceExists(sketch, segment)) {
				continue;
			}

			bool duplicate = std::any_of(
				unique.begin(),
				unique.end(),
				[&](const TrimPreviewResult& existing) {
					return sameTrimSegment(existing, segment);
				}
			);

			if (!duplicate) {
				unique.push_back(segment);
			}
		}

		return unique;
	}

	// Add delta-translated copies of `segments` to the sketch as standalone
	// entities, returning a preview of each addition pointing at its new entity.
	// Reads only the resolved geometry of each segment, never its source ID, so
	// the input may reference entities that no longer exist (a stale clipboard).
	std::vector<TrimPreviewResult> addTranslatedSegments(
		SketchModel& sketch,
		const std::vector<TrimPreviewResult>& segments,
		Vec2 delta
	) {
		std::vector<TrimPreviewResult> added;

		for (const TrimPreviewResult& segment : segments) {
			switch (segment.geometry) {
			case TrimPreviewGeometry::Line: {
				Vec2 a = translate(segment.a, delta);
				Vec2 b = translate(segment.b, delta);
				if (distance(a, b) <= 1e-9) {
					break;
				}

				int lineID = sketch.addLine(a, b);
				added.push_back(
					linePreview(
						a,
						b,
						SketchEntityType::Line,
						lineID,
						-1,
						0.0,
						1.0
					)
				);
				break;
			}
			case TrimPreviewGeometry::Circle: {
				if (segment.radius <= 1e-9) {
					break;
				}

				Vec2 center = translate(segment.center, delta);
				int circleID = sketch.addCircle(center, segment.radius);
				added.push_back(circlePreview(center, segment.radius, circleID));
				break;
			}
			case TrimPreviewGeometry::Arc: {
				if (segment.radius <= 1e-9) {
					break;
				}

				// normalize the two angles together so the span is preserved even
				// when the segment's startAngle wrapped past 2*pi (otherwise a moved
				// arc balloons into a near-full circle)
				double startAngle = segment.startAngle;
				double endAngle = segment.endAngle;
				normalizeArc(startAngle, endAngle);

				if ((endAngle - startAngle) * segment.radius <= 1e-9) {
					break;
				}

				Vec2 center = translate(segment.center, delta);
				int arcID = sketch.addArc(
					center,
					segment.radius,
					startAngle,
					endAngle
				);
				added.push_back(
					arcPreview(
						center,
						segment.radius,
						startAngle,
						endAngle,
						SketchEntityType::Arc,
						arcID,
						-1,
						0.0,
						1.0
					)
				);
				break;
			}
			default:
				break;
			}
		}

		return added;
	}

	int entityHoverPriority(SketchEntityType type) {
		switch (type) {
		case SketchEntityType::Arc:
			return 0;
		case SketchEntityType::Circle:
			return 1;
		case SketchEntityType::Line:
			return 2;
		case SketchEntityType::Rectangle:
			return 3;
		default:
			return 4;
		}
	}
}

std::optional<SketchTrimTarget> SketchView::findTrimTarget(ImVec2 mouse) {
	if (mouse.x < imageMin.x || mouse.x > imageMax.x ||
		mouse.y < imageMin.y || mouse.y > imageMax.y) {
		return std::nullopt;
	}

	Vec2 mouseWorld = camera.screenToWorld(mouse);
	std::vector<SketchTrimTarget> candidates;

	auto tryCandidate = [&](
		SketchEntityType type,
		int entityID,
		int edgeIndex,
		float distancePx
	) {
		if (distancePx <= pickRadiusPx) {
			candidates.push_back({ type, entityID, edgeIndex, distancePx });
		}
	};

	for (const SketchLine& line : geometry.sketch.lines) {
		const SketchPoint* p0 = geometry.sketch.findPoint(line.p0);
		const SketchPoint* p1 = geometry.sketch.findPoint(line.p1);
		if (!p0 || !p1) {
			continue;
		}

		Vec2 closest = closestPointOnSegment(mouseWorld, p0->pos, p1->pos);
		tryCandidate(
			SketchEntityType::Line,
			line.id,
			-1,
			pixelDistance(camera.worldToScreen(closest), mouse)
		);
	}

	for (const SketchRectangle& rect : geometry.sketch.rectangles) {
		forEachRectEdge(rect, [&](int edge, Vec2 a, Vec2 b) {
			Vec2 closest = closestPointOnSegment(mouseWorld, a, b);
			tryCandidate(
				SketchEntityType::Rectangle,
				rect.id,
				edge,
				pixelDistance(camera.worldToScreen(closest), mouse)
			);
		});
	}

	for (const SketchCircle& circle : geometry.sketch.circles) {
		double radialDistance =
			std::abs(distance(mouseWorld, circle.center) - circle.radius);

		tryCandidate(
			SketchEntityType::Circle,
			circle.id,
			-1,
			camera.worldLengthToScreen(radialDistance)
		);
	}

	for (const SketchArc& arc : geometry.sketch.arcs) {
		double angle = angleOfPoint(arc.center, mouseWorld);
		if (!angleOnArc(angle, arc)) {
			continue;
		}

		double radialDistance =
			std::abs(distance(mouseWorld, arc.center) - arc.radius);

		tryCandidate(
			SketchEntityType::Arc,
			arc.id,
			-1,
			camera.worldLengthToScreen(radialDistance)
		);
	}

	std::sort(
		candidates.begin(),
		candidates.end(),
		[](const SketchTrimTarget& a, const SketchTrimTarget& b) {
			if (std::abs(a.distancePx - b.distancePx) > 0.5f) {
				return a.distancePx < b.distancePx;
			}

			return entityHoverPriority(a.type) < entityHoverPriority(b.type);
		}
	);

	if (candidates.empty()) {
		return std::nullopt;
	}

	return candidates.front();
}

std::optional<TrimPreviewResult> SketchView::findTrimPreview(ImVec2 mouse) {
	std::optional<SketchTrimTarget> target = findTrimTarget(mouse);
	if (!target) {
		return std::nullopt;
	}

	Vec2 mouseWorld = camera.screenToWorld(mouse);

	auto segmentPreview = [&](
		Vec2 a,
		Vec2 b,
		int skipLineID,
		int skipRectangleID,
		SketchEntityType sourceType,
		int entityID,
		int edgeIndex
	) -> std::optional<TrimPreviewResult> {
		std::vector<double> splitParameters = collectLineSplitParameters(
			geometry.sketch, a, b, skipLineID, skipRectangleID);
		std::optional<int> interval =
			findLinearInterval(splitParameters, segmentParameter(mouseWorld, a, b));

		if (!interval) {
			return std::nullopt;
		}

		return linePreview(
			interpolate(a, b, splitParameters[*interval]),
			interpolate(a, b, splitParameters[*interval + 1]),
			sourceType,
			entityID,
			edgeIndex,
			splitParameters[*interval],
			splitParameters[*interval + 1]
		);
	};

	switch (target->type) {
	case SketchEntityType::Line: {
		const SketchLine* line = geometry.sketch.findLine(target->entityID);
		if (!line) {
			return std::nullopt;
		}

		const SketchPoint* p0 = geometry.sketch.findPoint(line->p0);
		const SketchPoint* p1 = geometry.sketch.findPoint(line->p1);
		if (!p0 || !p1) {
			return std::nullopt;
		}

		return segmentPreview(
			p0->pos,
			p1->pos,
			line->id,
			-1,
			SketchEntityType::Line,
			line->id,
			-1
		);
	}
	case SketchEntityType::Rectangle: {
		const SketchRectangle* rect =
			geometry.sketch.findRectangle(target->entityID);
		if (!rect || target->edgeIndex < 0 || target->edgeIndex > 3) {
			return std::nullopt;
		}

		auto corners = rectangleCorners(*rect);
		return segmentPreview(
			corners[target->edgeIndex],
			corners[(target->edgeIndex + 1) % 4],
			-1,
			rect->id,
			SketchEntityType::Rectangle,
			rect->id,
			target->edgeIndex
		);
	}
	case SketchEntityType::Circle: {
		const SketchCircle* circle =
			geometry.sketch.findCircle(target->entityID);
		if (!circle) {
			return std::nullopt;
		}

		std::vector<double> splitParameters =
			collectCircleSplitParameters(geometry.sketch, *circle, circle->id);

		if (splitParameters.size() < 2) {
			return circlePreview(circle->center, circle->radius, circle->id);
		}

		std::optional<int> interval = findCircleInterval(
			splitParameters,
			angleOfPoint(circle->center, mouseWorld) / twoPi
		);

		if (!interval) {
			return std::nullopt;
		}

		return arcPreview(
			circle->center,
			circle->radius,
			splitParameters[*interval] * twoPi,
			splitParameters[(*interval + 1) % splitParameters.size()] * twoPi,
			SketchEntityType::Circle,
			circle->id,
			-1,
			splitParameters[*interval],
			splitParameters[(*interval + 1) % splitParameters.size()]
		);
	}
	case SketchEntityType::Arc: {
		const SketchArc* arc = geometry.sketch.findArc(target->entityID);
		if (!arc) {
			return std::nullopt;
		}

		double clickAngle = angleOfPoint(arc->center, mouseWorld);
		if (!angleOnArc(clickAngle, *arc)) {
			return std::nullopt;
		}

		std::vector<double> splitParameters =
			collectArcSplitParameters(geometry.sketch, *arc, arc->id);
		std::optional<int> interval =
			findLinearInterval(splitParameters, arcParameter(clickAngle, *arc));

		if (!interval) {
			return std::nullopt;
		}

		double span = arcSpan(*arc);
		return arcPreview(
			arc->center,
			arc->radius,
			arc->startAngle + splitParameters[*interval] * span,
			arc->startAngle + splitParameters[*interval + 1] * span,
			SketchEntityType::Arc,
			arc->id,
			-1,
			splitParameters[*interval],
			splitParameters[*interval + 1]
		);
	}
	default:
		return std::nullopt;
	}
}

std::vector<TrimPreviewResult> SketchView::findTrimPreviewsInRegion(
	SketchBound region
) {
	std::vector<TrimPreviewResult> previews;

	auto addLineIntervals = [&](
		Vec2 a,
		Vec2 b,
		int skipLineID,
		int skipRectangleID,
		SketchEntityType sourceType,
		int entityID,
		int edgeIndex
	) {
		std::vector<double> splitParameters = collectLineSplitParameters(
			geometry.sketch,
			a,
			b,
			skipLineID,
			skipRectangleID
		);

		for (int i = 0; i < (int)(splitParameters.size()) - 1; i++) {
			Vec2 start = interpolate(a, b, splitParameters[i]);
			Vec2 end = interpolate(a, b, splitParameters[i + 1]);

			if (distance(start, end) > 1e-9) {
				addPreviewIfInside(
					previews,
					linePreview(
						start,
						end,
						sourceType,
						entityID,
						edgeIndex,
						splitParameters[i],
						splitParameters[i + 1]
					),
					region
				);
			}
		}
	};

	for (const SketchLine& line : geometry.sketch.lines) {
		const SketchPoint* p0 = geometry.sketch.findPoint(line.p0);
		const SketchPoint* p1 = geometry.sketch.findPoint(line.p1);
		if (p0 && p1) {
			addLineIntervals(
				p0->pos,
				p1->pos,
				line.id,
				-1,
				SketchEntityType::Line,
				line.id,
				-1
			);
		}
	}

	for (const SketchRectangle& rect : geometry.sketch.rectangles) {
		forEachRectEdge(rect, [&](int edge, Vec2 a, Vec2 b) {
			addLineIntervals(
				a,
				b,
				-1,
				rect.id,
				SketchEntityType::Rectangle,
				rect.id,
				edge
			);
		});
	}

	for (const SketchCircle& circle : geometry.sketch.circles) {
		std::vector<double> splitParameters =
			collectCircleSplitParameters(geometry.sketch, circle, circle.id);

		if (splitParameters.size() < 2) {
			addPreviewIfInside(
				previews,
				circlePreview(circle.center, circle.radius, circle.id),
				region
			);
			continue;
		}

		for (int i = 0; i < (int)(splitParameters.size()); i++) {
			addPreviewIfInside(
				previews,
				arcPreview(
					circle.center,
					circle.radius,
					splitParameters[i] * twoPi,
					splitParameters[(i + 1) % splitParameters.size()] * twoPi,
					SketchEntityType::Circle,
					circle.id,
					-1,
					splitParameters[i],
					splitParameters[(i + 1) % splitParameters.size()]
				),
				region
			);
		}
	}

	for (const SketchArc& arc : geometry.sketch.arcs) {
		std::vector<double> splitParameters =
			collectArcSplitParameters(geometry.sketch, arc, arc.id);
		double span = arcSpan(arc);

		for (int i = 0; i < (int)(splitParameters.size()) - 1; i++) {
			double startAngle = arc.startAngle + splitParameters[i] * span;
			double endAngle = arc.startAngle + splitParameters[i + 1] * span;

			if ((endAngle - startAngle) * arc.radius > 1e-9) {
				addPreviewIfInside(
					previews,
					arcPreview(
						arc.center,
						arc.radius,
						startAngle,
						endAngle,
						SketchEntityType::Arc,
						arc.id,
						-1,
						splitParameters[i],
						splitParameters[i + 1]
					),
					region
				);
			}
		}
	}

	return previews;
}

bool SketchView::deleteSelectedTrimSegments() {
	if (selectedTrimSegments.empty()) {
		return false;
	}

	std::vector<TrimPreviewResult> selectedSegments = selectedTrimSegments;
	std::vector<bool> processed(selectedSegments.size(), false);
	bool changed = false;

	for (int i = 0; i < (int)(selectedSegments.size()); i++) {
		if (processed[i] || selectedSegments[i].entityID < 0) {
			continue;
		}

		for (int j = i; j < (int)(selectedSegments.size()); j++) {
			if (sameSource(selectedSegments[i], selectedSegments[j])) {
				processed[j] = true;
			}
		}

		switch (selectedSegments[i].sourceType) {
		case SketchEntityType::Line: {
			const SketchLine* foundLine =
				geometry.sketch.findLine(selectedSegments[i].entityID);
			if (!foundLine) {
				break;
			}

			SketchLine line = *foundLine;
			const SketchPoint* p0 = geometry.sketch.findPoint(line.p0);
			const SketchPoint* p1 = geometry.sketch.findPoint(line.p1);
			if (!p0 || !p1) {
				break;
			}

			Vec2 a = p0->pos;
			Vec2 b = p1->pos;
			std::vector<ParamInterval> removed = removedIntervalsFor(
				selectedSegments,
				SketchEntityType::Line,
				line.id,
				-1,
				false
			);

			if (removed.empty()) {
				break;
			}

			geometry.sketch.removeEntity(SketchEntityType::Line, line.id);

			for (ParamInterval kept : keptIntervalsFromRemoved(removed)) {
				addLineIfLong(
					geometry.sketch,
					interpolate(a, b, kept.start),
					interpolate(a, b, kept.end)
				);
			}

			changed = true;
			break;
		}
		case SketchEntityType::Rectangle: {
			const SketchRectangle* foundRect =
				geometry.sketch.findRectangle(selectedSegments[i].entityID);
			if (!foundRect) {
				break;
			}

			SketchRectangle rect = *foundRect;
			std::array<std::vector<ParamInterval>, 4> removedByEdge;
			bool hasRemoved = false;
			for (int edge = 0; edge < 4; edge++) {
				removedByEdge[edge] = removedIntervalsFor(
					selectedSegments,
					SketchEntityType::Rectangle,
					rect.id,
					edge,
					false
				);

				if (!removedByEdge[edge].empty()) {
					hasRemoved = true;
				}
			}

			if (!hasRemoved) {
				break;
			}

			geometry.sketch.removeEntity(SketchEntityType::Rectangle, rect.id);

			forEachRectEdge(rect, [&](int edge, Vec2 a, Vec2 b) {
				for (ParamInterval kept :
					keptIntervalsFromRemoved(removedByEdge[edge])) {
					addLineIfLong(
						geometry.sketch,
						interpolate(a, b, kept.start),
						interpolate(a, b, kept.end)
					);
				}
			});

			changed = true;
			break;
		}
		case SketchEntityType::Circle: {
			const SketchCircle* foundCircle =
				geometry.sketch.findCircle(selectedSegments[i].entityID);
			if (!foundCircle) {
				break;
			}

			SketchCircle circle = *foundCircle;
			std::vector<ParamInterval> removed = removedIntervalsFor(
				selectedSegments,
				SketchEntityType::Circle,
				circle.id,
				-1,
				true
			);

			if (removed.empty()) {
				break;
			}

			geometry.sketch.removeEntity(SketchEntityType::Circle, circle.id);

			for (ParamInterval kept : keptIntervalsFromRemoved(removed)) {
				addArcIfLong(
					geometry.sketch,
					circle.center,
					circle.radius,
					kept.start * twoPi,
					kept.end * twoPi
				);
			}

			changed = true;
			break;
		}
		case SketchEntityType::Arc: {
			const SketchArc* foundArc =
				geometry.sketch.findArc(selectedSegments[i].entityID);
			if (!foundArc) {
				break;
			}

			SketchArc arc = *foundArc;
			std::vector<ParamInterval> removed = removedIntervalsFor(
				selectedSegments,
				SketchEntityType::Arc,
				arc.id,
				-1,
				false
			);

			if (removed.empty()) {
				break;
			}

			geometry.sketch.removeEntity(SketchEntityType::Arc, arc.id);

			double span = arcSpan(arc);
			for (ParamInterval kept : keptIntervalsFromRemoved(removed)) {
				addArcIfLong(
					geometry.sketch,
					arc.center,
					arc.radius,
					arc.startAngle + kept.start * span,
					arc.startAngle + kept.end * span
				);
			}

			changed = true;
			break;
		}
		default:
			break;
		}
	}

	if (changed) {
		selectedTrimSegments.clear();
	}

	return changed;
}

bool SketchView::moveSelectedTrimSegments(Vec2 delta) {
	if (selectedTrimSegments.empty()) {
		return false;
	}

	if (std::abs(delta.z) <= 1e-12 && std::abs(delta.r) <= 1e-12) {
		return false;
	}

	std::vector<TrimPreviewResult> originalSelection = selectedTrimSegments;
	std::vector<TrimPreviewResult> selectedSegments =
		uniqueLiveSegments(geometry.sketch, selectedTrimSegments);

	if (selectedSegments.empty()) {
		return false;
	}

	selectedTrimSegments = selectedSegments;
	if (!deleteSelectedTrimSegments()) {
		selectedTrimSegments = originalSelection;
		return false;
	}

	std::vector<TrimPreviewResult> movedSegments =
		addTranslatedSegments(geometry.sketch, selectedSegments, delta);

	selectedTrimSegments = movedSegments;
	return !movedSegments.empty();
}

void SketchView::publishSelectionSummary() {
	std::vector<TrimPreviewResult> live =
		uniqueLiveSegments(geometry.sketch, selectedTrimSegments);

	geometry.selectionCount = (int)live.size();

	if (live.empty()) {
		geometry.selectionCenter = Vec2{};
	}
	else {
		SketchBound bounds = trimPreviewBound(live.front());
		for (const TrimPreviewResult& segment : live) {
			SketchBound b = trimPreviewBound(segment);
			bounds.min.z = std::min(bounds.min.z, b.min.z);
			bounds.min.r = std::min(bounds.min.r, b.min.r);
			bounds.max.z = std::max(bounds.max.z, b.max.z);
			bounds.max.r = std::max(bounds.max.r, b.max.r);
		}
		geometry.selectionCenter = Vec2{
			(bounds.min.z + bounds.max.z) * 0.5,
			(bounds.min.r + bounds.max.r) * 0.5
		};
	}

	// Signature over the selected SET (entity id + type), so the panel re-reads the
	// center into its fields only when the selection changes -- not every frame,
	// which would clobber whatever the user is typing.
	size_t signature = 1469598103934665603ULL; // 64-bit FNV-1a offset basis
	auto mix = [&](size_t value) {
		signature ^= value;
		signature *= 1099511628211ULL;         // FNV prime
	};
	mix((size_t)live.size());
	for (const TrimPreviewResult& segment : live) {
		mix((size_t)segment.entityID);
		mix((size_t)segment.sourceType);
		mix((size_t)segment.geometry);
	}

	if (signature != lastSelectionSignature) {
		lastSelectionSignature = signature;
		geometry.selectionRevision++;
	}
}

bool SketchView::copySelectedTrimSegments() {
	std::vector<TrimPreviewResult> copied =
		uniqueLiveSegments(geometry.sketch, selectedTrimSegments);

	if (copied.empty()) {
		return false;
	}

	Vec2 anchor = trimPreviewBound(copied.front()).min;
	for (const TrimPreviewResult& segment : copied) {
		SketchBound bound = trimPreviewBound(segment);
		anchor.z = std::min(anchor.z, bound.min.z);
		anchor.r = std::min(anchor.r, bound.min.r);
	}

	clipboardSegments = copied;
	clipboardAnchor = anchor;
	return true;
}

bool SketchView::pasteClipboardSegments(Vec2 delta) {
	if (clipboardSegments.empty()) {
		return false;
	}

	std::vector<TrimPreviewResult> pastedSegments =
		addTranslatedSegments(geometry.sketch, clipboardSegments, delta);

	if (pastedSegments.empty()) {
		return false;
	}

	// select the paste, not the original, so it can be dragged straight away
	selectedTrimSegments = pastedSegments;
	return true;
}

Vec2 SketchView::offsetPasteTarget() const {
	constexpr float offsetPx = 20.0f;
	double offset = offsetPx * camera.unitsPerPixel;

	// down-right on screen: r grows upward, so it goes down as z goes right
	return Vec2{
		clipboardAnchor.z + offset,
		clipboardAnchor.r - offset
	};
}

bool SketchView::pasteClipboardAt(Vec2 target) {
	if (clipboardSegments.empty()) {
		return false;
	}

	// clears the drawing/selection/label-drag interaction state for us
	setActiveSketchTool(SketchTool::Select);

	SketchModel beforePaste = geometry.sketch;
	if (!pasteClipboardSegments(subtract(target, clipboardAnchor))) {
		return false;
	}

	recordSketchUndoState(beforePaste);
	return true;
}

bool SketchView::trimLineAtMouse(ImVec2 mouse) {
	std::optional<SketchTrimTarget> target = findTrimTarget(mouse);
	if (!target || target->type != SketchEntityType::Line) {
		return false;
	}

	const SketchLine* line = geometry.sketch.findLine(target->entityID);
	if (!line) {
		return false;
	}

	const SketchPoint* p0 = geometry.sketch.findPoint(line->p0);
	const SketchPoint* p1 = geometry.sketch.findPoint(line->p1);
	if (!p0 || !p1) {
		return false;
	}

	Vec2 mouseWorld = camera.screenToWorld(mouse);
	Vec2 a = p0->pos;
	Vec2 b = p1->pos;
	int lineID = line->id;
	std::vector<double> splitParameters =
		collectLineSplitParameters(geometry.sketch, a, b, lineID, -1);
	std::optional<int> interval =
		findLinearInterval(splitParameters, segmentParameter(mouseWorld, a, b));

	if (!interval) {
		return false;
	}

	geometry.sketch.removeEntity(SketchEntityType::Line, lineID);

	double removeLeft = splitParameters[*interval];
	double removeRight = splitParameters[*interval + 1];

	if (removeLeft > 1e-7) {
		addLineIfLong(geometry.sketch, a, interpolate(a, b, removeLeft));
	}

	if (removeRight < 1.0 - 1e-7) {
		addLineIfLong(geometry.sketch, interpolate(a, b, removeRight), b);
	}

	return true;
}

bool SketchView::trimRectangleAtMouse(ImVec2 mouse) {
	std::optional<SketchTrimTarget> target = findTrimTarget(mouse);
	if (!target || target->type != SketchEntityType::Rectangle ||
		target->edgeIndex < 0 || target->edgeIndex > 3) {
		return false;
	}

	const SketchRectangle* foundRect =
		geometry.sketch.findRectangle(target->entityID);
	if (!foundRect) {
		return false;
	}

	SketchRectangle rect = *foundRect;
	auto corners = rectangleCorners(rect);
	Vec2 a = corners[target->edgeIndex];
	Vec2 b = corners[(target->edgeIndex + 1) % 4];
	Vec2 mouseWorld = camera.screenToWorld(mouse);
	std::vector<double> splitParameters =
		collectLineSplitParameters(geometry.sketch, a, b, -1, rect.id);
	std::optional<int> interval =
		findLinearInterval(splitParameters, segmentParameter(mouseWorld, a, b));

	if (!interval) {
		return false;
	}

	geometry.sketch.removeEntity(SketchEntityType::Rectangle, rect.id);

	double removeLeft = splitParameters[*interval];
	double removeRight = splitParameters[*interval + 1];

	forEachRectEdge(rect, [&](int edge, Vec2 edgeA, Vec2 edgeB) {
		if (edge != target->edgeIndex) {
			addLineIfLong(geometry.sketch, edgeA, edgeB);
			return;
		}

		if (removeLeft > 1e-7) {
			addLineIfLong(geometry.sketch, edgeA, interpolate(edgeA, edgeB, removeLeft));
		}

		if (removeRight < 1.0 - 1e-7) {
			addLineIfLong(geometry.sketch, interpolate(edgeA, edgeB, removeRight), edgeB);
		}
	});

	return true;
}

bool SketchView::trimCircleAtMouse(ImVec2 mouse, int circleID) {
	const SketchCircle* targetCircle = geometry.sketch.findCircle(circleID);
	if (!targetCircle) {
		return false;
	}

	SketchCircle circle = *targetCircle;
	Vec2 mouseWorld = camera.screenToWorld(mouse);
	std::vector<double> splitParameters =
		collectCircleSplitParameters(geometry.sketch, circle, circleID);

	// nothing crosses it, so there is no piece to keep: trimming takes the circle
	if (splitParameters.size() < 2) {
		geometry.sketch.removeEntity(SketchEntityType::Circle, circleID);
		return true;
	}

	std::optional<int> interval = findCircleInterval(
		splitParameters,
		angleOfPoint(circle.center, mouseWorld) / twoPi
	);

	if (!interval) {
		return false;
	}

	geometry.sketch.removeEntity(SketchEntityType::Circle, circleID);

	for (int i = 0; i < (int)(splitParameters.size()); i++) {
		if (i == *interval) {
			continue;
		}

		addArcIfLong(
			geometry.sketch,
			circle.center,
			circle.radius,
			splitParameters[i] * twoPi,
			splitParameters[(i + 1) % splitParameters.size()] * twoPi
		);
	}

	return true;
}

bool SketchView::trimArcAtMouse(ImVec2 mouse, int arcID) {
	const SketchArc* targetArc = geometry.sketch.findArc(arcID);
	if (!targetArc) {
		return false;
	}

	SketchArc arc = *targetArc;
	Vec2 mouseWorld = camera.screenToWorld(mouse);
	double clickAngle = angleOfPoint(arc.center, mouseWorld);

	if (!angleOnArc(clickAngle, arc)) {
		return false;
	}

	std::vector<double> splitParameters =
		collectArcSplitParameters(geometry.sketch, arc, arcID);
	std::optional<int> interval =
		findLinearInterval(splitParameters, arcParameter(clickAngle, arc));

	if (!interval) {
		return false;
	}

	geometry.sketch.removeEntity(SketchEntityType::Arc, arcID);

	double span = arcSpan(arc);
	for (int i = 0; i < (int)(splitParameters.size()) - 1; i++) {
		if (i == *interval) {
			continue;
		}

		addArcIfLong(
			geometry.sketch,
			arc.center,
			arc.radius,
			arc.startAngle + splitParameters[i] * span,
			arc.startAngle + splitParameters[i + 1] * span
		);
	}

	return true;
}

bool SketchView::trimSketchAtMouse(ImVec2 mouse) {
	std::optional<SketchTrimTarget> target = findTrimTarget(mouse);
	if (!target) {
		return false;
	}

	switch (target->type) {
	case SketchEntityType::Line:
		return trimLineAtMouse(mouse);
	case SketchEntityType::Rectangle:
		return trimRectangleAtMouse(mouse);
	case SketchEntityType::Circle:
		return trimCircleAtMouse(mouse, target->entityID);
	case SketchEntityType::Arc:
		return trimArcAtMouse(mouse, target->entityID);
	default:
		return false;
	}
}

bool SketchView::eraseEntityAtMouse(ImVec2 mouse) {
	std::optional<SketchTrimTarget> target = findTrimTarget(mouse);
	if (!target) {
		return false;
	}

	// shared with the Geometry panel's delete button
	return geometry.sketch.removeEntity(target->type, target->entityID);
}

void SketchView::drawTrimPreview(ImDrawList* drawList) {
	if (geometry.sketch.activeTool != SketchTool::Trim ||
		!ImGui::IsItemHovered()) {
		return;
	}

	std::optional<TrimPreviewResult> preview =
		findTrimPreview(currentMousePos);
	if (!preview) {
		return;
	}

	const ImU32 previewColor = IM_COL32(255, 225, 80, 255);
	const float previewThickness = 4.0f;

	drawList->PushClipRect(imageMin, imageMax, true);

	switch (preview->geometry) {
	case TrimPreviewGeometry::Line:
		drawList->AddLine(
			camera.worldToScreen(preview->a),
			camera.worldToScreen(preview->b),
			previewColor,
			previewThickness
		);
		break;
	case TrimPreviewGeometry::Circle:
		drawList->AddCircle(
			camera.worldToScreen(preview->center),
			camera.worldLengthToScreen(preview->radius),
			previewColor,
			96,
			previewThickness
		);
		break;
	case TrimPreviewGeometry::Arc:
		drawArc(
			drawList,
			preview->center,
			preview->radius,
			preview->startAngle,
			preview->endAngle,
			previewColor,
			previewThickness
		);
		break;
	default:
		break;
	}

	drawList->PopClipRect();
}
