/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#define private public

#define _LIBTSCB_CALLBACK_UNITTESTS 1
#include <tscb/signal>
#include "tests.h"

int result = 0;
int called = 0;

class Receiver {
public:
	Receiver(void) : refcount(1) {}
	void cbrecv1(int arg) {result = arg;}
	void cbrecv2(int arg) {
		result = arg;
		link1.disconnect();
		ASSERT(refcount == 2);
		ASSERT(!link1.connected());
		ASSERT(refcount == 2);
	}
	void cbrecv3(int arg) {
		++called;
		result = arg;
		link1.disconnect();
		link2.disconnect();
		fflush(stdout);
	}

	inline void pin(void) {++refcount;}
	inline void release(void) {--refcount;}
	int refcount;

	tscb::connection link1, link2;
};

static inline void intrusive_ptr_add_ref(Receiver *t) throw()
{
	t->pin();
}

static inline void intrusive_ptr_release(Receiver *t) throw()
{
	t->release();
}

static void fn(int arg)
{
	called += arg;
}

void callback_tests(void)
{
	tscb::signal<void (int)> chain;
	{
		/* verify that callbacks are invoked correctly at all, that
		callbacks are cancellable and that references to target objects
		are handled correctly */
		Receiver r;

		r.link1 = chain.connect(std::bind(&Receiver::cbrecv1, tscb::intrusive_ptr<Receiver>(&r), std::placeholders::_1));
		ASSERT(r.refcount == 2);
		ASSERT(r.link1.callback_->refcount_ == 2);

		chain(1);
		ASSERT(result == 1);

		r.link1.disconnect();
		ASSERT(r.refcount == 1);

		chain(2);
		ASSERT(result == 1);
	}
	{
		/* verify that callbacks can cancel themselves and that the reference
		count to the target object is dropped after the callback has
		completed */
		Receiver r;
		r.link1 = chain.connect(std::bind(&Receiver::cbrecv2, tscb::intrusive_ptr<Receiver>(&r), std::placeholders::_1));

		chain(3);
		ASSERT(result == 3);
		chain(4);
		ASSERT(result == 3);

		ASSERT(r.refcount == 1);
	}
	{
		/* veriy that callbacks can cancel each other (out of two
		callbacks that mutually cancel each other, exactly one must be
		executed) and that reference counting still works as expected */

		Receiver r;
		r.link1 = chain.connect(std::bind(&Receiver::cbrecv3, tscb::intrusive_ptr<Receiver>(&r), std::placeholders::_1));
		r.link2 = chain.connect(std::bind(&Receiver::cbrecv3, tscb::intrusive_ptr<Receiver>(&r), std::placeholders::_1));

		chain(5);

		ASSERT(result == 5);
		ASSERT(called == 1);
		ASSERT(r.refcount == 1);
	}
	{
		/* verify that, upon destroying the a callback chain, all corresponding
		callback links are removed from the chain and all references
		to target objects are dropped as well */
		Receiver r;
		{
			tscb::signal<void (int)> chain;
			r.link1 = chain.connect(std::bind(&Receiver::cbrecv1, tscb::intrusive_ptr<Receiver>(&r), std::placeholders::_1));
			ASSERT(r.link1.callback_->refcount_ == 2);
			ASSERT(r.refcount == 2);
		}
		ASSERT(r.link1.callback_->refcount_ == 1);
		ASSERT(r.refcount == 1);
		r.link1.disconnect();
	}
	{
		called = 0;
		result = 0;
		tscb::connection l = chain.connect(std::bind(fn, std::placeholders::_1));

		chain(1);
		ASSERT(called == 1);
		ASSERT(result == 0);

		l.disconnect();
		chain(1);
		ASSERT(called == 1);
	}
	/* check cancellation of first element in list */
	{
		called = 0;
		tscb::connection link1, link2;
		link1 = chain.connect(std::bind(fn, std::placeholders::_1));
		link2 = chain.connect(std::bind(fn, std::placeholders::_1));

		chain(1);
		ASSERT(called == 2);

		link1.disconnect();
		called = 0;
		chain(1);
		ASSERT(called == 1);

		link2.disconnect();
	}
	/* check cancellation of second element in list */
	{
		called = 0;
		tscb::connection link1, link2;
		link1 = chain.connect(std::bind(fn, std::placeholders::_1));
		link2 = chain.connect(std::bind(fn, std::placeholders::_1));

		chain(1);
		ASSERT(called == 2);

		link2.disconnect();
		called = 0;
		chain(1);
		ASSERT(called == 1);

		link1.disconnect();
	}
}

int main(void)
{
	callback_tests();
	#if 0
	callback_reftests();
	#endif
	return 0;
}
