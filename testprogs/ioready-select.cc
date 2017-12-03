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

#include "ioready-dispatcher"
#include <tscb/ioready-select>

using namespace tscb;

int main()
{
	ioready_dispatcher_select *dispatcher;

	dispatcher=new ioready_dispatcher_select();

	test_dispatcher(dispatcher);
	test_dispatcher_threading(dispatcher);
	test_dispatcher_sync_disconnect(dispatcher);

	delete dispatcher;
}
