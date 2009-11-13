/* -*- C++ -*-
 * (c) 2009 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <boost/bind.hpp>

#define private public
#define protected public

#include "tests.h"

#include <tscb/ioready>
#include <pthread.h>

void test_pipe_eventflag(void)
{
	tscb::pipe_eventflag e;
	
	ASSERT(e.flagged==0);
	e.set();
	ASSERT(e.flagged==1);
	e.clear();
	ASSERT(e.flagged==0);
	
	e.start_waiting();
	ASSERT(e.waiting==1);
	e.stop_waiting();
	ASSERT(e.waiting==0);
	
	e.set();
	ASSERT(e.flagged==1);
	e.clear();
}

int main()
{
	test_pipe_eventflag();
}
