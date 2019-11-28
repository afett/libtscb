/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <tscb/config.h>
#include <tscb/ioready.h>

namespace tscb {

ioready_connection::link_type::~link_type() noexcept
{
}

#if 0
void ioready_callback::disconnect() noexcept
{
	cancellation_mutex_.lock();
	ioready_service * tmp = service_.load(std::memory_order_relaxed);
	if (tmp) {
		tmp->unregister_ioready_callback(this);
	} else {
		cancellation_mutex_.unlock();
	}
}

bool ioready_callback::connected() const noexcept
{
	return !!service_.load(std::memory_order_relaxed);
}

void ioready_callback::modify(ioready_events evmask) noexcept
{
	if (evmask != ioready_none) {
		evmask = evmask | ioready_error | ioready_hangup;
	}
	cancellation_mutex_.lock();
	ioready_service * tmp = service_.load(std::memory_order_relaxed);
	if (tmp) {
		tmp->modify_ioready_callback(this, evmask);
	}
	cancellation_mutex_.unlock();
}

ioready_callback::~ioready_callback() noexcept
{
}

#endif

ioready_service::~ioready_service() noexcept
{
}

ioready_dispatcher::~ioready_dispatcher() noexcept
{
}

static ioready_dispatcher *
create_ioready_dispatcher_probe() /*throw(std::bad_alloc, std::runtime_error)*/;

#ifdef HAVE_KQUEUE
ioready_dispatcher *
create_ioready_dispatcher_kqueue() /*throw(std::bad_alloc, std::runtime_error)*/;
#endif
#ifdef HAVE_EPOLL
ioready_dispatcher *
create_ioready_dispatcher_epoll() /*throw(std::bad_alloc, std::runtime_error)*/;
#endif
#ifdef HAVE_POLL
ioready_dispatcher *
create_ioready_dispatcher_poll() /*throw(std::bad_alloc, std::runtime_error)*/;
#endif
#ifdef HAVE_SELECT
ioready_dispatcher *
create_ioready_dispatcher_select() /*throw(std::bad_alloc, std::runtime_error)*/;
#endif

typedef ioready_dispatcher *(*ioready_dispatcher_creator_func_t)();

static ioready_dispatcher_creator_func_t ioready_dispatcher_creator_func
	=&create_ioready_dispatcher_probe;

static ioready_dispatcher_creator_func_t probe_functions[]={
#ifdef HAVE_KQUEUE
	&create_ioready_dispatcher_kqueue,
#endif
#ifdef HAVE_EPOLL
	&create_ioready_dispatcher_epoll,
#endif
#ifdef HAVE_POLL
	&create_ioready_dispatcher_poll,
#endif
#ifdef HAVE_SELECT
	&create_ioready_dispatcher_select,
#endif
	0
};

ioready_dispatcher *
create_ioready_dispatcher_probe() /*throw(std::bad_alloc, std::runtime_error)*/
{
	size_t n=0;
	while(true) {
		ioready_dispatcher_creator_func_t func=probe_functions[n];
		ioready_dispatcher *dispatcher;
		try {
			dispatcher=(*func)();
		}
		catch (std::runtime_error &) {
			n++;
			continue;
		}
		ioready_dispatcher_creator_func=func;
		return dispatcher;
	}
}

ioready_dispatcher *
create_ioready_dispatcher() /*throw(std::bad_alloc, std::runtime_error)*/
{
	return (*ioready_dispatcher_creator_func)();
}

ioready_dispatcher *
ioready_dispatcher::create() /*throw(std::bad_alloc, std::runtime_error)*/
{
	return create_ioready_dispatcher();
}

}
