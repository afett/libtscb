/* -*- C++ -*-
 * (c) 2011 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#define private public
#define protected public

#include <pthread.h>
#include <tscb/async-safe-work>

tscb::atomic<int> called_count(0);

static void work_handler(void)
{
	called_count.fetch_add(1, tscb::memory_order_relaxed);
}

static void throwing_work_handler(void)
{
	called_count.fetch_add(1, tscb::memory_order_relaxed);
	throw std::runtime_error("foo");
}

void * thread_func1(void * arg)
{
	tscb::eventtrigger * trigger = (tscb::eventtrigger *)arg;
	trigger->set();
	return 0;
}

void test_basic_operation(void)
{
	tscb::pipe_eventflag event;
	tscb::async_safe_work_dispatcher async(event);
	
	called_count.store(0);
	
	tscb::async_safe_connection connection = async.async_procedure(work_handler);
	assert(connection.get()->refcount == 2);
	
	pthread_t tid;
	pthread_create(&tid, 0, thread_func1, dynamic_cast<tscb::eventtrigger *>(&connection));
	
	while (called_count.load(tscb::memory_order_relaxed) == 0) {
		event.wait();
		event.clear();
		async.dispatch();
	}
	pthread_join(tid, 0);
}

void test_disconnect(void)
{
	tscb::pipe_eventflag event;
	tscb::async_safe_work_dispatcher async(event);
	tscb::async_safe_connection connection = async.async_procedure(work_handler);
	
	tscb::async_safe_callback * cb = connection.get();
	cb->pin();
	assert(async.first == cb);
	assert(async.last == cb);
	/* one from dispatcher, one from connection, and one just acquired */
	assert(cb->refcount == 3);
	
	connection.disconnect();
	assert(async.first == 0);
	assert(async.last == 0);
	assert(async.pending == 0);
	/* only our "private" ref remains now */
	assert(cb->refcount == 1);
	
	cb->release();
}

void test_disconnect_triggered(void)
{
	tscb::pipe_eventflag event;
	tscb::async_safe_work_dispatcher async(event);
	tscb::async_safe_connection connection = async.async_procedure(work_handler);
	
	called_count.store(0);
	
	tscb::async_safe_callback * cb = connection.get();
	cb->pin();
	assert(async.first == cb);
	assert(async.last == cb);
	/* one from dispatcher, one from connection, and one just acquired */
	assert(cb->refcount == 3);
	
	connection.set();
	connection.disconnect();
	assert(async.first == 0);
	assert(async.last == 0);
	assert(async.pending == cb);
	/* ref from connection object is dropped now */
	assert(cb->refcount == 2);
	
	async.dispatch();
	assert(called_count == 0);
	/* only our "private" ref remains now */
	assert(cb->refcount == 1);
	
	cb->release();
}

void test_dispatch_throw(void)
{
	tscb::pipe_eventflag event;
	tscb::async_safe_work_dispatcher async(event);
	
	tscb::async_safe_connection c1 = async.async_procedure(throwing_work_handler);
	tscb::async_safe_connection c2 = async.async_procedure(throwing_work_handler);
	
	called_count.store(0);
	
	c1.set();
	c2.set();
	assert(event.flagged != 0);
	
	/* dispatch pending events, will throw on first */
	event.clear();
	try {
		async.dispatch();
		assert(false);
	}
	catch(std::runtime_error) {
	}
	
	/* first must have been processed, other must remain pending;
	eventflag must have been reasserted */
	assert(called_count == 1);
	assert(async.pending != 0);
	assert(event.flagged != 0);
	
	/* dispatch pending events, will throw on second */
	event.clear();
	try {
		async.dispatch();
		assert(false);
	}
	catch(std::runtime_error) {
	}
	
	/* second must have been processed; nothing pending anymore */
	assert(called_count == 2);
	assert(event.flagged == 0);
}

/* simulate a disconnect racing with trigger */
void test_disconnect_race(void)
{
	tscb::pipe_eventflag event;
	tscb::async_safe_work_dispatcher async(event);
	
	called_count.store(0);
	
	tscb::async_safe_connection c1 = async.async_procedure(throwing_work_handler);
	tscb::async_safe_connection c2 = c1;
	
	/* thread1: top half of trigger */
	c1.get()->activation_flag.test_and_set();
	
	/* thread2: disconnect */
	c2.disconnect();
	/* will be enqueued to pending list by trigger_bottom */
	assert(async.pending == 0);
	
	/* thread1: bottom half of trigger */
	c1.get()->trigger_bottom();
	
	assert(event.flagged != 0);
	assert(async.pending != 0);
	
	/* dispatch pending events, should not call function but complete
	the racy disconnect operation instead */
	async.dispatch();
	
	assert(called_count == 0);
}

int main()
{
	test_basic_operation();
	test_disconnect();
	test_disconnect_triggered();
	test_dispatch_throw();
	test_disconnect_race();
}
