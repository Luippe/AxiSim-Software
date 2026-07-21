#include "image_writer.h"

#include <windows.h>
#include <wincodec.h>

#include <vector>

// PNG encoding goes through WIC (ships with Windows) rather than a vendored
// encoder: the app is already Win32-only -- see clipboard.cpp's DIBs and
// file_manager.cpp's GetSaveFileNameW -- so this costs one library and no
// third-party source.

namespace {

	// WIC needs an initialized COM apartment and nothing else in the app calls
	// CoInitializeEx, so the writer owns it per call. S_FALSE (already initialized
	// on this thread) still takes a reference and must be balanced;
	// RPC_E_CHANGED_MODE means someone else initialized with a different model --
	// WIC still works, we just must not uninitialize what we do not own.
	class ComScope {
	public:
		ComScope() {
			owns = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
		}

		~ComScope() {
			if (owns) {
				CoUninitialize();
			}
		}

		ComScope(const ComScope&) = delete;
		ComScope& operator=(const ComScope&) = delete;

	private:
		bool owns = false;
	};

	template<typename T>
	void release(T*& p) {
		if (p) {
			p->Release();
			p = nullptr;
		}
	}
}

bool writeRGBAToPNG(const std::wstring& path, const unsigned char* rgbaBottomUp, int width, int height) {

	if (path.empty() || !rgbaBottomUp || width <= 0 || height <= 0) {
		return false;
	}

	ComScope com;

	IWICImagingFactory* factory = nullptr;
	IWICStream* stream = nullptr;
	IWICBitmapEncoder* encoder = nullptr;
	IWICBitmapFrameEncode* frame = nullptr;
	IPropertyBag2* frameProps = nullptr;

	HRESULT hr = CoCreateInstance(
		CLSID_WICImagingFactory,
		nullptr,
		CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(&factory)
	);

	if (SUCCEEDED(hr)) hr = factory->CreateStream(&stream);
	if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
	if (SUCCEEDED(hr)) hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
	if (SUCCEEDED(hr)) hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
	if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(&frame, &frameProps);
	if (SUCCEEDED(hr)) hr = frame->Initialize(frameProps);
	if (SUCCEEDED(hr)) hr = frame->SetSize((UINT)width, (UINT)height);

	// BGRA rather than RGBA because it is the format every WIC encoder supports;
	// SetPixelFormat reports back what was actually taken, and the pixel buffer
	// below has to match it, so a substitution is a hard failure rather than
	// something to silently write garbage into.
	WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
	if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&format);

	if (SUCCEEDED(hr) && !IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA)) {
		hr = E_FAIL;
	}

	if (SUCCEEDED(hr)) {

		const size_t stride = (size_t)width * 4;
		std::vector<unsigned char> topDown(stride * (size_t)height);

		// flip vertically and swizzle RGBA -> BGRA in one pass
		for (int y = 0; y < height; y++) {

			const unsigned char* src = rgbaBottomUp + (size_t)(height - 1 - y) * stride;
			unsigned char* dst = topDown.data() + (size_t)y * stride;

			for (int x = 0; x < width; x++) {
				dst[x * 4 + 0] = src[x * 4 + 2];	// B
				dst[x * 4 + 1] = src[x * 4 + 1];	// G
				dst[x * 4 + 2] = src[x * 4 + 0];	// R
				dst[x * 4 + 3] = 255;				// A (opaque)
			}
		}

		hr = frame->WritePixels(
			(UINT)height,
			(UINT)stride,
			(UINT)topDown.size(),
			topDown.data()
		);
	}

	if (SUCCEEDED(hr)) hr = frame->Commit();
	if (SUCCEEDED(hr)) hr = encoder->Commit();

	release(frameProps);
	release(frame);
	release(encoder);
	release(stream);
	release(factory);

	return SUCCEEDED(hr);
}
