/*
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.
 * Refer to the file "COPYING" for details.
 */

#include <tscb/deferred>

namespace tscb {

	bool deferred_rwlock::read_lock_slow(void) throw()
	{
		writers_.lock();
		if (read_acquire()) {
			writers_.unlock();
			return false;
		}

		return true;
	}

	bool deferred_rwlock::read_unlock_slow(void) throw()
	{
		writers_.lock();
		/* note: if another thread obsevers 1->0 transition, it will
		take the mutex afterwards (and thus serialize with us)

		conversely, a 0->1 transition can only happen with the
		mutex held; therefore, the acquire/release implicit in
		the mutex is sufficient to enforce memory ordering here */
		if (readers_.load(std::memory_order_relaxed) != 0) {
			writers_.unlock();
			return false;
		}

		return true;
	}

	bool deferrable_rwlock::read_lock_slow(void) throw()
	{
		writers_.lock();
		while (waiting_) {
			waiting_ = false;
			waiting_writers_.notify_all();
			writers_.unlock();
			writers_.lock();
		}
		if (read_acquire()) {
			writers_.unlock();
			return false;
		}

		return true;
	}

	bool deferrable_rwlock::read_unlock_slow(void) throw()
	{
		writers_.lock();
		while (waiting_) {
			waiting_ = false;
			writers_.unlock();
			waiting_writers_.notify_all();
			writers_.lock();
		}
		/* note: if another thread obsevers 1->0 transition, it will
		take the mutex afterwards (and thus serialize with us)

		conversely, a 0->1 transition can only happen with the
		mutex held; therefore, the acquire/release implicit in
		the mutex is sufficient to enforce memory ordering here */
		if (readers_.load(std::memory_order_relaxed)!=0) {
			writers_.unlock();
			return false;
		}

		return true;
	}

}
