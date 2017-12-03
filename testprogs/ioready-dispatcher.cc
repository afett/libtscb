/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#define private public
#define protected public

#include "tests.h"

#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>

#include <tscb/ioready>

using namespace tscb;

void function(int *closure, int fd, int event)
{
	char c;
	::read(fd, &c, 1);
	*closure = 1;
}

class Target {
public:
	Target(void) : called(false) {}
	void function(int event)
	{
		called = true;
	}
	bool called;
};

class Target2;

static inline void intrusive_ptr_add_ref(Target2 *t) throw();
static inline void intrusive_ptr_release(Target2 *t) throw();

class Target2 {
public:
	Target2(ioready_service *srv, int fd)
		: called(false), refcount(1)
	{
		link=srv->watch(std::bind(&Target2::input, tscb::intrusive_ptr<Target2>(this), fd, std::placeholders::_1), fd, ioready_input);
		ASSERT(refcount == 2);
	}

	void input(int fd, int event)
	{
		char c;
		read(fd, &c, 1);
		called = true;
		link.disconnect();
		ASSERT(refcount == 2);
	}

	void pin(void)
	{
		refcount++;
	}

	void release(void)
	{
		refcount--;
	}

	ioready_connection link;
	bool called;
	int refcount;
};

static inline void intrusive_ptr_add_ref(Target2 *t) throw()
{
	t->pin();
}

static inline void intrusive_ptr_release(Target2 *t) throw()
{
	t->release();
}

void test_dispatcher(ioready_dispatcher *d)
{
	std::chrono::steady_clock::duration t = std::chrono::milliseconds(0);
	/* verify that an empty dispatcher in fact does nothing */
	{
		int count=d->dispatch(&t);
		ASSERT(count == 0);
	}
	/* verify that basic dispatching and cancellation works */
	{
		int pipefd[2];

		int oserror=pipe(pipefd);
		ASSERT(oserror==0);

		int called = 0;

		tscb::ioready_connection link = d->watch(std::bind(function, &called, pipefd[0], std::placeholders::_1),
			pipefd[0], ioready_input);

		ASSERT(link.callback_->refcount_ == 2);

		int count=d->dispatch(&t);
		ASSERT(count == 0);

		write(pipefd[1], &count, 1);
		count=d->dispatch(&t);
		ASSERT(count == 1);
		ASSERT(called == 1);

		called = 0;
		link.modify(ioready_none);
		write(pipefd[1], &count, 1);
		count=d->dispatch(&t);
		ASSERT(count == 0);
		ASSERT(called == 0);

		called = 0;
		link.modify(ioready_input);
		count=d->dispatch(&t);
		ASSERT(count == 1);
		ASSERT(called == 1);

		write(pipefd[1], &count, 1);
		called = 0;
		tscb::intrusive_ptr<ioready_callback> cb(link.callback_);
		link.disconnect();
		count=d->dispatch(&t);
		ASSERT(count == 0);
		ASSERT(called == 0);

		ASSERT(cb->refcount_==1);

		close(pipefd[0]);
		close(pipefd[1]);
	}
	{
		int pipefd[2];

		int oserror=pipe(pipefd);
		ASSERT(oserror==0);

		Target target;

		tscb::connection link=d->watch(std::bind(&Target::function, &target, std::placeholders::_1),
			pipefd[0], ioready_input);

		int count;
		write(pipefd[1], &count, 1);
		count=d->dispatch(&t);
		ASSERT(count == 1);
		ASSERT(target.called == 1);

		link.disconnect();
		count=d->dispatch(&t);
		ASSERT(count == 0);

		close(pipefd[0]);
		close(pipefd[1]);
	}

	/* verify that a callback can cancel itself */
	{
		int pipefd[2];

		int oserror=pipe(pipefd);
		ASSERT(oserror==0);

		Target2 target(d, pipefd[0]);

		int count;
		write(pipefd[1], &count, 1);
		count=d->dispatch(&t);
		ASSERT(count == 1);
		ASSERT(target.called == 1);
		ASSERT(target.refcount == 1);

		write(pipefd[1], &count, 1);
		count=d->dispatch(&t);
		ASSERT(count == 0);

		close(pipefd[0]);
		close(pipefd[1]);
	}
}

class pipe_swapper {
public:
	void handle_pipe1(ioready_events events)
	{
		char c;
		ssize_t count = read(pipe1[0], &c, 1);
		if (count == 0) {
			events |= ioready_hangup;
		}
		ASSERT((events & ioready_hangup) != 0);
		conn.disconnect();
		close(pipe1[0]);
		dup2(pipe2[0], pipe1[0]);
		conn = d->watch(std::bind(&pipe_swapper::handle_pipe2, this, std::placeholders::_1), pipe1[0], ioready_input);
	}

	void handle_pipe2(ioready_events events)
	{
		char c;
		ssize_t count = read(pipe1[0], &c, 1);
		if (count == 0) {
			events |= ioready_hangup;
		}
		ASSERT(count == 1);
		ASSERT( !(events & ioready_hangup) );
		ASSERT(events & ioready_input);
		conn.disconnect();
		finished = true;
	}

	int pipe1[2], pipe2[2];
	connection conn;
	ioready_dispatcher * d;
	bool finished;
};

void test_dispatcher_sync_disconnect(ioready_dispatcher * d)
{
	pipe_swapper sw;

	pipe(sw.pipe1);
	fcntl(sw.pipe1[0], F_SETFL, O_NONBLOCK);
	pipe(sw.pipe2);
	fcntl(sw.pipe2[0], F_SETFL, O_NONBLOCK);
	sw.d = d;
	sw.conn = d->watch(std::bind(&pipe_swapper::handle_pipe1, &sw, std::placeholders::_1), sw.pipe1[0], ioready_input);
	sw.finished = false;

	char c = 0;
	write(sw.pipe2[1], &c, 1);
	close(sw.pipe1[1]);

	while (!sw.finished) {
		std::chrono::steady_clock::duration t = std::chrono::milliseconds(0);
		d->dispatch(&t);
	}

	close(sw.pipe1[0]);
	close(sw.pipe2[0]);
	close(sw.pipe2[1]);
}

static int cancel_dispatching = 0;

static void *run_dispatcher(void *arg)
{
	ioready_dispatcher *d=(ioready_dispatcher *)arg;

	while(!cancel_dispatching) {
		d->dispatch(0);
	}

	return 0;
}

void test_dispatcher_threading(ioready_dispatcher * d)
{
	{
		cancel_dispatching = 0;

		eventflag & evflag = d->get_eventflag();

		pthread_t thread;

		pthread_create(&thread, 0, &run_dispatcher, d);

		usleep(10*1000);

		int pipefd[2];
		int oserror = ::pipe(pipefd);
		ASSERT(oserror == 0);

		int called = 0;

		tscb::ioready_connection link = d->watch(std::bind(function, &called, pipefd[0], std::placeholders::_1),
			pipefd[0], ioready_input);

		write(pipefd[1], &called, 1);

		usleep(10*1000);

		ASSERT(called == 1);

		cancel_dispatching = 1;

		evflag.set();

		pthread_join(thread, 0);
		link.disconnect();

		close(pipefd[0]);
		close(pipefd[1]);
	}
}
