#include "tests.h"
#include <tscb/deferred>

using namespace tscb;

void deferredtests(void)
{
	bool sync;
	deferred_rwlock guard;

	// test simple read locking
	sync=guard.read_lock();
	ASSERT(sync==false);
	sync=guard.read_unlock();
	ASSERT(sync==false);

	// test nested read/read locking
	sync=guard.read_lock();
	ASSERT(sync==false);
	sync=guard.read_lock();
	ASSERT(sync==false);
	sync=guard.read_unlock();
	ASSERT(sync==false);
	sync=guard.read_unlock();
	ASSERT(sync==false);

	// test simple write locking
	sync=guard.write_lock_async();
	ASSERT(sync==true);
	guard.sync_finished();

	// test "nested" read/write locking
	sync=guard.read_lock();
	ASSERT(sync==false);

			// think thread 2
			sync=guard.write_lock_async();
			ASSERT(sync==false);
			guard.write_unlock_async();

	sync=guard.read_unlock();
	ASSERT(sync==true);
	guard.sync_finished();
}

int main()
{
	deferredtests();
}
