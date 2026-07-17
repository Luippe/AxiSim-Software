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

	void requestDelete(SketchEntityType type, int entityID) {
		pendingDeleteType = type;
		pendingDeleteID = entityID;
	}

};