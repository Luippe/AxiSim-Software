#pragma once
#include <glm/vec3.hpp>
#include <glm/vec2.hpp>
#include "imgui.h"
#include "buffer_manager.h"

// assets for gui icons
struct AppAssets {
	TextureBuffer houseIcon;
	TextureBuffer clearIcon;
	TextureBuffer plusIcon;
	TextureBuffer copyIcon;
	TextureBuffer selectRegionIcon;
	TextureBuffer connectIcon;
	TextureBuffer eraseIcon;
	TextureBuffer rulerIcon;
	TextureBuffer fillCellIcon;
	TextureBuffer drawRectangleIcon;
	TextureBuffer drawCircleIcon;
};

struct AppFonts {
	ImFont* defaultFont = nullptr;
	ImFont* uiFontSmall = nullptr;
	ImFont* uiFontNormal = nullptr;
	ImFont* uiFontLarge = nullptr;
};

struct AppConfig {
	AppAssets assets;
	AppFonts fonts;
};

enum class CompareType {
	LessThan,
	EqualTo,
	GreaterThan,
	Between,
	Exclude
};

enum class ShadingType {
	Interp,
	Flat
};

struct SurfacePoint {

	// i and j indices
	ImVec2 dataPos;

	// optional data
	ImVec2 vecValue;
	float value;
};

struct Vertex {
	glm::vec3 position;
};

struct VertexEdge {
	glm::vec3 p0;
	glm::vec3 p1;
	glm::vec3 c0;
	glm::vec3 c1;
	glm::vec3 color;
};

struct VertexLine {
	glm::vec3 position;
	glm::vec3 color;
};

enum class ViewTab {
	TAB_MESH = 0,
	TAB_SOLVER = 1,
	TAB_RESULTS = 2
};

struct CylinderInstance {
	float x0;
	float x1;
	float innerR;
	float outerR;
};

struct CylinderTemplateVertex {
	glm::vec3 dir;          // (0, cos(theta), sin(theta))
	float xCoord;           // 0 = front, 1 = back
	float radialCoord;      // 0 = inner, 1 = outer
};

struct FilterValues {
	float valueAt = 0.0;
	float valueLower = 0.0;
	float valueUpper = 0.0;
};