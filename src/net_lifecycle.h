#pragma once

#include <stdint.h>

namespace net_lifecycle {

constexpr uint32_t RETRY_BASE_MS = 2000;
constexpr uint32_t RETRY_CAP_MS = 30000;

inline bool reached(uint32_t now, uint32_t deadline)
{
	return (int32_t)(now - deadline) >= 0;
}

class Backoff {
public:
	void reset()
	{
		failures_ = 0;
		due_ = 0;
	}

	uint32_t fail(uint32_t now)
	{
		uint32_t delay = RETRY_BASE_MS;
		for (uint8_t i = 0; i < failures_ && delay < RETRY_CAP_MS; i++) {
			delay = delay > RETRY_CAP_MS / 2 ? RETRY_CAP_MS : delay * 2;
		}
		if (failures_ < UINT8_MAX) {
			failures_++;
		}
		due_ = now + delay;
		return delay;
	}

	bool ready(uint32_t now) const
	{
		return reached(now, due_);
	}

private:
	uint8_t failures_ = 0;
	uint32_t due_ = 0;
};

class Revision {
public:
	uint32_t get() const
	{
		return value_;
	}

	uint32_t advance()
	{
		value_++;
		if (value_ == 0) {
			value_++;
		}
		return value_;
	}

private:
	uint32_t value_ = 0;
};

}  // namespace net_lifecycle
