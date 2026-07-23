#pragma once
#include <string>

// Cross-platform text clipboard plus the native image clipboard where available.

// copy UTF-8 text to the clipboard. returns false on failure.
bool copyTextToClipboard(const std::string& text);

// copy a tightly-packed RGBA image to the clipboard.
// the data is expected bottom-up (as produced by glReadPixels) and is made opaque.
// returns false on failure.
bool copyRGBAToClipboard(const unsigned char* rgbaBottomUp, int width, int height);

// Linux/Wayland and Linux/X11 require different image clipboard protocols; the
// first Linux release exposes PNG export instead of pretending a copy succeeded.
bool imageClipboardSupported();
