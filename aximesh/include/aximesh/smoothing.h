#pragma once

#include "aximesh/aximesh.h"

#include <cstdint>
#include <vector>

// step 8: mesh improvement -- short-edge collapse, edge flips and point smoothing.
// Split out of aximesh.cpp; the bodies are unchanged.
namespace AxiMesh::Smoothing {

	using SmoothFunc = double(*)(double d);

	Point ringCentroid(
		const std::vector<Point>& points,
		const PointRing& ring
	);

	double triangleQuality(
		const Point& a,
		const Point& b,
		const Point& c
	);

	double fanQuality(
		const std::vector<Point>& points,
		const std::vector<Triangle>& triangles,
		const PointRing& ring,
		int vi,
		const Point& candidate
	);

	std::vector<uint8_t> buildPinnedPoints(
		const std::vector<Segment>& segments,
		int size
	);

	std::vector<PointRing> buildPointRings(
		const std::vector<Point>& points,
		const std::vector<Triangle>& triangles,
		const std::vector<uint8_t>& pinned
	);

	SmoothFunc getSmoothingFunction(
		const Params& params
	);

	double normalizedArea(
		const std::vector<Point>& points,
		const Triangle& triangle,
		const std::vector<double>& h
	);

	double getPointExtent(
		const std::vector<Point>& points,
		const std::vector<Triangle>& triangles,
		const std::vector<double>& h,
		const PointRing& ring,
		const Params& params
	);

	double getEdgeExtent(
		const std::vector<Point>& points,
		const std::vector<Triangle>& triangles,
		const std::vector<double>& h,
		int t,
		int e
	);

	Point generalSmoothing(
		const std::vector<Point>& points,
		const std::vector<Triangle>& triangles,
		const std::vector<double>& h,
		const PointRing& ring,
		const Params& params,
		SmoothFunc func,
		int nP
	);

	bool collapseBoundaryEdge(
		std::vector<Triangle>& triangles,
		std::vector<uint8_t>& dead,
		const PointRing& ring,
		int np,
		int nb
	);

	void removeDeadTriangles(
		std::vector<Triangle>& triangles,
		const std::vector<uint8_t>& dead
	);

	void removeDeadPoints(
		std::vector<Point>& points,
		std::vector<Triangle>& triangles,
		std::vector<Segment>& segments,
		std::vector<double>& h,
		std::vector<uint8_t>& pinned
	);

	void smartEdgeCollapse(
		std::vector<Point>& points,
		std::vector<Triangle>& triangles,
		std::vector<Segment>& segments,
		const std::vector<PointRing>& pRings,
		std::vector<double>& h,
		std::vector<uint8_t>& pinned,
		const Params& params
	);

	void smartEdgeFlip(
		std::vector<Point>& points,
		std::vector<Triangle>& triangles
	);

	void smartSmoothing(
		std::vector<Point>& points,
		std::vector<Triangle>& triangles,
		std::vector<PointRing>& pRings,
		std::vector<double>& h,
		const Params& params
	);

	void postSmoothing(
		std::vector<Point>& points,
		std::vector<Triangle>& triangles,
		std::vector<Segment>& segments,
		std::vector<PointRing>& pRings,
		std::vector<double>& h,
		std::vector<uint8_t>& pinned,
		const Params& params
	);

}
