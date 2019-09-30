/* -*- C++ -*-
 * (c) 2010 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1.
 * Refer to the file_event "COPYING" for details.
 */

#ifndef TSCB_REACTOR_H
#define TSCB_REACTOR_H

#include <tscb/timer.h>
#include <tscb/ioready.h>
#include <tscb/workqueue.h>
#include <tscb/async-safe-work.h>

/**
	\page reactor_descr Reactor interface

	The \ref tscb::posix_reactor_service interface combines the
	\ref tscb::timer_service, \ref tscb::ioready_service and
	\ref tscb::workqueue_service interfaces. It is suitable
	for being used as the basis for event-driven applications
	that perform actions in reaction to external events.

*/

namespace tscb {

/**
	\brief Posix reactor service

	Combines the interfaces \ref tscb::workqueue_service "workqueue_service",
	\ref tscb::timer_service "timer_service" and \ref tscb::ioready_service "ioready_service"
*/
class posix_reactor_service : public workqueue_service, public timer_service, public ioready_service, public async_safe_work_service {
public:
	virtual ~posix_reactor_service() noexcept;

	virtual eventtrigger & get_eventtrigger() = 0;
};

};

#endif
