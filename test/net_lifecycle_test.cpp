#include <assert.h>
#include <stdint.h>

#include "net_lifecycle.h"

int main()
{
	net_lifecycle::Backoff backoff;
	assert(backoff.fail(100) == 2000);
	assert(!backoff.ready(2099));
	assert(backoff.ready(2100));
	assert(backoff.fail(2100) == 4000);
	assert(backoff.fail(6100) == 8000);
	assert(backoff.fail(14100) == 16000);
	assert(backoff.fail(30100) == 30000);
	assert(backoff.fail(60100) == 30000);

	backoff.reset();
	const uint32_t nearWrap = UINT32_MAX - 999;
	assert(backoff.fail(nearWrap) == 2000);
	assert(!backoff.ready(999));
	assert(backoff.ready(1000));

	net_lifecycle::Revision revision;
	assert(revision.get() == 0);
	assert(revision.advance() == 1);
	assert(revision.advance() == 2);
	return 0;
}
