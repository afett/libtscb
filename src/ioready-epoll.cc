/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <tscb/ioready-epoll>

#include <unistd.h>
#include <fcntl.h>
#include <sys/fcntl.h>
#include <sys/epoll.h>

#include <tscb/config>
#include <cassert>

namespace tscb {
	
	inline ioready_events ioready_dispatcher_epoll::translate_os_to_tscb(int ev) noexcept
	{
		ioready_events e = ioready_none;
		if (ev & EPOLLIN) e |= ioready_input;
		if (ev & EPOLLOUT) e |= ioready_output;
		/* deliver hangup event to input and output handlers as well */
		if (ev & EPOLLHUP) e |= ioready_input|ioready_output|ioready_hangup|ioready_error;
		if (ev & EPOLLERR) e |= ioready_input|ioready_output|ioready_error;
		return e;
	}
	
	inline int ioready_dispatcher_epoll::translate_tscb_to_os(ioready_events ev) noexcept
	{
		int e = 0;
		if (ev & ioready_input) e |= EPOLLIN;
		if (ev & ioready_output) e |= EPOLLOUT;
		return e;
	}
	
	ioready_dispatcher_epoll::ioready_dispatcher_epoll(void)
		/* throw(std::runtime_error) */
		: wakeup_flag_(nullptr)
	{
#if defined(HAVE_EPOLL1) && defined(EPOLL_CLOEXEC)
		epoll_fd_ = ::epoll_create1(EPOLL_CLOEXEC);
		if (epoll_fd_ >= 0) {
			return;
		}
#endif
		epoll_fd_ = ::epoll_create(1024);
		if (epoll_fd_ < 0) {
			throw std::runtime_error("Unable to create epoll descriptor");
		}
		::fcntl(epoll_fd_, F_SETFL, O_CLOEXEC);
	}
	
	ioready_dispatcher_epoll::~ioready_dispatcher_epoll(void) noexcept
	{
		/* we can assume
		
		- no thread is actively dispatching at the moment
		- no user can register new callbacks at the moment
		
		if those conditions are not met, we are in big trouble anyway, and
		there is no point doing anything about it
		*/
		
		while(lock_.read_lock()) {
			synchronize();
		}
		fdtab_.cancel_all();
		if (lock_.read_unlock()) {
			/* the above cancel operations will cause synchronization
			to be performed at the next possible point in time; if
			there is no concurrent cancellation, this is now */
			synchronize();
		} else {
			/* this can only happen if some callback link was
			cancelled while this object is being destroyed; in
			that case we have to suspend the thread that is destroying
			the object until we are certain that synchronization has
			been performed */
			
			lock_.write_lock_sync();
			synchronize();
			
			/* note that synchronize implicitly calls sync_finished,
			which is equivalent to write_unlock_sync for deferrable_rwlocks */
		}
		
		::close(epoll_fd_);
		
		if (wakeup_flag_.load(std::memory_order_relaxed)) {
			delete wakeup_flag_.load(std::memory_order_relaxed);
		}
	}
	
	void ioready_dispatcher_epoll::process_events(epoll_event events[], size_t nevents, uint32_t cookie)
	{
		read_guard<ioready_dispatcher_epoll> guard(*this);
		
		for(size_t n = 0; n < nevents; ++n) {
			int fd = events[n].data.fd;
			ioready_events ev = translate_os_to_tscb(events[n].events);
			
			fdtab_.notify(fd, ev, cookie);
		}
	}
	
	size_t ioready_dispatcher_epoll::dispatch(const std::chrono::steady_clock::duration * timeout, size_t max)
	{
		pipe_eventflag *evflag = wakeup_flag_.load(std::memory_order_consume);
		
		uint32_t cookie = fdtab_.get_cookie();
		
		int poll_timeout;
		/* need to round up timeout; alas this is the only good way to do it in boost */
		if (timeout) {
			poll_timeout = std::chrono::duration_cast<std::chrono::milliseconds>(
				(*timeout) + std::chrono::milliseconds(1) - std::chrono::steady_clock::duration(1)).count();
		} else {
			poll_timeout = -1;
		}
		
		if (max > 16) {
			max = 16;
		}
		epoll_event events[16];
		
		ssize_t nevents;
		
		if (__builtin_expect(evflag == nullptr, 1)) {
			nevents = ::epoll_wait(epoll_fd_, events, max, poll_timeout);
			
			if (nevents > 0) {
				process_events(events, nevents, cookie);
			} else {
				nevents = 0;
			}
		} else {
			evflag->start_waiting();
			if (evflag->flagged_.load(std::memory_order_relaxed) != 0) {
				poll_timeout = 0;
			}
			nevents = ::epoll_wait(epoll_fd_, events, max, poll_timeout);
			evflag->stop_waiting();
			
			if (nevents > 0) {
				process_events(events, nevents, cookie);
			} else {
				nevents = 0;
			}
			
			evflag->clear();
		}
		return nevents;
	}
	
	size_t ioready_dispatcher_epoll::dispatch_pending(size_t max)
	{
		pipe_eventflag *evflag = wakeup_flag_.load(std::memory_order_consume);
		
		uint32_t cookie = fdtab_.get_cookie();
		
		if (max > 16) {
			max = 16;
		}
		epoll_event events[16];
		
		ssize_t nevents = epoll_wait(epoll_fd_, events, max, 0);
		
		if (nevents > 0) {
			process_events(events, nevents, cookie);
		} else {
			nevents = 0;
		}
		
		if (evflag) {
			evflag->clear();
		}
		
		return nevents;
	}
	
	eventtrigger & ioready_dispatcher_epoll::get_eventtrigger(void)
		/* throw(std::runtime_error, std::bad_alloc) */
	{
		pipe_eventflag * flag = wakeup_flag_.load(std::memory_order_consume);
		if (flag) {
			return *flag;
		}
		
		singleton_mutex_.lock();
		flag = wakeup_flag_.load(std::memory_order_consume);
		if (flag) {
			singleton_mutex_.unlock();
			return *flag;
		}
		
		try {
			flag = new pipe_eventflag();
			watch(
				[this](ioready_events)
				{
					drain_queue();
				},
				flag->readfd_, ioready_input);
		}
		catch (std::bad_alloc) {
			delete flag;
			singleton_mutex_.unlock();
			throw;
		}
		catch (std::runtime_error) {
			delete flag;
			singleton_mutex_.unlock();
			throw;
		}
		
		wakeup_flag_.store(flag, std::memory_order_release);
		singleton_mutex_.unlock();
		
		return *flag;
	}
		
	void ioready_dispatcher_epoll::synchronize(void) noexcept
	{
		ioready_callback * stale = fdtab_.synchronize();
		lock_.sync_finished();
		
		while(stale) {
			ioready_callback * next = stale->inactive_next_;
			stale->cancelled();
			stale->release();
			stale = next;
		}
	}
	
	void ioready_dispatcher_epoll::register_ioready_callback(ioready_callback *link)
		/*throw(std::bad_alloc)*/
	{
		async_write_guard<ioready_dispatcher_epoll> guard(*this);
		
		ioready_events old_mask, new_mask;
		
		try {
			fdtab_.insert(link, old_mask, new_mask);
		}
		catch (std::bad_alloc) {
			delete link;
			throw;
		}
		
		if (new_mask != ioready_none && old_mask != new_mask) {
			epoll_event event;
			event.events = translate_tscb_to_os(new_mask);
			event.data.u64 = 0;
			event.data.fd = link->fd_;

			int op;
			if (old_mask) {
				op = EPOLL_CTL_MOD;
			} else {
				op = EPOLL_CTL_ADD;
			}

			if (::epoll_ctl(epoll_fd_, op, link->fd_, &event) != 0) {
				assert(false && "epoll_ctrl() failed");
			}
		}
		
		link->service_.store(this, std::memory_order_release);
	}
	
	void ioready_dispatcher_epoll::unregister_ioready_callback(ioready_callback *link)
		noexcept
	{
		async_write_guard<ioready_dispatcher_epoll> guard(*this);
		
		if (link->service_.load(std::memory_order_acquire)) {
			int fd = link->fd_;
			ioready_events old_mask, new_mask;
			fdtab_.remove(link, old_mask, new_mask);
			
			if (old_mask) {
				epoll_event event;
				event.data.u64 = 0;
				event.data.fd = fd;
				int op;
				if (new_mask) {
					event.events = translate_tscb_to_os(new_mask);
					op = EPOLL_CTL_MOD;
				} else {
					event.events = translate_tscb_to_os(old_mask);
					op = EPOLL_CTL_DEL;
				}
				if (::epoll_ctl(epoll_fd_, op, fd, &event) != 0) {
					assert(false && "epoll_ctrl() failed");
				}
			}
			
			link->service_.store(nullptr, std::memory_order_release);
		}
		
		link->cancellation_mutex_.unlock();
	}
	
	void ioready_dispatcher_epoll::modify_ioready_callback(ioready_callback *link, ioready_events event_mask)
		/*throw(std::bad_alloc)*/
	{
		async_write_guard<ioready_dispatcher_epoll> guard(*this);
		
		ioready_events old_mask = fdtab_.compute_mask(link->fd_);
		link->event_mask_ = event_mask;
		ioready_events new_mask = fdtab_.compute_mask(link->fd_);
		
		if (old_mask != new_mask) {
			epoll_event event;
			event.data.u64 = 0;
			event.data.fd = link->fd_;
			int op;
			
			if (old_mask) {
				if (new_mask) {
					event.events = translate_tscb_to_os(new_mask);
					op = EPOLL_CTL_MOD;
				} else {
					event.events = translate_tscb_to_os(old_mask);
					op = EPOLL_CTL_DEL;
				}
			} else {
				event.events = translate_tscb_to_os(new_mask);
				op = EPOLL_CTL_ADD;
			}
			if (::epoll_ctl(epoll_fd_, op, link->fd_, &event) != 0) {
				assert(false && "epoll_ctrl() failed");
			}
		}
	}
	
	void ioready_dispatcher_epoll::drain_queue(void) noexcept
	{
	}
	
	ioready_dispatcher *
	create_ioready_dispatcher_epoll(void) throw(std::bad_alloc, std::runtime_error)
	{
		return new ioready_dispatcher_epoll();
	}
	
}
