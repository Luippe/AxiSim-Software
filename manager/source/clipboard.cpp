#include "clipboard.h"

#include <windows.h>
#include <cstring>

bool copyTextToClipboard(const std::string& text) {

	// convert UTF-8 -> UTF-16 (CF_UNICODETEXT). +1/-1 handles the null terminator.
	int wideLen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
	if (wideLen <= 0) {
		return false;
	}

	HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (size_t)wideLen * sizeof(wchar_t));
	if (!hMem) {
		return false;
	}

	wchar_t* dst = static_cast<wchar_t*>(GlobalLock(hMem));
	MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, dst, wideLen);
	GlobalUnlock(hMem);

	if (!OpenClipboard(nullptr)) {
		GlobalFree(hMem);
		return false;
	}
	EmptyClipboard();

	bool ok = SetClipboardData(CF_UNICODETEXT, hMem) != nullptr;
	CloseClipboard();

	// on success the clipboard owns hMem; only free it if SetClipboardData failed
	if (!ok) {
		GlobalFree(hMem);
	}
	return ok;
}

bool copyRGBAToClipboard(const unsigned char* rgbaBottomUp, int width, int height) {

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
}
