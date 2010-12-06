/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <string.h>
#include <tscb/config>
#include <tscb/ioready>

namespace tscb {
	
	void ioready_callback::disconnect(void) throw()
	{
		cancellation_mutex.lock();
		ioready_service *tmp=service.load(memory_order_relaxed);
		if (tmp) tmp->unregister_ioready_callback(this);
		else cancellation_mutex.unlock();
	}
	
	bool ioready_callback::connected(void) const throw()
	{
		return service.load(memory_order_relaxed)!=0;
	}
	
	void ioready_callback::modify(ioready_events evmask) throw()
	{
		if (evmask != ioready_none) evmask = evmask | ioready_error | ioready_hangup;
		cancellation_mutex.lock();
		ioready_service *tmp=service.load(memory_order_relaxed);
		if (tmp) tmp->modify_ioready_callback(this, evmask);
		cancellation_mutex.unlock();
	}
	
	ioready_callback::~ioready_callback(void) throw()
	{
	}
	
	ioready_service::~ioready_service(void) throw()
	{
	}
	
	ioready_dispatcher::~ioready_dispatcher(void) throw()
	{
	}
	
	static ioready_dispatcher *
	create_ioready_dispatcher_probe(void) throw(std::bad_alloc, std::runtime_error);
	
	#ifdef HAVE_KQUEUE
	ioready_dispatcher *
	create_ioready_dispatcher_kqueue(void) throw(std::bad_alloc, std::runtime_error);
	#endif
	#ifdef HAVE_EPOLL
	ioready_dispatcher *
	create_ioready_dispatcher_epoll(void) throw(std::bad_alloc, std::runtime_error);
	#endif
	#ifdef HAVE_POLL
	ioready_dispatcher *
	create_ioready_dispatcher_poll(void) throw(std::bad_alloc, std::runtime_error);
	#endif
	#ifdef HAVE_SELECT
	ioready_dispatcher *
	create_ioready_dispatcher_select(void) throw(std::bad_alloc, std::runtime_error);
	#endif
	
	typedef ioready_dispatcher *(*ioready_dispatcher_creator_func_t)(void);
	
	static ioready_dispatcher_creator_func_t ioready_dispatcher_creator_func
		=&create_ioready_dispatcher_probe;
		
	static ioready_dispatcher_creator_func_t probe_functions[]={
	#ifdef HAVE_KQUEUE
		&create_ioready_dispatcher_kqueue,
	#endif
	#ifdef HAVE_EPOLL
		&create_ioready_dispatcher_epoll,
	#endif
	#ifdef HAVE_POLL
		&create_ioready_dispatcher_poll,
	#endif
	#ifdef HAVE_SELECT
		&create_ioready_dispatcher_select,
	#endif
		0
	};
	
	ioready_dispatcher *
	create_ioready_dispatcher_probe(void) throw(std::bad_alloc, std::runtime_error)
	{
		size_t n=0;
		while(true) {
			ioready_dispatcher_creator_func_t func=probe_functions[n];
			ioready_dispatcher *dispatcher;
			try {
				dispatcher=(*func)();
			}
			catch(std::runtime_error) {
				n++;
				continue;
			}
			ioready_dispatcher_creator_func=func;
			return dispatcher;
		}
	}
	
	ioready_dispatcher *
	create_ioready_dispatcher(void) /* throw(std::bad_alloc, std::runtime_error) */
	{
		return (*ioready_dispatcher_creator_func)();
	}
	
}
