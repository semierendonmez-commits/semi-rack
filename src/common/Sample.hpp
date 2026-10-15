#pragma once
// Shared sample / file loading for the series (breaks, references, data files).
//
//   semi::openFileDialog(filters, dir, cb)   Rack: osdialog (blocking, UI thread)
//                                            MetaModule: firmware file browser (async, GUI context)
//   semi::readWav(path, out, maxFrames)      PCM 8/16/24/32, float 32/64, EXTENSIBLE; no exceptions
//   semi::readFileBytes(path, out, max)      any file as bytes (data-driven modules)
//   semi::SampleSlot                         lock-free hand-over of a loaded buffer to the audio thread
//   semi::SampleLoader<Module>               request a load from anywhere (menu, dataFromJson); the
//                                            file is read off the audio thread on both platforms
//
// MetaModule rules followed here: no file access in process(), no exceptions, no iostreams,
// slow and fallible disks (every read is checked), paths like "sdc:/breaks/amen.wav".

#include <rack.hpp>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include <osdialog.h>
#ifdef METAMODULE
#include "filesystem/async_filebrowser.hh"
#include "threads/async_thread.hh"
#endif

namespace semi {

// ------------------------------------------------------------------ file dialog

// filters: osdialog syntax, e.g. "WAV:wav,WAV" or "Any file:*". cb gets the chosen path.
inline void openFileDialog(const char* filters, const std::string& dir,
                           std::function<void(const std::string&)> cb) {
	osdialog_filters* f = filters ? osdialog_filters_parse(filters) : nullptr;
#ifdef METAMODULE
	async_osdialog_file(OSDIALOG_OPEN, dir.empty() ? nullptr : dir.c_str(), nullptr, f,
	                    [cb = std::move(cb), f](char* path) {
		                    if (path) {
			                    cb(std::string(path));
			                    std::free(path);
		                    }
		                    if (f)
			                    osdialog_filters_free(f);
	                    });
#else
	char* path = osdialog_file(OSDIALOG_OPEN, dir.empty() ? nullptr : dir.c_str(), nullptr, f);
	if (f)
		osdialog_filters_free(f);
	if (path) {
		cb(std::string(path));
		std::free(path);
	}
#endif
}

inline std::string fileName(const std::string& path) {
	size_t s = path.find_last_of("/\\");
	return s == std::string::npos ? path : path.substr(s + 1);
}

inline std::string dirName(const std::string& path) {
	size_t s = path.find_last_of("/\\");
	return s == std::string::npos ? std::string() : path.substr(0, s);
}

// ------------------------------------------------------------------ WAV

struct Sample {
	std::vector<float> data; // interleaved, -1..1
	int channels = 0;
	double rate = 0.0;
	size_t frames = 0;
	std::string path;

	float at(size_t frame, int ch) const {
		return data[frame * (size_t) channels + (size_t) (ch < channels ? ch : channels - 1)];
	}
	float mono(size_t frame) const {
		float s = 0.f;
		for (int c = 0; c < channels; c++)
			s += data[frame * (size_t) channels + (size_t) c];
		return s / (float) channels;
	}
};

namespace detail {
inline uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t) p[3] << 24); }
inline uint16_t le16(const uint8_t* p) { return (uint16_t) (p[0] | (p[1] << 8)); }
} // namespace detail

// Reads a whole WAV file. maxFrames caps memory (0 = no cap; frames beyond are dropped).
// Returns false (and leaves out untouched) on any error.
inline bool readWav(const std::string& path, Sample& out, size_t maxFrames = 0) {
	FILE* f = std::fopen(path.c_str(), "rb");
	if (!f)
		return false;
	uint8_t hdr[12];
	if (std::fread(hdr, 1, 12, f) != 12 || std::memcmp(hdr, "RIFF", 4) || std::memcmp(hdr + 8, "WAVE", 4)) {
		std::fclose(f);
		return false;
	}
	int fmtTag = 0, ch = 0, bits = 0;
	uint32_t rate = 0;
	bool haveFmt = false;
	uint8_t ck[8];
	while (std::fread(ck, 1, 8, f) == 8) {
		uint32_t size = detail::le32(ck + 4);
		if (!std::memcmp(ck, "fmt ", 4)) {
			uint8_t fmt[40] = {};
			uint32_t n = size < 40 ? size : 40;
			if (std::fread(fmt, 1, n, f) != n)
				break;
			if (size > n)
				std::fseek(f, (long) (size - n), SEEK_CUR);
			fmtTag = detail::le16(fmt);
			ch = detail::le16(fmt + 2);
			rate = detail::le32(fmt + 4);
			bits = detail::le16(fmt + 14);
			if (fmtTag == 0xFFFE && n >= 26)
				fmtTag = detail::le16(fmt + 24); // EXTENSIBLE: sub-format GUID starts with the tag
			haveFmt = true;
		}
		else if (!std::memcmp(ck, "data", 4) && haveFmt) {
			if (ch <= 0 || ch > 8 || rate == 0 || (fmtTag != 1 && fmtTag != 3))
				break;
			const int bps = bits / 8;
			if (bps < 1 || bps > 8)
				break;
			size_t frames = size / (size_t) (bps * ch);
			if (maxFrames && frames > maxFrames)
				frames = maxFrames;
			Sample s;
			s.channels = ch;
			s.rate = rate;
			s.frames = frames;
			s.path = path;
			s.data.resize(frames * (size_t) ch);
			std::vector<uint8_t> buf(4096 * (size_t) (bps * ch));
			size_t done = 0;
			while (done < frames) {
				size_t want = std::min<size_t>(4096, frames - done);
				size_t got = std::fread(buf.data(), (size_t) (bps * ch), want, f);
				for (size_t i = 0; i < got * (size_t) ch; i++) {
					const uint8_t* p = buf.data() + i * (size_t) bps;
					float v = 0.f;
					if (fmtTag == 3 && bps == 4) {
						uint32_t u = detail::le32(p);
						std::memcpy(&v, &u, 4);
					}
					else if (fmtTag == 3 && bps == 8) {
						uint64_t u = (uint64_t) detail::le32(p) | ((uint64_t) detail::le32(p + 4) << 32);
						double d;
						std::memcpy(&d, &u, 8);
						v = (float) d;
					}
					else if (bps == 1)
						v = ((int) p[0] - 128) / 128.f;
					else if (bps == 2)
						v = (int16_t) detail::le16(p) / 32768.f;
					else if (bps == 3)
						v = (float) ((int32_t) ((uint32_t) p[0] << 8 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 24) >> 8) / 8388608.f;
					else if (bps == 4)
						v = (float) ((double) (int32_t) detail::le32(p) / 2147483648.0);
					s.data[done * (size_t) ch + i] = v;
				}
				done += got;
				if (got < want)
					break;
			}
			std::fclose(f);
			if (done == 0)
				return false;
			s.frames = done;
			s.data.resize(done * (size_t) ch);
			out = std::move(s);
			return true;
		}
		else {
			std::fseek(f, (long) (size + (size & 1)), SEEK_CUR);
		}
	}
	std::fclose(f);
	return false;
}

// Any file as raw bytes (capped). For the data-driven modules (PINDATA, IKEDAT, JDAT ...).
inline bool readFileBytes(const std::string& path, std::vector<uint8_t>& out, size_t maxBytes) {
	FILE* f = std::fopen(path.c_str(), "rb");
	if (!f)
		return false;
	std::vector<uint8_t> b;
	uint8_t chunk[4096];
	size_t got;
	while ((got = std::fread(chunk, 1, sizeof chunk, f)) > 0) {
		size_t room = maxBytes - b.size();
		b.insert(b.end(), chunk, chunk + std::min(got, room));
		if (b.size() >= maxBytes)
			break;
	}
	std::fclose(f);
	if (b.empty())
		return false;
	out = std::move(b);
	return true;
}

// ------------------------------------------------------------------ hand-over

// Generic: T is whatever the module builds off-thread (a Sample, an analysed break ...).
// Loader thread: offer(new T) -> audio thread: poll() at block start -> loader: collect().
template <typename T>
struct Slot {
	std::atomic<T*> pending{nullptr};
	std::atomic<T*> retired{nullptr};
	T* active = nullptr; // audio-thread owned

	~Slot() {
		delete pending.exchange(nullptr);
		delete retired.exchange(nullptr);
		delete active;
	}
	// Non-audio thread. Takes ownership.
	void offer(T* t) { delete pending.exchange(t); }
	// Audio thread. Returns true when a new object became active this call.
	bool poll() {
		if (retired.load(std::memory_order_acquire) || !pending.load(std::memory_order_acquire))
			return false;
		T* p = pending.exchange(nullptr, std::memory_order_acq_rel);
		if (!p)
			return false;
		retired.store(active, std::memory_order_release);
		active = p;
		return true;
	}
	// Non-audio thread, call regularly (widget step / async thread / before offer).
	void collect() { delete retired.exchange(nullptr); }
};

using SampleSlot = Slot<Sample>;

// Loads a file off the audio thread on both platforms, builds T with `build(path, T&)` and
// offers it to `slot`. Rack: runs inline (call from the UI thread or dataFromJson).
// MetaModule: queued and run by an AsyncThread owned by the module.
template <typename T>
struct Loader {
	using Build = std::function<bool(const std::string&, T&)>;
	Slot<T>* slot = nullptr;
	Build build;
	std::string lastPath; // shown in menus, saved in the patch
	std::atomic<bool> busy{false};
	std::atomic<bool> failed{false};

#ifdef METAMODULE
	std::string queued;
	std::atomic<bool> hasQueued{false};
	std::unique_ptr<MetaModule::AsyncThread> thread;

	void attach(rack::engine::Module* m, Slot<T>* s, Build b) {
		slot = s;
		build = std::move(b);
		thread = std::make_unique<MetaModule::AsyncThread>(m, [this]() { runQueued(); });
		thread->start();
	}
	void request(const std::string& path) {
		queued = path;
		hasQueued.store(true, std::memory_order_release);
	}
	void runQueued() {
		slot->collect();
		if (!hasQueued.exchange(false, std::memory_order_acq_rel))
			return;
		run(queued);
	}
#else
	void attach(rack::engine::Module*, Slot<T>* s, Build b) {
		slot = s;
		build = std::move(b);
	}
	void request(const std::string& path) {
		slot->collect();
		run(path);
	}
#endif

	void run(const std::string& path) {
		busy = true;
		T* t = new T;
		if (build(path, *t)) {
			lastPath = path;
			failed = false;
			slot->offer(t);
		}
		else {
			failed = true;
			delete t;
		}
		busy = false;
	}
};

using SampleLoader = Loader<Sample>;

// Default builder for plain WAV samples (capped at ~6 min @ 48 kHz stereo).
inline bool buildWav(const std::string& path, Sample& s) { return readWav(path, s, 48000 * 360); }

} // namespace semi
