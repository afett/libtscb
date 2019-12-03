/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1.
 * Refer to the file_event "COPYING" for details.
 */

#include <memory>

#include <tscb/dispatch>

namespace tscb {

	void dispatch(tscb::timerqueue_dispatcher *tq,
		tscb::ioready_dispatcher * io)
	{
		/* if there are no timers pending, avoid call to gettimeofday
		it is debatable whether this should be considered fast-path
		or not -- however a mispredicted branch is lost in the noise
		compared to the call to gettimeofday
		*/
		if (__builtin_expect(!tq->timers_pending(), true)) {
			io->dispatch(0);
			return;
		}

		std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
		std::chrono::steady_clock::time_point t = now;
		bool pending;
		do {
			t = now;
			pending = tq->run_queue(t);
			if (!pending) {
				break;
			}
			now = std::chrono::steady_clock::now();
		} while(now >= t);

		if (pending) {
			std::chrono::steady_clock::duration timeout = t - now;
			io->dispatch(&timeout);
		} else io->dispatch(0);
	}

	posix_reactor::posix_reactor(void)
		: io_(create_ioready_dispatcher()),
		trigger_(io_->get_eventtrigger()),
		timer_dispatcher_(trigger_),
		async_workqueue_(trigger_)
	{
	}

	posix_reactor::~posix_reactor(void) noexcept
	{
		delete io_;
	}

	void
	posix_reactor::post(std::function<void(void)> function) /*throw(std::bad_alloc)*/
	{
		{
			std::unique_ptr<workitem> item(new workitem(std::move(function)));
			std::unique_lock<std::mutex> guard(workqueue_lock_);
			workqueue_.push_back(item.get());
			item.release();
		}
		trigger_.set();
	}

	void posix_reactor::register_timer(timer_callback * cb) noexcept
	{
		timer_dispatcher_.register_timer(cb);
	}

	void posix_reactor::unregister_timer(timer_callback * cb) noexcept
	{
		timer_dispatcher_.unregister_timer(cb);
	}

	void
	posix_reactor::register_ioready_callback(ioready_callback * cb) /*throw(std::bad_alloc)*/
	{
		io_->register_ioready_callback(cb);
	}

	void
	posix_reactor::unregister_ioready_callback(ioready_callback * cb) noexcept
	{
		io_->unregister_ioready_callback(cb);
	}

	void
	posix_reactor::modify_ioready_callback(ioready_callback * cb, ioready_events event_mask) /*throw(std::bad_alloc)*/
	{
		io_->modify_ioready_callback(cb, event_mask);
	}

	async_safe_connection
	posix_reactor::async_procedure(std::function<void(void)> function)
	{
		return async_workqueue_.async_procedure(std::move(function));
	}

	eventtrigger &
	posix_reactor::get_eventtrigger(void) /*throw(std::bad_alloc)*/
	{
		return trigger_;
	}

	void
	posix_reactor::dispatch(void)
	{
		if (__builtin_expect(!workqueue_.empty(), 0)) {
			std::unique_lock<std::mutex> guard(workqueue_lock_);
			std::unique_ptr<workitem> item(workqueue_.pop());
			guard.unlock();

			if (item.get()) {
				item->function_();
			}

			guard.lock();
			if (!workqueue_.empty()) {
				trigger_.set();
			}
			guard.unlock();
		}
		async_workqueue_.dispatch();
		tscb::dispatch(&timer_dispatcher_, io_);
	}

	bool
	posix_reactor::dispatch_pending(void)
	{
		bool processed_events = false;

		if (__builtin_expect(!workqueue_.empty(), 0)) {
			std::unique_lock<std::mutex> guard(workqueue_lock_);
			std::unique_ptr<workitem> item(workqueue_.pop());
			guard.unlock();

			if (item.get()) {
				item->function_();
				processed_events = true;
			}

			guard.lock();
			if (!workqueue_.empty()) {
				trigger_.set();
			}
			guard.unlock();
		}

		if (async_workqueue_.dispatch()) {
			processed_events = true;
		}

		std::chrono::steady_clock::time_point first_timer_due;
		if (__builtin_expect(timer_dispatcher_.next_timer(first_timer_due), false)) {
			std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

			if (first_timer_due <= now) {
				processed_events = true;

				timer_dispatcher_.run_queue(now);
			}
		}

		if (io_->dispatch_pending()) {
			processed_events = true;
		}

		return processed_events;
	}

	void posix_reactor::dispatch_pending_all(void)
	{
		while (dispatch_pending()) {
			/* empty */
		}
	}

};
