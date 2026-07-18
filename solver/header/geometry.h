#pragma once
#include "sketch_struct.h"

struct Config;

class Geometry {
public:

	Geometry(Config& config);

	SketchModel sketch;

	// Deletion requested from the Geometry panel. The panel can't record undo
	// (SketchView owns the history), so it only parks the request here and
	// SketchView performs it — with undo — on its next frame. id < 0 = nothing
	// pending.
	SketchEntityType pendingDeleteType = SketchEntityType::Line;
	int pendingDeleteID = -1;

	// Move requested from the Geometry panel (translate the selected entity by
	// pendingMoveDelta). Same story as the delete request: parked here, applied
	// with undo by SketchView on its next frame. id < 0 = nothing pending.
	SketchEntityType pendingMoveType = SketchEntityType::Line;
	int pendingMoveID = -1;
	Vec2 pendingMoveDelta{};

	// Summary of the sketch view's multi-selection (the box-select group), pushed
	// here by SketchView each frame so the Geometry panel can offer a "move the
	// whole selection to a location" control without owning that view-side state.
	// selectionRevision changes whenever the selected SET changes, so the panel
	// knows when to re-read selectionCenter into its fields.
	int selectionCount = 0;
	Vec2 selectionCenter{};
	int selectionRevision = 0;

	// Group move parked by the panel: translate the whole sketch-view selection by
	// pendingGroupMoveDelta. Applied (with undo) by SketchView next frame.
	bool pendingGroupMove = false;
	Vec2 pendingGroupMoveDelta{};

	void requestDelete(SketchEntityType type, int entityID) {
		pendingDeleteType = type;
		pendingDeleteID = entityID;
	}

	void requestMove(SketchEntityType type, int entityID, Vec2 delta) {
		pendingMoveType = type;
		pendingMoveID = entityID;
		pendingMoveDelta = delta;
	}

	void requestGroupMove(Vec2 delta) {
		pendingGroupMove = true;
		pendingGroupMoveDelta = delta;
	}

	// blank the geometry for a new project: empty sketch, no pending edits.
	void reset() {
		sketch = SketchModel{};
		pendingDeleteType = SketchEntityType::Line;
		pendingDeleteID = -1;
		pendingMoveType = SketchEntityType::Line;
		pendingMoveID = -1;
		pendingMoveDelta = Vec2{};
		selectionCount = 0;
		selectionCenter = Vec2{};
		selectionRevision = 0;
		pendingGroupMove = false;
		pendingGroupMoveDelta = Vec2{};
	}

};