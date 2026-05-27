#pragma once
#include "imgui.h"
#include "graphics_struct.h"
#include "shader.h"

class BaseSurfaceViewer {
public:

	BaseSurfaceViewer(const char* vertexShaderPath, const char* fragmentShaderPath);

	// public copy variables
	bool pendingCopy = false;
	bool consoleCopy = false;

protected:

	// copy variables
	int pendingCopyWidth = 1600;
	int pendingCopyHeight = 420;

	// simple 2D buffers
	FrameBuffer frameBuffer;	
	FrameBuffer offScreenFBO;
	
	// select data
	const float circleRadius = 3.0f;
	std::vector<SurfacePoint> points;

	// rectangle selection
	bool toggleSelect = false;
	bool isRectReady = false;

	ImVec2 initMouseIndex = ImVec2(0.0f, 0.0f);
	ImVec2 currentMouseIndex = ImVec2(0.0f, 0.0f);
	ImVec2 currentMousePos = ImVec2(0.0f, 0.0f);
	ImVec2 rectPos1 = ImVec2(0.0f, 0.0f);
	ImVec2 rectPos2 = ImVec2(0.0f, 0.0f);

	// shader
	Shader shader;

	// popup variables
	bool openPopUp = false;

	// image dimensions
	int imageWidth, imageHeight;
	ImVec2 zoomCenter = ImVec2(0.5f, 0.5f);
	float zoom = 1.0f;

	float u0 = 0.0f;
	float v0 = 0.0f;
	float u1 = 1.0f;
	float v1 = 1.0f;

	// ======================================================================
	// -----------------------HELPER FUNCTION--------------------------------
	// ======================================================================
	void setImagePadding(ImVec2& padding);

	void updateUV(float halfW, float halfH);

	void clampZoomCenter(float& halfW, float& halfH);

	void toggleSelectedPoint(std::vector<SurfacePoint>& points, ImVec2& dataPos, ImVec2& mousePos, float value);

	void resetView();

	void addHighlightCell(std::vector<int>& indices, int n);

	void highlightCellsInRect(std::vector<int>& indices, ImVec2 cellA, ImVec2 cellB, int nzBase, int nrBase);

	// get physical z and r coordinates at mouse position
	void getMousePhysicalCoord(ImVec2& mousePos, const std::vector<double>& rFace, const std::vector<double> zFace, double& r, double& z);

	ImVec2 screenToUV(const ImVec2& mousePos);

	ImVec2 uvToScreen(const ImVec2& uv);

	ImVec2 gridFaceToScreen(int jFace, int iFace, const std::vector<double>& zFace, const std::vector<double>& rFace);

	ImVec2 getMouseIndex(const std::vector<double>& rFace, const std::vector<double> zFace);

	// turns i,j coordinates to pixel coordinates
	ImVec2 gridToScreen(int jFace, int iFace, const std::vector<double>& rFace, const std::vector<double> zFace);

	// add tooltip to image button when hovered
	void setToolTip(const char* text);

	// ======================================================================
	// -----------------------HANDLE INPUT-----------------------------------
	// ======================================================================
	void handleZoom(ImGuiIO& io);

	void handlePan(ImGuiIO& io);

	void handleRectSelection(ImGuiIO& io);

	void handlePopup();

	void resizeImage(int padx, int pady);

	// ======================================================================
	// -----------------------DRAW CALLS-------------------------------------
	// ======================================================================
	// draw rectangle when mouse is dragged
	void displayRect(int nrBase, int nzBase);

	void drawHighlightedCells(std::vector<int>& indices, const std::vector<double>& zFace, const std::vector<double>& rFace);



	// ======================================================================
	// -----------------------MENU ITEM HANDLES------------------------------
	// ======================================================================
	// add menu items
	void addMenuItemResetView(const char* text);

	void addMenuItemClearPoints(const char* text);

	void addMenuItemCopyToClipboard(const char* text);

	void addMenuItemToggleBool(const char* text, bool& toggle);

	// add image buttons
	void addImageButtonResetView(TextureBuffer& icon, ImVec2 buttonSize);

	void addImageButtonToggleBool(TextureBuffer& icon, ImVec2 buttonSize, bool& toggle);

	void addImageButtonCopyToClipboard(TextureBuffer& icon, ImVec2 buttonSize);

	template<typename T>
	void addImageButtonClearVector(const char* id, TextureBuffer& icon, ImVec2 buttonSize, std::vector<T>& vec) {
		if (ImGui::ImageButton(id, (ImTextureID)(intptr_t)icon.getTextureID(), buttonSize)) {
			vec.clear();
		}
	}
};
