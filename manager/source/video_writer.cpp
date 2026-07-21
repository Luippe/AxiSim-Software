#include "video_writer.h"

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>

#include <algorithm>

namespace {

	// 100-nanosecond units, which is what Media Foundation measures time in
	constexpr long long hnsPerSecond = 10'000'000LL;

	// Rough target: about a quarter bit per pixel per frame, which holds up for
	// the hard edges and flat colour fields a contour plot is made of. Clamped so
	// a tiny panel still gets a usable stream and a huge one does not run away.
	unsigned int bitrateFor(int width, int height, int fps) {

		long long raw = (long long)width * height * fps / 4;

		return (unsigned int)std::clamp(raw, 2'000'000LL, 40'000'000LL);
	}

	template<typename T>
	void release(T*& p) {
		if (p) {
			p->Release();
			p = nullptr;
		}
	}
}

struct VideoWriter::Impl {
	IMFSinkWriter* writer = nullptr;
	DWORD streamIndex = 0;

	int width = 0;			// even, what is actually encoded
	int height = 0;
	int sourceWidth = 0;	// what the caller hands us, possibly odd
	int fps = 30;

	long long frameDuration = 0;
	long long elapsed = 0;

	bool mfStarted = false;
	bool failed = false;
};

VideoWriter::~VideoWriter() {
	close();
}

bool VideoWriter::isOpen() const {
	return impl != nullptr && impl->writer != nullptr;
}

int VideoWriter::encodedWidth() const {
	return impl ? impl->width : 0;
}

int VideoWriter::encodedHeight() const {
	return impl ? impl->height : 0;
}

bool VideoWriter::open(const std::wstring& path, int width, int height, int fps) {

	if (impl || path.empty() || width <= 1 || height <= 1 || fps <= 0) {
		return false;
	}

	impl = new Impl();

	// H.264 encodes in 2x2 blocks, so odd dimensions are not representable.
	// Dropping the odd column/row is invisible next to rescaling the frame.
	impl->width = width & ~1;
	impl->height = height & ~1;
	impl->sourceWidth = width;
	impl->fps = fps;
	impl->frameDuration = hnsPerSecond / fps;

	HRESULT hr = MFStartup(MF_VERSION);

	if (FAILED(hr)) {
		close();
		return false;
	}

	impl->mfStarted = true;

	IMFMediaType* outType = nullptr;
	IMFMediaType* inType = nullptr;

	hr = MFCreateSinkWriterFromURL(path.c_str(), nullptr, nullptr, &impl->writer);

	// --- what comes out: H.264 in an mp4 container ---
	if (SUCCEEDED(hr)) hr = MFCreateMediaType(&outType);
	if (SUCCEEDED(hr)) hr = outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	if (SUCCEEDED(hr)) hr = outType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
	if (SUCCEEDED(hr)) hr = outType->SetUINT32(MF_MT_AVG_BITRATE, bitrateFor(impl->width, impl->height, fps));
	if (SUCCEEDED(hr)) hr = outType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
	if (SUCCEEDED(hr)) hr = MFSetAttributeSize(outType, MF_MT_FRAME_SIZE, impl->width, impl->height);
	if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(outType, MF_MT_FRAME_RATE, fps, 1);
	if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(outType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
	if (SUCCEEDED(hr)) hr = impl->writer->AddStream(outType, &impl->streamIndex);

	// --- what goes in: uncompressed RGB32, converted for us by the sink writer ---
	if (SUCCEEDED(hr)) hr = MFCreateMediaType(&inType);
	if (SUCCEEDED(hr)) hr = inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	if (SUCCEEDED(hr)) hr = inType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
	if (SUCCEEDED(hr)) hr = inType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);

	// Stated rather than left to the default: for RGB formats a POSITIVE stride
	// means bottom-up rows, which is the order glReadPixels hands us. Saying so
	// explicitly is what keeps the video from coming out vertically mirrored.
	if (SUCCEEDED(hr)) hr = inType->SetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32)(impl->width * 4));
	if (SUCCEEDED(hr)) hr = MFSetAttributeSize(inType, MF_MT_FRAME_SIZE, impl->width, impl->height);
	if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(inType, MF_MT_FRAME_RATE, fps, 1);
	if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(inType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
	if (SUCCEEDED(hr)) hr = impl->writer->SetInputMediaType(impl->streamIndex, inType, nullptr);

	if (SUCCEEDED(hr)) hr = impl->writer->BeginWriting();

	release(inType);
	release(outType);

	if (FAILED(hr)) {
		close();
		return false;
	}

	return true;
}

bool VideoWriter::writeFrame(const unsigned char* rgbaBottomUp) {

	if (!isOpen() || impl->failed || !rgbaBottomUp) {
		return false;
	}

	const DWORD stride = (DWORD)impl->width * 4;
	const DWORD frameBytes = stride * (DWORD)impl->height;

	IMFMediaBuffer* buffer = nullptr;
	IMFSample* sample = nullptr;

	HRESULT hr = MFCreateMemoryBuffer(frameBytes, &buffer);

	if (SUCCEEDED(hr)) {

		BYTE* dst = nullptr;
		hr = buffer->Lock(&dst, nullptr, nullptr);

		if (SUCCEEDED(hr)) {

			// RGB32 with a positive stride is bottom-up, the same convention the
			// DIB in clipboard.cpp relies on -- so the rows go across untouched
			// and only the channel order changes. The source may be wider than
			// what is encoded, hence the separate strides.
			const size_t sourceStride = (size_t)impl->sourceWidth * 4;

			for (int y = 0; y < impl->height; y++) {

				const unsigned char* src = rgbaBottomUp + (size_t)y * sourceStride;
				BYTE* row = dst + (size_t)y * stride;

				for (int x = 0; x < impl->width; x++) {
					row[x * 4 + 0] = src[x * 4 + 2];	// B
					row[x * 4 + 1] = src[x * 4 + 1];	// G
					row[x * 4 + 2] = src[x * 4 + 0];	// R
					row[x * 4 + 3] = 255;				// X (opaque)
				}
			}

			buffer->Unlock();
		}
	}

	if (SUCCEEDED(hr)) hr = buffer->SetCurrentLength(frameBytes);
	if (SUCCEEDED(hr)) hr = MFCreateSample(&sample);
	if (SUCCEEDED(hr)) hr = sample->AddBuffer(buffer);
	if (SUCCEEDED(hr)) hr = sample->SetSampleTime(impl->elapsed);
	if (SUCCEEDED(hr)) hr = sample->SetSampleDuration(impl->frameDuration);
	if (SUCCEEDED(hr)) hr = impl->writer->WriteSample(impl->streamIndex, sample);

	release(sample);
	release(buffer);

	if (FAILED(hr)) {
		impl->failed = true;
		return false;
	}

	impl->elapsed += impl->frameDuration;
	return true;
}

bool VideoWriter::close() {

	if (!impl) {
		return false;
	}

	HRESULT hr = S_OK;

	if (impl->writer) {
		// Finalize is what actually writes the mp4 index; without it the file is
		// left unplayable however many samples went in.
		hr = impl->failed ? E_FAIL : impl->writer->Finalize();
		release(impl->writer);
	}
	else {
		hr = E_FAIL;
	}

	if (impl->mfStarted) {
		MFShutdown();
	}

	delete impl;
	impl = nullptr;

	return SUCCEEDED(hr);
}
