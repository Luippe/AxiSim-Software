#pragma once

#include <filesystem>
#include <string>
#include <string_view>

// Locates the resource folders that ship with the app: assets/ (fonts, icons),
// graphics/shaders/ (GLSL) and presets/.
//
// These were opened through bare relative paths ("assets/icons"), which only
// resolve when the app happens to be launched with the repo root as the working
// directory. Running the exe straight out of a build directory -- which is what
// Visual Studio does -- lost every shader, font and icon at once. Resolving
// through here instead makes the launch directory irrelevant.
namespace Resources {

	// Directory holding the running executable. Falls back to the working
	// directory if the exe path can't be read.
	const std::filesystem::path& executableDir();

	// Path to a bundled resource, named relative to the resource root
	// ("assets/fonts/Roboto-Regular.ttf", "graphics/shaders/mesh.vert").
	//
	// Looked for next to the exe first -- the packaged layout that install() in
	// CMakeLists.txt assembles -- then in the source tree, so an exe sitting in
	// out/build/... finds them without a copy step and an edited .frag is picked
	// up on the next run. Returns `relative` unchanged if neither has it, leaving
	// the caller's own file-not-found path to report the plain name.
	std::string resolve(std::string_view relative);

}
