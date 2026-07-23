#pragma once
#include "imgui.h"

#include <algorithm>
#include <array>
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

	inline Vec2 translate(Vec2 point, Vec2 delta) {
		return Vec2{ point.z + delta.z, point.r + delta.r };
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

	// End angle brought ahead of the start angle before subtracting, so the span
	// is right for arcs stored with end < start (a trim that wrapped past 0).
	inline double positiveSpan(double startAngle, double endAngle) {
		while (endAngle < startAngle) {
			endAngle += twoPi;
		}

		return endAngle - startAngle;
	}

	inline double arcSpan(const SketchArc& arc) {
		return positiveSpan(arc.startAngle, arc.endAngle);
	}

	// Bring an arc's angles into a canonical form: startAngle in [0, 2*pi) with
	// the span preserved. Normalizing startAngle on its own would drop it below
	// endAngle and inflate the span whenever startAngle >= 2*pi (arcs crossing 0
	// degrees), turning a small piece into a near-full circle.
	inline void normalizeArc(double& startAngle, double& endAngle) {
		double span = positiveSpan(startAngle, endAngle);
		startAngle = normalizeAngle(startAngle);
		endAngle = startAngle + span;
	}

	inline std::array<Vec2, 4> rectangleCorners(const SketchRectangle& rect) {
		return {
			Vec2{ rect.min.z, rect.min.r },
			Vec2{ rect.max.z, rect.min.r },
			Vec2{ rect.max.z, rect.max.r },
			Vec2{ rect.min.z, rect.max.r }
		};
	}

	inline Vec2 rectangleCenter(const SketchRectangle& rect) {
		return Vec2{
			0.5 * (rect.min.z + rect.max.z),
			0.5 * (rect.min.r + rect.max.r)
		};
	}

	// Invokes func(edgeIndex, cornerA, cornerB) for each of the 4 edges.
	template <typename Func>
	void forEachRectEdge(const SketchRectangle& rect, Func&& func) {
		std::array<Vec2, 4> corners = rectangleCorners(rect);
		for (int edge = 0; edge < 4; edge++) {
			func(edge, corners[edge], corners[(edge + 1) % 4]);
		}
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

	// Drawn by GUI into the app-wide toolbar strip above the dockspace, not by
	// render(), so the band can span the whole window instead of this panel.
	void drawToolBar();

	void copyActiveSurfaceToClipboard();

private:

	// snapping
	bool toggleSnapping = false;
	bool toggleTrim = false;
	bool toggleEraser = false;
	bool toggleMoveTo = false;

	Vec2 pendingStartWorld;
	Vec2 pendingCurrentWorld;
	int editingDimensionID = -1;
	double dimensionEditValue = 0.0;

	// dimension label dragging (Dimension tool)
	int draggingDimensionID = -1;      // -1 = no label being dragged
	Vec2 dimensionDragOffset{};        // world offset from the cursor to the label origin
	bool dimensionDragMoved = false;   // true once the drag actually moved the label
	SketchModel dimensionDragBefore;   // sketch snapshot taken at grab, for undo

	Geometry& geometry;
	AppAssets& assets;
	GUI& gui;

	bool isDrawingEntity = false;
	bool isSelecting = false;
	bool isMovingSelection = false;
	Vec2 moveStartWorld{};
	bool hasMoveToBase = false;
	Vec2 moveToBaseWorld{};
	std::vector<TrimPreviewResult> selectedTrimSegments;
	std::vector<TrimPreviewResult> movingTrimSegments;

	// signature of the last-published selection set, so publishSelectionSummary
	// only bumps Geometry::selectionRevision when the selected set actually changes
	size_t lastSelectionSignature = 0;

	// Copy/paste buffer. Holds resolved geometry (world-space), not entity IDs,
	// so the copied shapes survive the originals being edited or deleted.
	// clipboardAnchor is the copy's bounding-box min, used to re-anchor a paste
	// under the cursor.
	std::vector<TrimPreviewResult> clipboardSegments;
	Vec2 clipboardAnchor{};

	std::vector<SketchModel> undoSketchStates;
	std::vector<SketchModel> redoSketchStates;

	void clearToolToggles();
	void setActiveSketchTool(SketchTool tool);
	void recordSketchUndoState(const SketchModel& beforeChange);

	// perform a delete parked by the Geometry panel (see Geometry::requestDelete),
	// recording undo here since this view owns the history
	void consumePendingDelete();

	// perform a move parked by the Geometry panel (see Geometry::requestMove),
	// recording undo here since this view owns the history
	void consumePendingMove();

	// perform a group move parked by the Geometry panel (see
	// Geometry::requestGroupMove): translate the whole box-select group, undo here
	void consumePendingGroupMove();

	// push the box-select group's count/center into Geometry so the panel can show
	// a "move selection" control; bumps selectionRevision when the set changes
	void publishSelectionSummary();

	void restoreSketchState(const SketchModel& state);
	bool undoSketchEdit();
	bool redoSketchEdit();

	bool handleShortcuts(ImGuiIO& io);

	void handleSelect();
	void handleMoveToTool();
	void handleMouseAndKey();

	// drag/edit dimension labels; runs in both Select and Dimension tools
	void handleDimensionLabels();
	void handleDimensionTool();
	void handleTrimTool();
	void handleErase();
	void handleOpenPopup();

	void drawPopup(ImDrawList* drawList);

	// polyline approximation of an arc, shared by the entity, selection and trim
	// draws so all three curve the same way
	void drawArc(
		ImDrawList* drawList,
		Vec2 center,
		double radius,
		double startAngle,
		double endAngle,
		ImU32 color,
		float thickness
	);

	void drawSketchEntities(ImDrawList* drawList);
	void drawTrimPreview(ImDrawList* drawList);
	void drawDimensions(ImDrawList* drawList);
	void drawDimensionEditor();
	void drawTemporarySketch(ImDrawList* drawList);
	void drawPendingSketchEntity(ImDrawList* drawList);

	// draw a circle at the cursor's location if there is a place to snap
	void drawSnapping(ImDrawList* drawList);

	// set initLeftMouse and pendingStartWorld
	void setInitLeftMouse();

	// updates pendingCurrentWorld
	void updateCurrentWorld();

	// returns a SnapResult for the nearest sketch feature (vertex/edge)
	std::optional<SnapResult> findSnap(ImVec2 mouse);

	// full snap decision used for both getSnappedWorld() and the snap preview:
	// findSnap(), but when the grid is shown a nearby grid vertex takes priority
	// over sketch edges (a closer sketch vertex still wins)
	std::optional<SnapResult> resolveSnap(ImVec2 mouse);

	// returns world coordinate of snapped location IF a snapping occurs.
	// If not, just convert the mouse coordinate to world coordinate
	Vec2 getSnappedWorld(ImVec2 mouse);

	std::optional<DimensionPickResult> findDimensionTarget(ImVec2 mouse);
	std::optional<int> findDimensionLabel(ImVec2 mouse);
	std::optional<SketchTrimTarget> findTrimTarget(ImVec2 mouse);
	std::optional<TrimPreviewResult> findTrimPreview(ImVec2 mouse);
	std::vector<TrimPreviewResult> findTrimPreviewsInRegion(SketchBound region);
	bool hoveredSelectedTrimSegment(ImVec2 mouse);
	std::optional<Vec2> findSelectedMovePoint(ImVec2 mouse);
	std::string getDimensionLabel(const SketchDimension& dimension) const;
	void openDimensionEditor(int dimensionID);

	// start/continue dragging a dimension's label around (Dimension tool)
	void beginDimensionLabelDrag(int dimensionID);
	void updateDimensionLabelDrag();
	bool trimLineAtMouse(ImVec2 mouse);
	bool trimRectangleAtMouse(ImVec2 mouse);
	bool trimCircleAtMouse(ImVec2 mouse, int circleID);
	bool trimArcAtMouse(ImVec2 mouse, int arcID);
	bool trimSketchAtMouse(ImVec2 mouse);
	bool eraseEntityAtMouse(ImVec2 mouse);
	bool deleteSelectedTrimSegments();
	bool moveSelectedTrimSegments(Vec2 delta);

	// copy the current selection into clipboardSegments; false if nothing usable
	bool copySelectedTrimSegments();

	// add a translated copy of clipboardSegments to the sketch and select it,
	// leaving the originals alone. Undo is the caller's job.
	bool pasteClipboardSegments(Vec2 delta);

	// The whole paste action, shared by the toolbar button and the chord: land the
	// clipboard's lower-left corner on `target`, switch to Select (the paste comes
	// out selected, and dragging or deleting it only works there), and record undo.
	// Any half-drawn entity on the old tool is abandoned.
	bool pasteClipboardAt(Vec2 target);

	// Where a paste lands with no cursor over the canvas to anchor to: one nudge
	// down-right of where the copy was taken from, so it doesn't come down exactly
	// on the original and read as nothing having happened. This is what a paste
	// from the toolbar always uses — the cursor is up on the button.
	Vec2 offsetPasteTarget() const;
};
