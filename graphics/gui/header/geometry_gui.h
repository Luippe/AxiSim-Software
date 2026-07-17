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

	Project& project;
	AppAssets& assets;
};
