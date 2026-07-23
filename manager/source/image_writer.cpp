#include "image_writer.h"

#include <png.h>

#include <fstream>
#include <limits>
#include <vector>

bool writeRGBAToPNG(
	const std::filesystem::path& path,
	const unsigned char* rgbaBottomUp,
	int width,
	int height
) {
	if (path.empty() || !rgbaBottomUp || width <= 0 || height <= 0) {
		return false;
	}

	const size_t stride = static_cast<size_t>(width) * 4;
	if (stride > static_cast<size_t>(std::numeric_limits<png_int_32>::max())) {
		return false;
	}

	// OpenGL returns rows bottom-up. libpng's simplified writer consumes them
	// top-down, so flip the image and force it opaque in one pass.
	std::vector<unsigned char> topDown(stride * static_cast<size_t>(height));
	for (int y = 0; y < height; ++y) {
		const unsigned char* src =
			rgbaBottomUp + static_cast<size_t>(height - 1 - y) * stride;
		unsigned char* dst = topDown.data() + static_cast<size_t>(y) * stride;

		for (int x = 0; x < width; ++x) {
			dst[x * 4 + 0] = src[x * 4 + 0];
			dst[x * 4 + 1] = src[x * 4 + 1];
			dst[x * 4 + 2] = src[x * 4 + 2];
			dst[x * 4 + 3] = 255;
		}
	}

	png_image image{};
	image.version = PNG_IMAGE_VERSION;
	image.width = static_cast<png_uint_32>(width);
	image.height = static_cast<png_uint_32>(height);
	image.format = PNG_FORMAT_RGBA;

	png_alloc_size_t encodedSize = 0;
	if (!png_image_write_to_memory(
		&image,
		nullptr,
		&encodedSize,
		0,
		topDown.data(),
		static_cast<png_int_32>(stride),
		nullptr
	)) {
		png_image_free(&image);
		return false;
	}

	std::vector<unsigned char> encoded(encodedSize);
	if (!png_image_write_to_memory(
		&image,
		encoded.data(),
		&encodedSize,
		0,
		topDown.data(),
		static_cast<png_int_32>(stride),
		nullptr
	)) {
		png_image_free(&image);
		return false;
	}
	png_image_free(&image);

	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out) {
		return false;
	}

	out.write(
		reinterpret_cast<const char*>(encoded.data()),
		static_cast<std::streamsize>(encodedSize)
	);
	return static_cast<bool>(out);
}
