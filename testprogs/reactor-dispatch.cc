/* -*- C++ -*-
 * (c) 2011 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <iostream>
#include <memory>

#define private public
#define protected public

#include <assert.h>
#include <unistd.h>

#include <tscb/dispatch>

static bool dummy_timer(int * what, std::chrono::steady_clock::time_point & now)
{
	(*what) ++;
	return false;
}

static void dummy_reader(int * what, int fd, tscb::ioready_events events)
{
	char tmp;
	read(fd, &tmp, 1);
	(*what) ++;
}

static void dummy_work(int * what)
{
	(*what) ++;
}

void test_basic_operation(void)
{
	tscb::posix_reactor reactor;
	
	{
		int timer_called = 0;
		tscb::connection c = reactor.timer(std::bind(dummy_timer, &timer_called, std::placeholders::_1), std::chrono::steady_clock::now());
		reactor.dispatch();
		
		assert(timer_called);
		
		c.disconnect();
	}
	
	{
		int fds[2];
		int reader_called = 0;
		pipe(fds);
		tscb::connection c = reactor.watch(std::bind(dummy_reader, &reader_called, fds[0], std::placeholders::_1), fds[0], tscb::ioready_input);
		reactor.get_eventtrigger().set();
		reactor.dispatch();
		assert(!reader_called);
		
		write(fds[1], "x", 1);
		reactor.get_eventtrigger().set();
		reactor.dispatch();
		assert(reader_called);
		
		c.disconnect();
		
		close(fds[0]);
		close(fds[1]);
	}
	
	{
		int worker_called = 0;
		reactor.post(std::bind(dummy_work, &worker_called));
		reactor.dispatch();
		
		assert(worker_called);
	}
}

static void perpetual_work(tscb::posix_reactor_service & reactor, int * what)
{
	(*what) ++;
	reactor.post(std::bind(perpetual_work, std::ref(reactor), what));
}

void test_workqueue_monopolization(void)
{
	tscb::posix_reactor reactor;
	
	int count = 0;
	perpetual_work(reactor, &count);
	
	while(count < 10)
		reactor.dispatch();
}

void test_pending(void)
{
	tscb::posix_reactor reactor;
	
	assert(reactor.dispatch_pending() == false);
	
	/* timers pending */
	{
		int timer_called = 0;
		std::chrono::steady_clock::time_point due = std::chrono::steady_clock::now() + std::chrono::milliseconds(10);
		tscb::connection c = reactor.timer(std::bind(dummy_timer, &timer_called, std::placeholders::_1), due);
		
		/* registering a new event source may as a side effect cause
		a spurious wakeup, so clear this first */
		while (reactor.dispatch_pending()) { /* nothing */ }
		
		assert(!timer_called);
		
		while (std::chrono::steady_clock::now() < due) {
			usleep(1000);
		}
		
		assert(reactor.dispatch_pending());
		
		assert(timer_called);
		
		c.disconnect();
		/* removal may cause spurious wakeup as well */
		reactor.dispatch_pending_all();
	}
	
	/* io events pending */
	{
		int fds[2];
		int reader_called = 0;
		pipe(fds);
		tscb::connection c = reactor.watch(std::bind(dummy_reader, &reader_called, fds[0], std::placeholders::_1), fds[0], tscb::ioready_input);
		
		/* registering a new event source may as a side effect cause
		a spurious wakeup, so clear this first */
		reactor.dispatch_pending_all();
		
		assert(!reader_called);
		
		write(fds[1], "x", 1);
		
		assert(reactor.dispatch_pending());
		
		assert(reader_called);
		
		c.disconnect();
		
		close(fds[0]);
		close(fds[1]);
		/* removal may cause spurious wakeup as well */
		while(reactor.dispatch_pending()) { /* nothing */ }
	}
	
	/* pending work items */
	{
		int worker_called = 0;
		reactor.post(std::bind(dummy_work, &worker_called));
		
		assert(reactor.dispatch_pending());
		
		assert(worker_called);
		
		assert(!reactor.dispatch_pending());
	}
}

namespace {
void output_something()
{
	std::cerr << "post_class was successful\n";
}

class post_class {
public:
	typedef std::shared_ptr<post_class> pointer;

	~post_class()
	{
		reactor_.post(output_something);
	}

	static pointer create(tscb::posix_reactor_service &reactor)
	{
		return pointer(new post_class(reactor));
	}
private:
	post_class(tscb::posix_reactor_service &reactor)
		: reactor_(reactor)
	{
	}

	tscb::posix_reactor_service &reactor_;
};

void some_dummy_function(post_class::pointer const &)
{
}

}

void test_post_during_dispatch(void)
{
	tscb::posix_reactor reactor;

	reactor.post(std::bind(some_dummy_function, post_class::create(reactor)));
	reactor.dispatch_pending_all();
}

int main()
{
	test_basic_operation();
	test_workqueue_monopolization();
	test_pending();
	test_post_during_dispatch();
}
