/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <tscb/ioready-select>

#include <string.h>

namespace tscb {

	ioready_dispatcher_select::ioready_dispatcher_select(void)
		/* throw(std::bad_alloc, std::runtime_error) */
		: maxfd_(0)
	{
		FD_ZERO(&readfds_);
		FD_ZERO(&writefds_);
		FD_ZERO(&exceptfds_);

		pthread_mutex_init(&fdset_mtx_, NULL);

		watch(
			[this](ioready_events)
			{
				drain_queue();
			},
			wakeup_flag_.readfd_, ioready_input);
	}

	ioready_dispatcher_select::~ioready_dispatcher_select(void) noexcept
	{
		/* we can assume

		- no thread is actively dispatching at the moment
		- no user can register new callbacks at the moment

		if those conditions are not met, we are in big trouble anyway, and
		there is no point doing anything about it
		*/

		while (lock_.read_lock()) {
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
	}

	eventtrigger & ioready_dispatcher_select::get_eventtrigger(void) noexcept
	{
		return wakeup_flag_;
	}

	/* this assumes that fd_set *really* is a bitfield -- which does
	not necessarily have to be true */
	static void copy_fdset(fd_set *dst, const fd_set *src, int maxfd)
	{
		int copybytes = ((maxfd + 63) / 64) * 8;
		memcpy(dst, src, copybytes);
	}

	size_t ioready_dispatcher_select::dispatch(const std::chrono::steady_clock::duration *timeout, size_t max)
	{
		read_guard<ioready_dispatcher_select> guard(*this);

		uint32_t cookie = fdtab_.get_cookie();

		fd_set l_readfds, l_writefds, l_exceptfds;
		int l_maxfd;

		pthread_mutex_lock(&fdset_mtx_);
		l_maxfd = maxfd_;
		copy_fdset(&l_readfds, &readfds_, maxfd_);
		copy_fdset(&l_writefds, &writefds_, maxfd_);
		copy_fdset(&l_exceptfds, &exceptfds_, maxfd_);
		pthread_mutex_unlock(&fdset_mtx_);

		struct timeval tv, * select_timeout;
		if (timeout) {
			uint64_t usecs = std::chrono::duration_cast<std::chrono::microseconds>(
				(*timeout) + std::chrono::microseconds(1) - std::chrono::steady_clock::duration(1)).count();
			tv.tv_sec = usecs/1000000;
			tv.tv_usec = usecs%1000000;
			select_timeout = &tv;
		} else {
			select_timeout = nullptr;
		}

		wakeup_flag_.start_waiting();

		if (wakeup_flag_.flagged_.load(std::memory_order_relaxed) != 0) {
			tv.tv_sec = 0;
			tv.tv_usec = 0;
			select_timeout = &tv;
		}

		int count = select(l_maxfd, &l_readfds, &l_writefds, &l_exceptfds, select_timeout);

		wakeup_flag_.stop_waiting();

		if (count < 0) {
			count = 0;
		}
		if ((size_t)count > max) {
			count = max;
		}

		size_t n = 0, handled = 0;
		while(count) {
			int r = FD_ISSET(n, &l_readfds);
			int w = FD_ISSET(n, &l_writefds);
			int e = FD_ISSET(n, &l_exceptfds);
			if (r | w | e) {
				ioready_events ev = ioready_none;
				if (r) {
					ev = ioready_input;
				}
				if (w) {
					ev |= ioready_output;
				}
				/* deliver exception events to everyone */
				if (e) {
					ev |= ioready_error|ioready_input|ioready_output;
				}

				fdtab_.notify(n, ev, cookie);
				count--;
				handled++;
			}
			n++;
		}

		wakeup_flag_.clear();

		return handled;
	}

	size_t ioready_dispatcher_select::dispatch_pending(size_t max)
	{
		read_guard<ioready_dispatcher_select> guard(*this);

		uint32_t cookie = fdtab_.get_cookie();

		fd_set l_readfds, l_writefds, l_exceptfds;
		int l_maxfd;

		pthread_mutex_lock(&fdset_mtx_);
		l_maxfd = maxfd_;
		copy_fdset(&l_readfds, &readfds_, maxfd_);
		copy_fdset(&l_writefds, &writefds_, maxfd_);
		copy_fdset(&l_exceptfds, &exceptfds_, maxfd_);
		pthread_mutex_unlock(&fdset_mtx_);

		struct timeval tv;
		tv.tv_sec = 0;
		tv.tv_usec = 0;

		int count = select(l_maxfd, &l_readfds, &l_writefds, &l_exceptfds, &tv);

		if (count < 0) {
			count = 0;
		}
		if ((size_t)count > max) {
			count = max;
		}

		size_t n = 0, handled = 0;
		while (count) {
			int r = FD_ISSET(n, &l_readfds);
			int w = FD_ISSET(n, &l_writefds);
			int e = FD_ISSET(n, &l_exceptfds);
			if (r | w | e) {
				ioready_events ev = ioready_none;
				if (r) {
					ev = ioready_input;
				}
				if (w) {
					ev |= ioready_output;
				}
				/* deliver exception events to everyone */
				if (e) {
					ev |= ioready_error|ioready_input|ioready_output;
				}

				fdtab_.notify(n, ev, cookie);
				count--;
				handled++;
			}
			n++;
		}

		wakeup_flag_.clear();

		return handled;
	}

	void ioready_dispatcher_select::register_ioready_callback(ioready_callback *link)
		/*throw(std::bad_alloc)*/
	{
		if (link->fd_ >= (int)FD_SETSIZE) {
			delete link;
			throw std::bad_alloc();
		}

		{
			async_write_guard<ioready_dispatcher_select> guard(*this);

			ioready_events old_events, new_events;
			try {
				fdtab_.insert(link, old_events, new_events);
			}
			catch (std::bad_alloc) {
				delete link;
				throw;
			}
			update_fdsets(link->fd_, new_events);

			link->service_.store(this, std::memory_order_relaxed);
		}

		wakeup_flag_.set();
	}

	void ioready_dispatcher_select::unregister_ioready_callback(ioready_callback *link)
		noexcept
	{
		{
			async_write_guard<ioready_dispatcher_select> guard(*this);

			if (link->service_.load(std::memory_order_relaxed)) {
				ioready_events old_events, new_events;
				fdtab_.remove(link, old_events, new_events);
				update_fdsets(link->fd_, new_events);

				link->service_.store(nullptr, std::memory_order_relaxed);
			}

			link->cancellation_mutex_.unlock();
		}

		wakeup_flag_.set();
	}

	void ioready_dispatcher_select::modify_ioready_callback(ioready_callback *link, ioready_events event_mask)
		/*throw(std::bad_alloc)*/
	{
		{
			async_write_guard<ioready_dispatcher_select> guard(*this);

			link->event_mask_ = event_mask;
			ioready_events new_events = fdtab_.compute_mask(link->fd_);
			update_fdsets(link->fd_, new_events);
		}

		wakeup_flag_.set();
	}

	void ioready_dispatcher_select::update_fdsets(int fd, ioready_events mask) noexcept
	{
		pthread_mutex_lock(&fdset_mtx_);
		if (mask & ioready_input) {
			FD_SET(fd, &readfds_);
		} else {
			FD_CLR(fd, &readfds_);
		}
		if (mask & ioready_output) {
			FD_SET(fd, &writefds_);
		} else {
			FD_CLR(fd, &writefds_);
		}
		if (mask) {
			FD_SET(fd, &exceptfds_);
		} else {
			FD_CLR(fd, &exceptfds_);
		}

		if (mask) {
			if (fd >= maxfd_) maxfd_ = fd + 1;
		} else if (fd == maxfd_ - 1) {
			for (;;) {
				maxfd_--;
				if (!maxfd_ ||
					FD_ISSET(maxfd_ - 1, &readfds_) ||
					FD_ISSET(maxfd_ - 1, &writefds_) ||
					FD_ISSET(maxfd_ - 1, &exceptfds_)) {
					break;
				}
			}
		}
		pthread_mutex_unlock(&fdset_mtx_);
	}

	void ioready_dispatcher_select::synchronize(void) noexcept
	{
		ioready_callback * stale = fdtab_.synchronize();

		lock_.sync_finished();

		while (stale) {
			ioready_callback * next = stale->inactive_next_;
			stale->cancelled();
			stale->release();
			stale = next;
		}
	}

	void ioready_dispatcher_select::drain_queue(void) noexcept
	{
	}

	ioready_dispatcher *
	create_ioready_dispatcher_select(void) throw(std::bad_alloc, std::runtime_error)
	{
		return new ioready_dispatcher_select();
	}

}
