#include "client/audio_capture.h"

// miniaudio.h is defined (with MA_IMPLEMENTATION) in audio.cpp. Including
// without the implementation macro here just pulls declarations.
#include "client/miniaudio.h"

#include <cstdio>
#include <cstring>

namespace solarium {

namespace {

// C callback — miniaudio calls this from its capture thread. Forward into
// the owning AudioCapture via pDevice->pUserData.
void onCaptureCb(ma_device* pDevice, void* /*pOutput*/, const void* pInput, ma_uint32 frameCount) {
	auto* self = static_cast<AudioCapture*>(pDevice->pUserData);
	if (self && pInput) self->onCaptureData(pInput, frameCount);
}

// Little-endian write helpers for the WAV header.
void writeLe32(uint8_t* p, uint32_t v) {
	p[0] = v & 0xff; p[1] = (v >> 8) & 0xff; p[2] = (v >> 16) & 0xff; p[3] = (v >> 24) & 0xff;
}
void writeLe16(uint8_t* p, uint16_t v) {
	p[0] = v & 0xff; p[1] = (v >> 8) & 0xff;
}

} // namespace

struct AudioCapture::Impl {
	ma_device device;
	bool      deviceOpen = false;
};

AudioCapture::~AudioCapture() { shutdown(); }

bool AudioCapture::init() {
	if (m_impl) return true;
	m_impl = new Impl{};

	ma_device_config cfg = ma_device_config_init(ma_device_type_capture);
	cfg.capture.format   = ma_format_s16;
	cfg.capture.channels = kChannels;
	cfg.sampleRate       = kSampleRate;
	cfg.dataCallback     = onCaptureCb;
	cfg.pUserData        = this;
	// Small buffer so startStream → first-sample latency is low.
	cfg.periodSizeInMilliseconds = 30;

	if (ma_device_init(nullptr, &cfg, &m_impl->device) != MA_SUCCESS) {
		std::fprintf(stderr, "[audio-capture] ma_device_init failed — no mic?\n");
		delete m_impl;
		m_impl = nullptr;
		return false;
	}
	m_impl->deviceOpen = true;
	m_deviceReady = true;
	std::printf("[audio-capture] ready: mono %u Hz s16\n", kSampleRate);
	return true;
}

void AudioCapture::shutdown() {
	if (!m_impl) return;
	if (m_impl->deviceOpen) {
		// Stop the stream first if it's running.
		if (ma_device_get_state(&m_impl->device) == ma_device_state_started) {
			ma_device_stop(&m_impl->device);
		}
		ma_device_uninit(&m_impl->device);
	}
	delete m_impl;
	m_impl = nullptr;
	m_deviceReady = false;
	m_capturing   = false;
	std::lock_guard<std::mutex> lk(m_mutex);
	m_samples.clear();
}

bool AudioCapture::startStream() {
	if (!m_deviceReady || !m_impl) return false;
	{
		std::lock_guard<std::mutex> lk(m_mutex);
		m_samples.clear();
		// Reserve 10 s @ 16 kHz so short utterances don't realloc the vector
		// on the capture thread.
		m_samples.reserve(kSampleRate * 10);
	}
	if (ma_device_get_state(&m_impl->device) != ma_device_state_started) {
		if (ma_device_start(&m_impl->device) != MA_SUCCESS) {
			std::fprintf(stderr, "[audio-capture] ma_device_start failed\n");
			return false;
		}
	}
	m_capturing = true;
	return true;
}

void AudioCapture::stopStream() {
	if (!m_impl || !m_capturing) return;
	ma_device_stop(&m_impl->device);
	m_capturing = false;
}

void AudioCapture::onCaptureData(const void* input, uint32_t frameCount) {
	if (!m_capturing) return;
	const int16_t* src = static_cast<const int16_t*>(input);
	std::lock_guard<std::mutex> lk(m_mutex);
	m_samples.insert(m_samples.end(), src, src + frameCount * kChannels);
}

float AudioCapture::bufferedSeconds() const {
	std::lock_guard<std::mutex> lk(m_mutex);
	return (float)m_samples.size() / (float)(kSampleRate * kChannels);
}

std::vector<uint8_t> AudioCapture::drainAsWav() {
	std::vector<int16_t> samples;
	{
		std::lock_guard<std::mutex> lk(m_mutex);
		samples.swap(m_samples);
	}
	if (samples.empty()) return {};

	const uint32_t dataBytes   = (uint32_t)(samples.size() * sizeof(int16_t));
	const uint32_t byteRate    = kSampleRate * kChannels * sizeof(int16_t);
	const uint16_t blockAlign  = kChannels * sizeof(int16_t);
	const uint32_t fileSize    = 36 + dataBytes;

	std::vector<uint8_t> wav;
	wav.resize(44 + dataBytes);
	uint8_t* h = wav.data();

	std::memcpy(h + 0, "RIFF", 4);
	writeLe32(h + 4, fileSize);
	std::memcpy(h + 8, "WAVE", 4);
	std::memcpy(h + 12, "fmt ", 4);
	writeLe32(h + 16, 16);            // PCM fmt chunk size
	writeLe16(h + 20, 1);             // PCM format
	writeLe16(h + 22, (uint16_t)kChannels);
	writeLe32(h + 24, kSampleRate);
	writeLe32(h + 28, byteRate);
	writeLe16(h + 32, blockAlign);
	writeLe16(h + 34, 16);            // bits per sample
	std::memcpy(h + 36, "data", 4);
	writeLe32(h + 40, dataBytes);
	std::memcpy(h + 44, samples.data(), dataBytes);

	return wav;
}

} // namespace solarium
