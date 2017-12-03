/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <tscb/ioready-poll>

#include <sys/fcntl.h>
#include <string.h>

#include <memory>

namespace tscb {

	inline ioready_events ioready_dispatcher_poll::translate_os_to_tscb(int ev) noexcept
	{
		ioready_events e = ioready_none;
		if (ev & POLLIN) {
			e |= ioready_input;
		}
		if (ev & POLLOUT) {
			e |= ioready_output;
		}
		/* deliver hangup event to input and output handlers as well */
		if (ev & POLLHUP) {
			e |= (ioready_input|ioready_output|ioready_hangup|ioready_error);
		}
		if (ev & POLLERR) {
			e |= (ioready_input|ioready_output|ioready_error);
		}
		return e;
	}

	inline int ioready_dispatcher_poll::translate_tscb_to_os(ioready_events ev) noexcept
	{
		int e = 0;
		if (ev & ioready_input) {
			e |= POLLIN;
		}
		if (ev & ioready_output) {
			e |= POLLOUT;
		}
		return e;
	}

	ioready_dispatcher_poll::polltab::polltab(size_t size)
		throw(std::bad_alloc)
		: size_(size), pfd_(new pollfd[size]), old_(nullptr), peer_(nullptr)
	{
	}

	ioready_dispatcher_poll::polltab::~polltab(void)
		noexcept
	{
		delete []pfd_;
	}

	/* dispatcher_poll */

	ioready_dispatcher_poll::ioready_dispatcher_poll(void)
		/*throw(std::bad_alloc, std::runtime_error)*/
		: master_ptab_(nullptr)
	{
		std::unique_ptr<polltab> master_ptab(new polltab(0));
		master_ptab_.store(master_ptab.get(), std::memory_order_relaxed);
		pipe_callback_ = watch(
			[this](ioready_events)
			{
				drain_queue();
			},
			wakeup_flag_.readfd_, ioready_input);
		master_ptab.release();
	}

	ioready_dispatcher_poll::~ioready_dispatcher_poll(void) noexcept
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

		/* FIXME: all other ptabs */
		delete master_ptab_.load(std::memory_order_relaxed);
	}

	eventtrigger & ioready_dispatcher_poll::get_eventtrigger(void) noexcept
	{
		return wakeup_flag_;
	}

	size_t ioready_dispatcher_poll::dispatch(const std::chrono::steady_clock::duration * timeout, size_t max)
	{
		read_guard<ioready_dispatcher_poll> guard(*this);

		uint32_t cookie = fdtab_.get_cookie();

		polltab * ptab = master_ptab_.load(std::memory_order_consume);

		/* need to round up timeout */
		int poll_timeout;
		if (timeout) {
			poll_timeout = std::chrono::duration_cast<std::chrono::milliseconds>(
				(*timeout) + std::chrono::milliseconds(1) - std::chrono::steady_clock::duration(1)).count();
		} else {
			poll_timeout = -1;
		}

		wakeup_flag_.start_waiting();

		if (wakeup_flag_.flagged_.load(std::memory_order_relaxed) != 0) {
			poll_timeout = 0;
		}

		ssize_t count = ::poll(ptab->pfd_, ptab->size_, poll_timeout);

		wakeup_flag_.stop_waiting();

		if (count < 0) {
			count = 0;
		}
		if ((size_t)count > max) {
			count = max;
		}
		size_t n = 0, handled = 0;
		while (count) {
			if (ptab->pfd_[n].revents) {
				int fd = ptab->pfd_[n].fd;
				ioready_events ev = translate_os_to_tscb(ptab->pfd_[n].revents);
				fdtab_.notify(fd, ev, cookie);

				count--;
				handled++;
			}
			n++;
		}

		wakeup_flag_.clear();

		if (lock_.read_unlock()) {
			synchronize();
		}

		return handled;
	}

	size_t ioready_dispatcher_poll::dispatch_pending(size_t max)
	{
		read_guard<ioready_dispatcher_poll> guard(*this);

		polltab * ptab = master_ptab_.load(std::memory_order_consume);

		uint32_t cookie = fdtab_.get_cookie();

		ssize_t count = ::poll(ptab->pfd_, ptab->size_, 0);

		if (count < 0) {
			count = 0;
		}
		if ((size_t)count > max) {
			count = max;
		}

		size_t n = 0, handled = 0;
		while (count) {
			if (ptab->pfd_[n].revents) {
				int fd = ptab->pfd_[n].fd;
				ioready_events ev = translate_os_to_tscb(ptab->pfd_[n].revents);
				fdtab_.notify(fd, ev, cookie);

				count--;
				handled++;
			}
			n++;
		}

		wakeup_flag_.clear();

		if (lock_.read_unlock()) {
			synchronize();
		}

		return handled;
	}

	void ioready_dispatcher_poll::synchronize(void) noexcept
	{
		ioready_callback * stale = fdtab_.synchronize();

		polltab * ptab = master_ptab_.load(std::memory_order_relaxed);
		polltab * discard_ptab = ptab->old_;
		ptab->old_ = nullptr;

		lock_.sync_finished();

		while (stale) {
			ioready_callback * next = stale->inactive_next_;
			stale->cancelled();
			stale->release();
			stale = next;
		}

		while (discard_ptab) {
			polltab * next = discard_ptab->old_;
			delete discard_ptab;
			discard_ptab = next;
		}
	}

	void ioready_dispatcher_poll::update_polltab_entry(int fd, ioready_events mask) /*throw(std::bad_alloc)*/
	{
		polltab * old_ptab = master_ptab_.load(std::memory_order_relaxed);
		int index = -1;

		if ( ((size_t)fd) < polltab_index_.size() ) {
			index = polltab_index_[fd];
		}

		if (index == -1) {
			if (!mask) {
				return;
			}

			/* no entry so far, just create new one */
			while (polltab_index_.size() <= (size_t) fd) {
				polltab_index_.push_back(-1);
			}

			polltab * p = new polltab(old_ptab->size_ + 1);
			for (size_t n = 0; n < old_ptab->size_; ++n) {
				p->pfd_[n].fd = old_ptab->pfd_[n].fd;
				p->pfd_[n].events = old_ptab->pfd_[n].events;
			}

			p->pfd_[p->size_-1].fd = fd;
			p->pfd_[p->size_-1].events = translate_tscb_to_os(mask);

			polltab_index_[fd] = p->size_ - 1;
			p->old_ = old_ptab;

			master_ptab_.store(p, std::memory_order_release);

			return;
		}

		if (mask) {
			old_ptab->pfd_[index].events = translate_tscb_to_os(mask);

			return;
		}

		polltab * p = new polltab(old_ptab->size_ - 1);
		for(size_t n = 0; n < p->size_; ++n) {
			p->pfd_[n].fd = old_ptab->pfd_[n].fd;
			p->pfd_[n].events = old_ptab->pfd_[n].events;
		}

		if (p->size_ > (size_t) index) {
			/* unless deleting last element, move last element into vacant position */
			p->pfd_[index].fd = old_ptab->pfd_[old_ptab->size_ - 1].fd;
			p->pfd_[index].events = old_ptab->pfd_[old_ptab->size_ - 1].events;
		}

		polltab_index_[fd] = -1;
		p->old_ = old_ptab;
		master_ptab_.store(p, std::memory_order_release);
	}

	void ioready_dispatcher_poll::register_ioready_callback(ioready_callback *link)
		/*throw(std::bad_alloc)*/
	{
		{
			async_write_guard<ioready_dispatcher_poll> guard(*this);

			try {
				ioready_events old_mask, new_mask;
				fdtab_.insert(link, old_mask, new_mask);
				if (old_mask != new_mask) {
					update_polltab_entry(link->fd_, new_mask);
				}
			}
			catch (std::bad_alloc) {
				delete link;
				throw;
			}

			link->service_.store(this, std::memory_order_relaxed);
		}

		wakeup_flag_.set();
	}

	void ioready_dispatcher_poll::unregister_ioready_callback(ioready_callback *link)
		noexcept
	{
		{
			async_write_guard<ioready_dispatcher_poll> guard(*this);

			if (link->service_.load(std::memory_order_relaxed)) {
				ioready_events old_mask, new_mask;
				fdtab_.remove(link, old_mask, new_mask);
				if (old_mask != new_mask) {
					update_polltab_entry(link->fd_, new_mask);
				}

				link->service_.store(nullptr, std::memory_order_relaxed);
			}

			link->cancellation_mutex_.unlock();
		}

		wakeup_flag_.set();
	}

	void ioready_dispatcher_poll::modify_ioready_callback(ioready_callback *link, ioready_events event_mask)
	{
		{
			async_write_guard<ioready_dispatcher_poll> guard(*this);

			ioready_events old_mask = link->event_mask_;
			link->event_mask_ = event_mask;
			ioready_events new_events = fdtab_.compute_mask(link->fd_);
			try {
				update_polltab_entry(link->fd_, new_events);
			}
			catch(std::bad_alloc) {
				link->event_mask_ = old_mask;
				throw;
			}
		}

		wakeup_flag_.set();
	}

	void ioready_dispatcher_poll::drain_queue(void) noexcept
	{
	}

	ioready_dispatcher *
	create_ioready_dispatcher_poll(void) throw(std::bad_alloc, std::runtime_error)
	{
		return new ioready_dispatcher_poll();
	}

}
