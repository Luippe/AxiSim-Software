#pragma once
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "base_surface_viewer.h"

#include "camera.h"
#include "graphics_struct.h"
#include "sketch_struct.h"

class Project;
class GUI;
struct AppAssets;
class Geometry;

// Pure 2D geometry helpers shared by the sketch view and the trim tool.
namespace sketchmath {
	constexpr double twoPi = 6.28318530717958647692;
	constexpr double sketchEpsilon = 1e-9;

	inline float pixelDistance(ImVec2 a, ImVec2 b) {
		float dx = a.x - b.x;
		float dy = a.y - b.y;

		return std::sqrt(dx * dx + dy * dy);
	}

	inline double dot(Vec2 a, Vec2 b) {
		return a.z * b.z + a.r * b.r;
	}

	inline double cross(Vec2 a, Vec2 b) {
		return a.z * b.r - a.r * b.z;
	}

	inline Vec2 subtract(Vec2 a, Vec2 b) {
		return Vec2{ a.z - b.z, a.r - b.r };
	}

	inline Vec2 interpolate(Vec2 a, Vec2 b, double t) {
		return Vec2{
			a.z + (b.z - a.z) * t,
			a.r + (b.r - a.r) * t
		};
	}

	inline Vec2 pointOnCircle(Vec2 center, double radius, double angle) {
		return Vec2{
			center.z + radius * std::cos(angle),
			center.r + radius * std::sin(angle)
		};
	}

	inline double normalizeAngle(double angle) {
		angle = std::fmod(angle, twoPi);
		if (angle < 0.0) {
			angle += twoPi;
		}

		return angle;
	}

	inline double angleOfPoint(Vec2 center, Vec2 point) {
		return normalizeAngle(std::atan2(point.r - center.r, point.z - center.z));
	}

	inline bool angleOnArc(double angle, const SketchArc& arc) {
		double start = normalizeAngle(arc.startAngle);
		double end = arc.endAngle;
		while (end < start) {
			end += twoPi;
		}

		angle = normalizeAngle(angle);
		if (angle < start) {
			angle += twoPi;
		}

		return angle >= start - 1e-7 && angle <= end + 1e-7;
	}

	inline double arcParameter(double angle, const SketchArc& arc) {
		double start = normalizeAngle(arc.startAngle);
		double end = arc.endAngle;
		while (end < start) {
			end += twoPi;
		}

		angle = normalizeAngle(angle);
		if (angle < start) {
			angle += twoPi;
		}

		double span = end - start;
		if (span <= sketchEpsilon) {
			return 0.0;
		}

		return std::clamp((angle - start) / span, 0.0, 1.0);
	}

	inline double segmentParameter(Vec2 p, Vec2 a, Vec2 b) {
		Vec2 ab = subtract(b, a);
		double len2 = dot(ab, ab);
		if (len2 <= sketchEpsilon) {
			return 0.0;
		}

		return std::clamp(dot(subtract(p, a), ab) / len2, 0.0, 1.0);
	}
}

enum class SnapType {
	None,
	Vertex,
	Line,
	Circle
};

struct SnapResult {
	SnapType type = SnapType::None;
	Vec2 world{};
	ImVec2 screen{};
	float distancePx = 0.0f;
	int entityID = -1;
};

struct DimensionPickResult {
	SketchDimensionType type = SketchDimensionType::LineLength;
	int entityID = -1;
	Vec2 labelPos{};
	float distancePx = 0.0f;
};

struct SketchTrimTarget {
	SketchEntityType type = SketchEntityType::Line;
	int entityID = -1;
	int edgeIndex = -1;
	float distancePx = 0.0f;
};

enum class TrimPreviewGeometry {
	Line,
	Arc,
	Circle
};

struct TrimPreviewResult {
	TrimPreviewGeometry geometry = TrimPreviewGeometry::Line;
	SketchEntityType sourceType = SketchEntityType::Line;
	int entityID = -1;
	int edgeIndex = -1;
	double startT = 0.0;
	double endT = 1.0;
	Vec2 a{};
	Vec2 b{};
	Vec2 center{};
	double radius = 0.0;
	double startAngle = 0.0;
	double endAngle = 0.0;
};

class SketchView : public BaseSurfaceViewer{
public:

	SketchView(Project& project, GUI& gui);

	void render();

private:

	// snapping
	bool toggleSnapping = false;
	bool toggleTrim = false;
	bool toggleEraser = false;

	Vec2 pendingStartWorld;
	Vec2 pendingCurrentWorld;
	int editingDimensionID = -1;
	double dimensionEditValue = 0.0;

	const ImU32 sketchBgColor = IM_COL32(102, 102, 102, 255);
	const ImU32 outlineColor = IM_COL32(150, 150, 150, 255);

	Geometry& geometry;
	AppAssets& assets;
	GUI& gui;
	Camera2D camera;
	bool isDrawingEntity = false;
	bool isSelecting = false;
	std::vector<TrimPreviewResult> selectedTrimSegments;
	SketchNamedSelection pendingNamedSelection;

	void drawToolBar();
	void clearToolToggles();
	void setActiveSketchTool(SketchTool tool);

	void handleSelect();
	void handleMouseAndKey();
	void handleDimensionTool();
	void handleTrimTool();
	void handleErase();
	void handleOpenPopup();

	void drawPopup(ImDrawList* drawList);
	void drawNamedSelectionPopup();
	void drawAxes(ImDrawList* drawList);
	void drawSketchEntities(ImDrawList* drawList);
	void drawTrimPreview(ImDrawList* drawList);
	void drawDimensions(ImDrawList* drawList);
	void drawDimensionEditor();
	void drawTemporarySketch(ImDrawList* drawList);
	void drawPendingSketchEntity(ImDrawList* drawList);

	// used for gui
	
	// draw a circle at the cursor's location if there is a place to snap
	void drawSnapping(ImDrawList* drawList);

	// set initLeftMouse and pendingStartWorld
	void setInitLeftMouse();

	// updates pendingCurrentWorld
	void updateCurrentWorld();

	// returns a SnapResult if a snapping location is found
	std::optional<SnapResult> findSnap(ImVec2 mouse);

	// returns world coordinate of snapped location IF a snapping occurs.
	// If not, just convert the mouse coordinate to world coordinate
	Vec2 getSnappedWorld(ImVec2 mouse);

	std::optional<DimensionPickResult> findDimensionTarget(ImVec2 mouse);
	std::optional<int> findDimensionLabel(ImVec2 mouse);
	std::optional<SketchTrimTarget> findTrimTarget(ImVec2 mouse);
	std::optional<TrimPreviewResult> findTrimPreview(ImVec2 mouse);
	std::vector<TrimPreviewResult> findTrimPreviewsInRegion(SketchBound region);
	std::string getDimensionLabel(const SketchDimension& dimension) const;
	void openDimensionEditor(int dimensionID);
	void prepareNamedSelectionPopup();
	bool savePendingNamedSelection();
	bool trimLineAtMouse(ImVec2 mouse);
	bool trimRectangleAtMouse(ImVec2 mouse);
	bool trimCircleAtMouse(ImVec2 mouse, int circleID);
	bool trimArcAtMouse(ImVec2 mouse, int arcID);
	bool trimSketchAtMouse(ImVec2 mouse);
	bool eraseEntityAtMouse(ImVec2 mouse);
	bool deleteSelectedTrimSegments();
};
