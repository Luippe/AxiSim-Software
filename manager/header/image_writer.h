#pragma once
#include <filesystem>

// Encode a tightly-packed RGBA image to a PNG file, creating/overwriting it.
// The data is expected bottom-up (as produced by glReadPixels) and is flipped to
// PNG's top-down row order on the way out; alpha is forced opaque, matching
// copyRGBAToClipboard. Returns false on failure.
bool writeRGBAToPNG(
	const std::filesystem::path& path,
	const unsigned char* rgbaBottomUp,
	int width,
	int height
);
