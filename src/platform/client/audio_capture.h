#pragma once

// AudioCapture — push-to-talk mic capture for the DialogPanel's STT flow.
//
// miniaudio is already implemented in audio.cpp (MA_IMPLEMENTATION there);
// this header/impl link against the same compilation unit, so do NOT define
// MA_IMPLEMENTATION anywhere else.
//
// Usage:
//   AudioCapture cap;
//   cap.init();                    // opens default input device; no stream yet
//   cap.startStream();             // begins filling an internal PCM buffer
//   ... user holds the key ...
//   cap.stopStream();              // stops the stream, leaves data in place
//   std::vector<uint8_t> wav = cap.drainAsWav();  // RIFF-wrapped s16 mono 16kHz
//
// The capture device is mono 16 kHz s16 — whisper's native format, so we
// avoid an extra resample on the client side.

#include <cstdint>
#include <mutex>
#include <vector>

namespace civcraft {

class AudioCapture {
public:
	AudioCapture() = default;
	~AudioCapture();

	AudioCapture(const AudioCapture&) = delete;
	AudioCapture& operator=(const AudioCapture&) = delete;

	// Opens the default input device in s16 mono @ 16 kHz. Returns false on
	// any miniaudio failure; caller should treat STT as disabled in that case.
	bool init();
	void shutdown();

	bool isReady() const { return m_deviceReady; }

	// Begin filling the internal PCM buffer. Cheap — reuses the already-open
	// device. Safe to call again without stopStream (resets the buffer).
	bool startStream();

	// Stop filling the buffer. Samples captured so far remain in m_samples
	// until drainAsWav() consumes them.
	void stopStream();

	bool isCapturing() const { return m_capturing; }

	// Number of seconds of audio currently buffered (thread-safe).
	float bufferedSeconds() const;

	// Build a WAV file in memory (44-byte RIFF header + s16 PCM) and clear the
	// buffer. Returns an empty vector if nothing has been captured.
	std::vector<uint8_t> drainAsWav();

	// Public so miniaudio's C callback can reach it. Do not call directly.
	void onCaptureData(const void* input, uint32_t frameCount);

private:
	// Opaque miniaudio state held on the heap so this header stays free of
	// miniaudio.h (which pulls in thousands of lines).
	struct Impl;
	Impl* m_impl = nullptr;

	mutable std::mutex        m_mutex;
	std::vector<int16_t>      m_samples;
	bool                      m_deviceReady = false;
	bool                      m_capturing   = false;

	static constexpr uint32_t kSampleRate = 16000;
	static constexpr uint32_t kChannels   = 1;
};

} // namespace civcraft
