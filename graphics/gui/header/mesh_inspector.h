#pragma once
#include "pch.h"

#include "buffer_manager.h"
#include "graphics_struct.h"
#include "base_surface_viewer.h"
#include <glm/fwd.hpp>

class Mesh;
struct GridConfig;

class MeshInspector : public BaseSurfaceViewer {
public:

	MeshInspector(Mesh& mesh, AppAssets& assets);

	void generate();

	// toolbar is drawn first, then the 2D field is rendered using the remaining space
	void render();

	// copy surface to clipboard
	void copyActiveSurfaceToClipboard();

	// create buffer using mesh.gridVertices
	void createGridBuffer();

private:

	// ----------dependencies-----------
	Mesh& mesh;
	GridConfig& g;

	// ----------mesh analyzer region-----------
	int nrBase = 0;
	int nzBase = 0;

	// ----------resources-----------
	AppAssets& assets;
	VertexBuffer vertexBuffer;

	// render the preview onto fbo
	void renderPreview();

	// handle mouse events
	void handleMouse();

	// draw toolbar at the top of the mesh analyzer, which can be used for variety of functions
	void drawToolBar();

	// draw popup menu when right clicked
	void drawPopup();

	// draw text at clicked position
	void drawTextAtSurfacePoint();

	void setBaseNrNz();

};