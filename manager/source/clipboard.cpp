#include "clipboard.h"

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>

#ifdef _WIN32
#include <windows.h>
#include <cstring>
#endif

bool copyTextToClipboard(const std::string& text) {
	GLFWwindow* window = glfwGetCurrentContext();
	if (!window) {
		return false;
	}

	glfwSetClipboardString(window, text.c_str());
	return true;
}

bool copyRGBAToClipboard(const unsigned char* rgbaBottomUp, int width, int height) {

#ifdef _WIN32

	if (!rgbaBottomUp || width <= 0 || height <= 0) {
		return false;
	}

	const size_t pixelCount = (size_t)width * height;
	const size_t imageSize = pixelCount * 4;

	// CF_DIBV5 header. a positive height means a bottom-up DIB, which already
	// matches the bottom-up data glReadPixels gives us, so no vertical flip is needed.
	BITMAPV5HEADER header = {};
	header.bV5Size = sizeof(BITMAPV5HEADER);
	header.bV5Width = width;
	header.bV5Height = height;
	header.bV5Planes = 1;
	header.bV5BitCount = 32;
	header.bV5Compression = BI_BITFIELDS;
	header.bV5RedMask = 0x00FF0000;   // DIBs store pixels as BGRA in memory
	header.bV5GreenMask = 0x0000FF00;
	header.bV5BlueMask = 0x000000FF;
	header.bV5AlphaMask = 0xFF000000;
	header.bV5CSType = LCS_sRGB;

	HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, sizeof(header) + imageSize);
	if (!hMem) {
		return false;
	}

	unsigned char* dst = static_cast<unsigned char*>(GlobalLock(hMem));
	std::memcpy(dst, &header, sizeof(header));

	// convert RGBA -> BGRA and force the image opaque
	unsigned char* pixels = dst + sizeof(header);
	for (size_t i = 0; i < pixelCount; ++i) {
		pixels[i * 4 + 0] = rgbaBottomUp[i * 4 + 2]; // B
		pixels[i * 4 + 1] = rgbaBottomUp[i * 4 + 1]; // G
		pixels[i * 4 + 2] = rgbaBottomUp[i * 4 + 0]; // R
		pixels[i * 4 + 3] = 255;                     // A (opaque)
	}
	GlobalUnlock(hMem);

	if (!OpenClipboard(nullptr)) {
		GlobalFree(hMem);
		return false;
	}
	EmptyClipboard();

	// Windows synthesizes CF_DIB / CF_BITMAP from CF_DIBV5 automatically.
	bool ok = SetClipboardData(CF_DIBV5, hMem) != nullptr;
	CloseClipboard();

	if (!ok) {
		GlobalFree(hMem);
	}
	return ok;
#else
	(void)rgbaBottomUp;
	(void)width;
	(void)height;
	return false;
#endif
}

bool imageClipboardSupported() {
#ifdef _WIN32
	return true;
#else
	return false;
#endif
}
