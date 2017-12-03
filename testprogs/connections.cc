/* -*- C++ -*-
 * (c) 2011 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <tscb/signal>
#include <tscb/ioready>
#include <tscb/timer>

static void io_handler(tscb::ioready_events events)
{
	(void) events;
}

void test_ioready_casts(void)
{
	tscb::ioready_service * ioready = tscb::ioready_dispatcher::create();

	/* no casts, assignment/construction to/from same type */
	{
		tscb::ioready_connection construct(ioready->watch(io_handler, 0, tscb::ioready_input));
		construct.disconnect();

		tscb::ioready_connection assign;
		assign = ioready->watch(io_handler, 0, tscb::ioready_input);
		assign.disconnect();
	}

	/* downcast to "connection" */
	{
		tscb::connection construct(ioready->watch(io_handler, 0, tscb::ioready_input));
		construct.disconnect();

		tscb::connection assign;
		assign = ioready->watch(io_handler, 0, tscb::ioready_input);
		assign.disconnect();
	}

	/* scoped_connection */
	{
		tscb::scoped_connection construct(ioready->watch(io_handler, 0, tscb::ioready_input));

		tscb::scoped_connection assign;
		assign = ioready->watch(io_handler, 0, tscb::ioready_input);
	}

	delete ioready;
}

static bool timer_handler(std::chrono::steady_clock::time_point & now)
{
	(void)now;
	return false;
}

void test_timer_casts(void)
{
	tscb::platform_eventflag ev;
	tscb::timerqueue_dispatcher timer(ev);

	/* no casts, assignment/construction to/from same type */
	{
		tscb::timer_connection construct(timer.timer(timer_handler, std::chrono::steady_clock::now()));
		construct.disconnect();

		tscb::timer_connection assign;
		assign = timer.timer(timer_handler, std::chrono::steady_clock::now());
		assign.disconnect();
	}

	/* downcast to "connection" */
	{
		tscb::connection construct(timer.timer(timer_handler, std::chrono::steady_clock::now()));
		construct.disconnect();

		tscb::connection assign;
		assign = timer.timer(timer_handler, std::chrono::steady_clock::now());
		assign.disconnect();
	}

	/* scoped_connection */
	{
		tscb::scoped_connection construct(timer.timer(timer_handler, std::chrono::steady_clock::now()));

		tscb::scoped_connection assign;
		assign = timer.timer(timer_handler, std::chrono::steady_clock::now());
	}
}

int main()
{
	test_ioready_casts();
	test_timer_casts();
}
