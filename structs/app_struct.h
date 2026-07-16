#pragma once
#include <string>			// std::wstring in AppSettings
#include <unordered_map>	// icon registry in AppAssets
#include <stdexcept>
#include "imgui.h"			// ImFont in AppFonts
#include "buffer_manager.h"	// TextureBuffer in AppAssets

// GUI icons, loaded recursively from assets/icons at startup (image_buttons/,
// headers/, and any future subfolder) and keyed by file name without extension
// (e.g. "house", "draw-circle"). Add an icon by dropping a PNG under that folder.
struct AppAssets {
	std::unordered_map<std::string, TextureBuffer> icons;

	// look up an icon by name; returns a blank, non-crashing placeholder (and
	// warns once) if no icon with that name was loaded.
	TextureBuffer& icon(const std::string& name);
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
