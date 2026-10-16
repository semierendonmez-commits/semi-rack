// Semi-rack addition: DATASCREEN's file loader (Source/data/DataLoader.cpp) without JUCE — the same
// functions as engines/pindata/Hybrid.h's pdata block (bytes, text, WAV audio decoded by the caller;
// images are read as bytes), in namespace dsc.
#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "DataBlob.h"

namespace dsc {
namespace pdata {

inline uint32_t nextSerial() {
	static std::atomic<uint32_t> serials{1};
	return serials.fetch_add(1);
}

// AUDIO: the caller decodes (mono, any rate); normalised, bits = sample above zero.
inline bool buildAudio(DataBlob& b, const std::vector<float>& mono, double rate, int channels) {
	if (mono.empty())
		return false;
	b.symbols = mono;
	float peak = 1.0e-6f;
	for (float s : b.symbols)
		peak = std::max(peak, std::fabs(s));
	for (auto& s : b.symbols)
		s /= peak;
	std::vector<uint8_t> bits(b.symbols.size());
	for (size_t i = 0; i < bits.size(); ++i)
		bits[i] = b.symbols[i] > 0.f ? 1 : 0;
	b.packBits(bits);
	b.used = ReadAs::Audio;
	b.audioRate = rate;
	b.audioChannels = channels;
	b.audioSeconds = rate > 0.0 ? (double) mono.size() / rate : 0.0;
	char buf[64];
	std::snprintf(buf, sizeof buf, "%.1f KHZ  %d CH  %.2f S", rate / 1000.0, channels, b.audioSeconds);
	b.kindNote = buf;
	return true;
}

inline bool isAudioType(const std::string& t) {
	return t == "WAV AUDIO" || t == "AIFF AUDIO" || t == "AIFF-C AUDIO" || t == "FLAC AUDIO" || t == "OGG STREAM" || t == "MP3 AUDIO";
}
inline bool isImageType(const std::string& t) { return t == "PNG IMAGE" || t == "JPEG IMAGE" || t == "GIF IMAGE"; }

// build(): bits / symbols / analysis from b.bytes. `audio` = the decoded samples when the caller
// could decode the file (WAV), else nullptr. Images are not decoded here: read as bytes.
inline void build(DataBlob& b, ReadAs requested, const std::vector<float>* audio = nullptr, double rate = 0.0, int channels = 0) {
	b.analyse();
	b.symbols.clear();
	b.kindNote.clear();
	b.imageW = b.imageH = b.rasterRows = 0;
	b.audioRate = b.audioSeconds = 0.0;
	b.audioChannels = 0;

	ReadAs mode = requested;
	if (mode == ReadAs::Auto) {
		if (isAudioType(b.typeName))
			mode = ReadAs::Audio;
		else if (isImageType(b.typeName))
			mode = ReadAs::Image;
		else if (b.typeName == "TEXT")
			mode = ReadAs::Text;
		else
			mode = ReadAs::Bytes;
	}
	if (mode == ReadAs::Audio && !(audio && buildAudio(b, *audio, rate, channels))) {
		b.kindNote = "NOT DECODABLE AS AUDIO - READ AS BYTES";
		mode = ReadAs::Bytes;
	}
	if (mode == ReadAs::Image) {
		b.kindNote = "IMAGE READ AS BYTES";
		mode = ReadAs::Bytes;
	}
	if (mode == ReadAs::Text) {
		b.bitsFromText();
		b.symbolsFromBytes();
		b.used = ReadAs::Text;
		if (b.kindNote.empty())
			b.kindNote = std::to_string(b.wordCount) + " WORDS  " + std::to_string(b.lineCount) + " LINES";
		if (b.nbits == 0) {
			b.bitsFromBytes();
			b.used = ReadAs::Bytes;
		}
	}
	else if (mode == ReadAs::Bytes) {
		b.bitsFromBytes();
		b.symbolsFromBytes();
		b.used = ReadAs::Bytes;
		if (b.kindNote.empty())
			b.kindNote = "8 BITS / BYTE  MSB FIRST";
	}
	b.serial = nextSerial();
}

inline void setBytes(DataBlob& b, const uint8_t* d, size_t n, size_t cap, const std::string& name, const std::string& path) {
	b.name = name;
	b.path = path;
	size_t dot = name.find_last_of('.');
	b.ext = dot == std::string::npos ? std::string() : name.substr(dot + 1);
	b.bytes.assign(d, d + std::min(n, cap));
	b.fileSize = n;
	b.truncated = n > cap;
}

inline void fromText(DataBlob& b, const char* text, const std::string& name, ReadAs requested) {
	std::string s = text ? text : "";
	if (s.empty())
		s = " ";
	setBytes(b, (const uint8_t*) s.data(), s.size(), s.size(), name, "");
	b.ext = "txt";
	build(b, requested == ReadAs::Auto ? ReadAs::Text : requested);
}

} // namespace pdata

} // namespace dsc
