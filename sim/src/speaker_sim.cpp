// The speaker, out of SDL rather than out of the ES8311.
//
// Beat is a drum machine and Calm is paced by a gong, so a silent simulator
// hides half of what those two are. This mixes the same calls the firmware
// makes, on the desktop and in the browser, where emscripten maps SDL audio
// onto Web Audio.
//
// A tone is a square wave, which is what a small speaker driven hard sounds
// like and what the firmware is written against. Every voice gets a short
// attack and release, because a square that starts and stops at full amplitude
// clicks, and eight of those a second is worse than silence.
//
// Browsers will not start audio before the page has been interacted with. The
// simulator is driven by the keyboard, so the first keypress is the gesture.

#include <SDL.h>

#include <cmath>
#include <cstring>
#include <vector>

#include "M5Cardputer.h"

namespace {

constexpr int RATE = 22050;
constexpr int CHANNELS = 8;  // what M5Unified offers, and Beat uses one a track
constexpr int RAMP = RATE / 500;  // 2ms, enough to take the click off
constexpr float MAX = 9000.0f;    // headroom for eight voices in an int16

struct Voice {
	bool active = false;
	bool raw = false;

	float phase = 0.0f;  // tone: 0 to 1 across one cycle
	float step = 0.0f;

	std::vector<int8_t> sample;  // raw: copied, since the caller may free it
	float cursor = 0.0f;
	float advance = 1.0f;
	uint32_t repeat = 1;

	int64_t left = 0;  // samples still to play, negative for until stopped
	int64_t played = 0;
	uint8_t volume = 255;
};

SDL_AudioDeviceID device = 0;
Voice voices[CHANNELS];
uint8_t master = 255;

// The callback runs on SDL's own thread on the desktop, so anything the API
// touches is locked around.
void lock()
{
	if (device != 0) {
		SDL_LockAudioDevice(device);
	}
}

void unlock()
{
	if (device != 0) {
		SDL_UnlockAudioDevice(device);
	}
}

float envelope(const Voice &v)
{
	if (v.played < RAMP) {
		return (float)v.played / (float)RAMP;
	}
	if (v.left >= 0 && v.left < RAMP) {
		return (float)v.left / (float)RAMP;
	}
	return 1.0f;
}

void mix(void *, Uint8 *stream, int bytes)
{
	int16_t *out = (int16_t *)stream;
	const int frames = bytes / (int)sizeof(int16_t);
	memset(stream, 0, (size_t)bytes);

	for (int i = 0; i < frames; i++) {
		float sum = 0.0f;
		for (Voice &v : voices) {
			if (!v.active) {
				continue;
			}
			float value = 0.0f;
			if (v.raw) {
				const size_t at = (size_t)v.cursor;
				if (at >= v.sample.size()) {
					if (v.repeat > 1) {
						v.repeat--;
						v.cursor = 0.0f;
					} else {
						v.active = false;
						continue;
					}
				}
				value = (float)v.sample[(size_t)v.cursor] / 127.0f;
				v.cursor += v.advance;
			} else {
				value = v.phase < 0.5f ? 1.0f : -1.0f;
				v.phase += v.step;
				if (v.phase >= 1.0f) {
					v.phase -= 1.0f;
				}
			}

			sum += value * envelope(v) * ((float)v.volume / 255.0f);
			v.played++;
			if (v.left > 0 && --v.left == 0) {
				v.active = false;
			}
		}

		const float scaled = sum * MAX * ((float)master / 255.0f);
		out[i] = (int16_t)(scaled > 32000.0f ? 32000.0f : (scaled < -32000.0f ? -32000.0f : scaled));
	}
}

// -1 means whichever voice is free, which is what M5Unified does.
int pick(int channel)
{
	if (channel >= 0 && channel < CHANNELS) {
		return channel;
	}
	for (int i = 0; i < CHANNELS; i++) {
		if (!voices[i].active) {
			return i;
		}
	}
	return 0;
}

}  // namespace

namespace m5 {

bool Speaker_Class::begin()
{
	if (device != 0) {
		return true;
	}
	if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
		SDL_Log("speaker: no audio (%s)", SDL_GetError());
		return false;
	}
	SDL_AudioSpec want;
	SDL_zero(want);
	want.freq = RATE;
	want.format = AUDIO_S16SYS;
	want.channels = 1;
	want.samples = 512;
	want.callback = mix;

	SDL_AudioSpec have;
	device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
	if (device == 0) {
		SDL_Log("speaker: no device (%s)", SDL_GetError());
		return false;
	}
	SDL_PauseAudioDevice(device, 0);
	return true;
}

void Speaker_Class::end()
{
	if (device == 0) {
		return;
	}
	SDL_CloseAudioDevice(device);
	device = 0;
}

void Speaker_Class::setVolume(uint8_t volume)
{
	lock();
	master = volume;
	unlock();
}

void Speaker_Class::setChannelVolume(uint8_t channel, uint8_t volume)
{
	if (channel >= CHANNELS) {
		return;
	}
	lock();
	voices[channel].volume = volume;
	unlock();
}

bool Speaker_Class::tone(float frequency, uint32_t duration, int channel, bool stopCurrent)
{
	if (!begin() || frequency <= 0.0f) {
		return false;
	}
	lock();
	const int i = pick(channel);
	if (stopCurrent) {
		voices[i] = Voice();
	}
	Voice &v = voices[i];
	v.active = true;
	v.raw = false;
	v.sample.clear();
	v.phase = 0.0f;
	v.step = frequency / (float)RATE;
	v.played = 0;
	// A tone with no duration plays until something stops it.
	v.left = duration == 0 ? -1 : (int64_t)((uint64_t)duration * RATE / 1000);
	unlock();
	return true;
}

bool Speaker_Class::playRaw(const int8_t *data, size_t length, uint32_t rate, bool, uint32_t repeat,
                            int channel, bool stopCurrent)
{
	if (!begin() || data == nullptr || length == 0) {
		return false;
	}
	lock();
	const int i = pick(channel);
	if (stopCurrent) {
		voices[i] = Voice();
	}
	Voice &v = voices[i];
	v.active = true;
	v.raw = true;
	// Copied rather than referenced: Calm frees its gong when the view closes.
	v.sample.assign(data, data + length);
	v.cursor = 0.0f;
	v.advance = (float)rate / (float)RATE;
	v.repeat = repeat == 0 ? 1 : repeat;
	v.played = 0;
	v.left = (int64_t)((float)length / v.advance) * (int64_t)v.repeat;
	unlock();
	return true;
}

void Speaker_Class::stop()
{
	lock();
	for (Voice &v : voices) {
		v = Voice();
	}
	unlock();
}

void Speaker_Class::stop(uint8_t channel)
{
	if (channel >= CHANNELS) {
		return;
	}
	lock();
	voices[channel] = Voice();
	unlock();
}

bool Speaker_Class::isPlaying() const
{
	bool playing = false;
	lock();
	for (const Voice &v : voices) {
		playing = playing || v.active;
	}
	unlock();
	return playing;
}

bool Speaker_Class::isEnabled() const
{
	return device != 0;
}

}  // namespace m5
