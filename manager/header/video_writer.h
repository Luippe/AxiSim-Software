#pragma once
#include <filesystem>

// Encodes frames to an H.264 .mp4 through Media Foundation, which ships with
// Windows -- so nothing has to be bundled and the user needs no ffmpeg on PATH.
// Same reasoning as image_writer.cpp using WIC for PNG.
//
// Open once, push every frame, close. Frames are tightly-packed RGBA, bottom-up
// (as glReadPixels produces), and must all be the size passed to open(). Alpha
// is ignored -- video is opaque.
class VideoWriter {
public:

	VideoWriter() = default;
	~VideoWriter();

	VideoWriter(const VideoWriter&) = delete;
	VideoWriter& operator=(const VideoWriter&) = delete;

	// H.264 only encodes even dimensions, so an odd width or height loses its
	// last column/row. Returns false if the encoder could not be set up -- most
	// likely a Windows N/KN edition with no Media Feature Pack installed.
	bool open(const std::filesystem::path& path, int width, int height, int fps);

	// MP4 uses Media Foundation for now. Linux builds keep PNG-sequence export
	// available and report MP4 as unsupported until an FFmpeg backend is added.
	static bool supported();

	bool isOpen() const;

	// Appends one frame. False on the first failure; the file is then unusable
	// and close() should still be called to release the encoder.
	bool writeFrame(const unsigned char* rgbaBottomUp);

	// Finalizes the container. Safe to call twice, and called by the destructor,
	// but call it explicitly to find out whether the file was written.
	bool close();

	// dimensions actually being encoded (open()'s, rounded down to even)
	int encodedWidth() const;
	int encodedHeight() const;

private:

	// keeps windows.h / mfapi.h out of every TU that only wants to write a video
	struct Impl;
	Impl* impl = nullptr;
};
