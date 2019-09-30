/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1.
 * Refer to the file_event "COPYING" for details.
 */

#ifndef TSCB_IOREADY_KQUEUE_H
#define TSCB_IOREADY_KQUEUE_H

#include <vector>

#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>

#include <tscb/ioready.h>
#include <tscb/deferred.h>
#include <tscb/file-descriptor-table.h>

namespace tscb {

/**
	\brief Dispatcher for IO readiness events using the
	<TT>kqueue</TT> system call mechanism

	This class supports collecting the IO readiness state of
	a set of file descriptors using the <TT>kevent</TT> system
	call, and dispatching callbacks to receivers that have
	registered themselves for events on specific file descriptors.

	The <TT>kevent</TT> system call provide the fastest
	possible way to observe the state of a set of file descriptors
	on BSD-derived systems; additionally other event
	notifications can be routed through <TT>kqueue</TT>
	as well. Like
	\ref tscb::ioready_dispatcher_epoll "ioready_dispatcher_epoll"
	all relevant operations are O(1), that is: independent from
	the number of descriptors being watched.

	Moreover, the \ref dispatch method can usefully be called by
	multiple threads.
*/
class ioready_dispatcher_kqueue : public ioready_dispatcher {
public:
	ioready_dispatcher_kqueue() throw(std::runtime_error);
	virtual ~ioready_dispatcher_kqueue() noexcept;

	virtual size_t dispatch(const boost::posix_time::time_duration *timeout, int max);

	virtual size_t dispatch_pending(size_t max);

	virtual eventflag *get_eventflag()
		throw(std::runtime_error, std::bad_alloc);

	virtual void register_ioready_callback(ioready_callback *l)
		throw(std::bad_alloc);
	virtual void unregister_ioready_callback(ioready_callback *e)
		noexcept;
	virtual void modify_ioready_callback(ioready_callback *e, ioready_events event_mask)
		noexcept;
protected:
	void drain_queue() noexcept;

	void update_evmask(int fd) noexcept;

	void process_events(struct kevent events[], size_t nevents) noexcept;

	int kqueue_fd;

	void synchronize() noexcept;

	ioready_callback_table callback_tab;

	pipe_eventflag *wakeup_flag;
	std::mutex singleton_mutex;

	deferrable_rwlock guard;
};

}

#endif
