#pragma once
#include "pch.h"

#include "colorbar.h"

#include "buffer_manager.h"
#include "graphics_struct.h"
#include "base_surface_viewer.h"

class Mesh;
class Results;
class Colormap;
struct GridConfig;
class SceneView;

class Inspector : public BaseSurfaceViewer {
public:

	Inspector(SceneView& scene, AppAssets& assets);

	void generate();

	// toolbar is drawn first, then the 2D field is rendered using the remaining space
	void render();

	// upload all uniforms to shader
	void uploadUniforms();

	// copy surface to clipboard
	void copyActiveSurfaceToClipboard();

	Colorbar colorbar;

private:

	// ----------dependencies-----------
	SceneView& scene;
	Mesh& mesh;
	Results& results;
	GridConfig& g;

	// ----------inspector region-----------
	int nrBase = 0;
	int nzBase = 0;

	std::vector<float> quadVertices;

	// ----------resources-----------
	AppAssets& assets;
	VertexBuffer vertexBuffer;

	// ----------image state-----------
	int startX, startY;
	int endX, endY;


	// create a rectangular quad
	void createFullScreenQuad();

	// render the preview onto fbo
	void renderPreview();

	// handle mouse events
	void handleMouse();

	// display text at positions given by textPos
	void displayTextValue();

	// draw toolbar at the top of the inspector, which can be used for variety of functions
	void drawToolBar();

	// draw popup menu when right clicked
	void drawPopup();

};