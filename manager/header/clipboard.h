#pragma once
#include <string>

// Windows clipboard helpers (Win32). These replace the clip library.

// copy UTF-8 text to the clipboard. returns false on failure.
bool copyTextToClipboard(const std::string& text);

// copy a tightly-packed RGBA image to the clipboard.
// the data is expected bottom-up (as produced by glReadPixels) and is made opaque.
// returns false on failure.
bool copyRGBAToClipboard(const unsigned char* rgbaBottomUp, int width, int height);
