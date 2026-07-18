#pragma once
#include "imgui.h"

#include <optional>
#include <utility>

#include "base_gui.h"
#include "graphics_struct.h"
#include "sketch_struct.h"

class Project;
struct AppAssets;

class GeometryGUI : public BaseGUI {

public:
	GeometryGUI(Project& project, AppAssets& assets);

	void drawPropertiesPanel();
	void draw();

private:

	// one collapsible group of same-typed entities; rows select on click
	void drawEntityGroup(
		SketchModel& sketch,
		const char* label,
		SketchEntityType type,
		int count
	);

	// the picked entity, or nullopt when nothing in the sketch is selected.
	// Read back off the sketch model rather than mirrored here, so a pick made
	// on the canvas shows up in this panel too.
	std::optional<std::pair<SketchEntityType, int>> selectedEntity(
		const SketchModel& sketch
	) const;

	// natural anchor of an entity (line midpoint / shape center), in base units.
	// nullopt if the id doesn't resolve. Used by the "move to" fields below.
	std::optional<Vec2> entityCenter(
		const SketchModel& sketch,
		SketchEntityType type,
		int entityID
	) const;

	// "Move to a typed location" fields + apply button. Operates on the sketch
	// view's box-select group when there is one (any number of entities), else on
	// a single entity picked from the list. Parks the move on Geometry; SketchView
	// applies it with undo. `picked` is the list selection (may be nullopt).
	void drawMoveSection(std::optional<std::pair<SketchEntityType, int>> picked);

	Project& project;
	AppAssets& assets;

	// target location typed into the move fields, in base units. moveSyncToken
	// identifies the current selection; when it changes, the fields are re-read
	// from that selection's center instead of keeping what the user last typed.
	double moveTargetZ = 0.0;
	double moveTargetR = 0.0;
	long long moveSyncToken = 0;
};
