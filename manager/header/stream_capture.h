#pragma once
#include <streambuf>
#include <string>
#include <vector>

// Collects everything written to std::cout and std::cerr for the lifetime of the
// object, so a GUI call site can put it where the user is looking.
//
// The file writers report what went wrong on stderr and nowhere else: they sit below
// the GUI, and the console commands call them too, so they cannot talk to a panel
// directly. In a windowed session that output lands in a console window behind the
// app -- which means an export can fail while the GUI says nothing at all. The
// OpenFOAM export did exactly that: it left a case directory with no controlDict in
// it, and the only symptom reached the user minutes later, out of blockMesh.
//
// Both streams share one buffer, so their relative order survives -- a failure
// reported on stderr stays above the summary line stdout would have written next.
//
// The original stream buffers stay hooked up: the console window keeps its copy.
// printf output is neither captured nor disturbed, since it never passes through
// these buffers.
class StreamCapture {
public:
	StreamCapture();
	~StreamCapture();

	StreamCapture(const StreamCapture&) = delete;
	StreamCapture& operator=(const StreamCapture&) = delete;

	// What has been written so far, one entry per line, newlines stripped. Blank
	// lines are dropped: a console panel shows a list of entries, and a writer
	// spacing out its stderr paragraphs should not push empty rows into it.
	std::vector<std::string> lines() const;

private:

	// Writes through to the stream it replaced and keeps a copy.
	class TeeBuffer : public std::streambuf {
	public:
		TeeBuffer(std::streambuf* downstream, std::string& sink);

	protected:
		int overflow(int c) override;
		std::streamsize xsputn(const char* s, std::streamsize n) override;
		int sync() override;

	private:
		std::streambuf* downstream;
		std::string& sink;
	};

	// Declared before the buffers that reference it.
	std::string captured;

	TeeBuffer outBuffer;
	TeeBuffer errBuffer;

	std::streambuf* previousOut;
	std::streambuf* previousErr;
};
