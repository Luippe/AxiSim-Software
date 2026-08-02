#include "stream_capture.h"

#include <iostream>

StreamCapture::TeeBuffer::TeeBuffer(std::streambuf* downstream, std::string& sink) :
	downstream(downstream),
	sink(sink) {

}

int StreamCapture::TeeBuffer::overflow(int c) {

	if (c == traits_type::eof()) {
		return traits_type::not_eof(c);
	}

	sink.push_back(traits_type::to_char_type(c));

	return downstream ? downstream->sputc(traits_type::to_char_type(c))
	                  : traits_type::not_eof(c);
}

std::streamsize StreamCapture::TeeBuffer::xsputn(const char* s, std::streamsize n) {

	sink.append(s, (std::size_t)n);

	return downstream ? downstream->sputn(s, n) : n;
}

int StreamCapture::TeeBuffer::sync() {
	return downstream ? downstream->pubsync() : 0;
}

StreamCapture::StreamCapture() :
	outBuffer(std::cout.rdbuf(), captured),
	errBuffer(std::cerr.rdbuf(), captured) {

	previousOut = std::cout.rdbuf(&outBuffer);
	previousErr = std::cerr.rdbuf(&errBuffer);
}

StreamCapture::~StreamCapture() {
	std::cout.rdbuf(previousOut);
	std::cerr.rdbuf(previousErr);
}

std::vector<std::string> StreamCapture::lines() const {

	std::vector<std::string> out;
	std::string line;

	// Trailing '\r' is stripped as well: a writer that spelled its newline "\r\n"
	// would otherwise leave a carriage return inside the console entry.
	auto flushLine = [&]() {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		if (!line.empty()) out.push_back(line);
		line.clear();
	};

	for (const char c : captured) {
		if (c == '\n') {
			flushLine();
			continue;
		}
		line.push_back(c);
	}

	// The last write need not have ended in a newline -- report it anyway rather
	// than swallow the one message that matters.
	flushLine();

	return out;
}
