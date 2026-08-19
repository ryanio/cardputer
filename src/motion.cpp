#include "motion.h"

#include <math.h>

namespace motion {

namespace {

// 50Hz. Fast enough to catch a shake, slow enough that a view which is also
// drawing does not spend its loop on I2C.
constexpr uint32_t SAMPLE_MS = 20;

// Gravity is the slow half of the signal and a shake is the fast half, so one
// low pass splits them: what the filter keeps is which way is down, and what
// it throws away is the shaking. The tilt the views read is smoothed again on
// top of that, because a degree of jitter is visible on a 240px panel.
constexpr float GRAVITY_ALPHA = 0.15f;
constexpr float TILT_ALPHA = 0.30f;

// A rattle, not a nudge. One swing past the threshold is the unit being put
// down; two inside the window is somebody meaning it. The release level in
// between is what makes a swing countable, so a single long push cannot count
// twice.
constexpr float SHAKE_G = 1.10f;
constexpr float RELEASE_G = 0.45f;
constexpr uint32_t SHAKE_WINDOW_MS = 700;
constexpr uint32_t SHAKE_HOLDOFF_MS = 500;

// Below this the unit is sitting on something rather than being held.
constexpr float STILL_G = 0.06f;

// Face up reads about +1g on Z, so face down is the other side of zero. Held
// for long enough that a hand passing over the unit is not a flip.
constexpr float FACE_DOWN_G = -0.75f;
constexpr uint32_t FACE_MS = 400;

// Which way the chip is glued down. M5Unified fixes the axes up per board and
// has no case for a Cardputer at all: IMU_Class.cpp names AtomS3R, CoreS3,
// ChainCaptain and CoreMatrix and stops. So this is a hardware question that
// no amount of reading settles, and it is deliberately four constants in one
// place.
//
// Settled on a unit. Flat and screen up it reads 0, 0, +1g, so Z and the two
// zeroes were already right, but a sample at rest cannot say which way X grows
// and that one was inverted: tipping the right hand edge down rolled the
// marble left and spun the menu backwards. One sign here, and every view that
// reads motion:: turned round with it.
constexpr bool SWAP_XY = false;
constexpr float SIGN_X = -1.0f;  // settled on a unit: X reads the wrong way round
constexpr float SIGN_Y = 1.0f;
constexpr float SIGN_Z = 1.0f;

constexpr float DEG = 57.2957795f;

bool started = false;
bool present = false;
uint32_t sampledAt = 0;

float gx = 0.0f;
float gy = 0.0f;
float gz = 1.0f;
float tiltRoll = 0.0f;
float tiltPitch = 0.0f;

bool swinging = false;
int swings = 0;
uint32_t firstSwing = 0;
uint32_t shakeUntil = 0;
bool shakeLatched = false;

uint32_t movedAt = 0;
bool downNow = false;
bool downCandidate = false;
uint32_t downSince = 0;

// The dead zone. Below it the reading is noise and a view steering on it would
// drift on a flat desk.
constexpr float DEAD_DEG = 3.0f;

float steer(float degrees)
{
	const float span = FULL_TILT - DEAD_DEG;
	if (degrees > DEAD_DEG) {
		const float out = (degrees - DEAD_DEG) / span;
		return out > 1.0f ? 1.0f : out;
	}
	if (degrees < -DEAD_DEG) {
		const float out = (degrees + DEAD_DEG) / span;
		return out < -1.0f ? -1.0f : out;
	}
	return 0.0f;
}

void sample(float ax, float ay, float az)
{
	const float x = SWAP_XY ? ay * SIGN_X : ax * SIGN_X;
	const float y = SWAP_XY ? ax * SIGN_Y : ay * SIGN_Y;
	const float z = az * SIGN_Z;

	gx += (x - gx) * GRAVITY_ALPHA;
	gy += (y - gy) * GRAVITY_ALPHA;
	gz += (z - gz) * GRAVITY_ALPHA;

	// What is left after gravity is the movement, which is the only thing a
	// shake or a stillness test cares about.
	const float dx = x - gx;
	const float dy = y - gy;
	const float dz = z - gz;
	const float energy = sqrtf(dx * dx + dy * dy + dz * dz);
	const uint32_t now = millis();

	if (energy > STILL_G) {
		movedAt = now;
	}

	if (energy > SHAKE_G) {
		if (!swinging) {
			swinging = true;
			if (swings == 0 || now - firstSwing > SHAKE_WINDOW_MS) {
				swings = 1;
				firstSwing = now;
			} else {
				swings++;
			}
			if (swings >= 2 && (int32_t)(now - shakeUntil) > 0) {
				shakeLatched = true;
				shakeUntil = now + SHAKE_HOLDOFF_MS;
				swings = 0;
			}
		}
	} else if (energy < RELEASE_G) {
		swinging = false;
	}

	// atan2 rather than asin, so the angle keeps its sign past vertical and a
	// unit held upright does not fold back on itself.
	const float rollNow = atan2f(gx, gz) * DEG;
	const float pitchNow = atan2f(gy, gz) * DEG;
	tiltRoll += (rollNow - tiltRoll) * TILT_ALPHA;
	tiltPitch += (pitchNow - tiltPitch) * TILT_ALPHA;

	const bool down = gz < FACE_DOWN_G;
	if (down != downCandidate) {
		downCandidate = down;
		downSince = now;
	} else if (down != downNow && now - downSince > FACE_MS) {
		downNow = down;
	}
}

}  // namespace

bool available()
{
	if (!started) {
		started = true;
		present = M5.Imu.isEnabled();
	}
	return present;
}

void update()
{
	if (!available()) {
		return;
	}
	const uint32_t now = millis();
	if (now - sampledAt < SAMPLE_MS) {
		return;
	}
	sampledAt = now;

	float ax = 0.0f;
	float ay = 0.0f;
	float az = 0.0f;
	if (M5.Imu.getAccel(&ax, &ay, &az)) {
		sample(ax, ay, az);
	}
}

float roll()
{
	return tiltRoll;
}

float pitch()
{
	return tiltPitch;
}

float steerX()
{
	return steer(tiltRoll);
}

float steerY()
{
	return steer(tiltPitch);
}

float gravityX()
{
	return gx;
}

float gravityY()
{
	return gy;
}

float gravityZ()
{
	return gz;
}

bool shaken()
{
	const bool was = shakeLatched;
	shakeLatched = false;
	return was;
}

bool faceDown()
{
	return downNow;
}

uint32_t stillFor()
{
	return millis() - movedAt;
}

}  // namespace motion
