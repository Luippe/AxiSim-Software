#pragma once
#include <string>			// std::wstring in AppSettings
#include <stdexcept>
#include "imgui.h"			// ImFont in AppFonts
#include "buffer_manager.h"	// TextureBuffer in AppAssets

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
	TextureBuffer drawLineIcon;
	TextureBuffer selectIcon;
	TextureBuffer trimIcon;
	TextureBuffer crossArrowIcon;
	TextureBuffer gridIcon;
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

struct AppSettings {
	std::wstring quickLaunch;
};


struct AppError : std::runtime_error {
	using std::runtime_error::runtime_error;
};
