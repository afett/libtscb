/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <string.h>
#include <tscb/config>
#include <tscb/ioready>
#include <tscb/ioready-epoll>

namespace tscb {

	void ioready_callback::disconnect(void) throw()
	{
		cancellation_mutex_.lock();
		ioready_service * tmp = service_.load(std::memory_order_acquire);
		if (tmp) {
			tmp->unregister_ioready_callback(this);
		} else {
			cancellation_mutex_.unlock();
		}
	}

	bool ioready_callback::connected(void) const throw()
	{
		return !!service_.load(std::memory_order_acquire);
	}

	void ioready_callback::modify(ioready_events evmask) throw()
	{
		if (evmask != ioready_none) {
			evmask = evmask | ioready_error | ioready_hangup;
		}
		cancellation_mutex_.lock();
		ioready_service * tmp = service_.load(std::memory_order_acquire);
		if (tmp) {
			tmp->modify_ioready_callback(this, evmask);
		}
		cancellation_mutex_.unlock();
	}

	ioready_callback::~ioready_callback(void) throw()
	{
	}

	ioready_service::~ioready_service(void) throw()
	{
	}

	ioready_dispatcher::~ioready_dispatcher(void) throw()
	{
	}

	ioready_dispatcher *
	ioready_dispatcher::create(void) /* throw(std::bad_alloc, std::runtime_error) */
	{
		return create_ioready_dispatcher_epoll();
	}

}
