/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <boost/bind.hpp>

#define private public
#define protected public

#include <tscb/timer>
#include <tscb/eventflag>
#include "tests.h"

using namespace tscb;

class my_eventflag: public eventflag {
public:
	my_eventflag(void) : flagged(false) {}
	virtual ~my_eventflag(void) throw() {}
	
	virtual void set(void) throw() {flagged=true;}
	virtual void wait(void) throw() {while(!flagged);}
	virtual void clear(void) throw() {flagged=false;}
	
	volatile bool flagged;
};

my_eventflag flag;
int called=0;
int released=0;

boost::intrusive_ptr<generic_timer_callback_link<long long> > timer_link;
//timer_callback timer_link;

bool my_fn(long long &time)
{
	time++;
	called++;
	return true;
}

bool my_fn2(long long &time)
{
	time++;
	called++;
	ASSERT(released==0);
	timer_link->cancel();
	ASSERT(released==0);
	return true;
}

void my_release(void *)
{
	released++;
}

class X {
public:
	X(void) : refcount(1) {}
	bool fn(long long &t) {return false;}
	void pin(void) {refcount++;}
	void release(void) {refcount--;}
	
	int refcount;
};
static inline void intrusive_ptr_add_ref(X *x) {x->pin();}
static inline void intrusive_ptr_release(X *x) {x->release();}

class Y {
public:
	Y(void) : refcount(1) {}
	bool fn(long long &t) {timer_link->cancel(); return false;}
	void pin(void) {refcount++;}
	void release(void) {refcount--;}
	
	int refcount;
};
static inline void intrusive_ptr_add_ref(Y *y) {y->pin();}
static inline void intrusive_ptr_release(Y *y) {y->release();}

void timer_tests(void)
{
	generic_timerqueue_dispatcher<long long> tq(&flag);
	
	{
		long long zero(0);
		bool pending=tq.run_queue(zero);
		ASSERT(pending==false);
	}
	
	{
		long long zero(0);
		bool pending=tq.run_queue(zero);
		ASSERT(pending==false);
	}
	
	{
		called=0;
		long long time(0);
		
		timer_link=tq.timer(my_fn, time);
		ASSERT(timer_link->refcount==2);
		
		ASSERT(flag.flagged==true);
		flag.clear();
		
		bool pending=tq.run_queue(time);
		ASSERT(pending==true);
		ASSERT(called==1);
		ASSERT(time==1);
		ASSERT(flag.flagged==false);
		timer_link->cancel();
		ASSERT(flag.flagged==true);
		flag.clear();
		pending=tq.run_queue(time);
		ASSERT(pending==false);
		ASSERT(called==1);
		ASSERT(flag.flagged==false);
		
		ASSERT(timer_link->refcount==1);
	}
	
	{
		long long time(0);
		timer_link=tq.timer(my_fn2, time);
		
		called=0; released=0;
		tq.run_queue(time);
		ASSERT(called==1);
		ASSERT(timer_link->refcount==1);
	}
	
	{
		X x;
		long long time(0);
		timer_link=tq.timer(boost::bind(&X::fn, &x, _1), time);
		timer_link->cancel();
		ASSERT(timer_link->refcount==1);
	}
	{
		X x;
		long long time(0);
		ASSERT(x.refcount==1);
		timer_link=tq.timer(boost::bind(&X::fn, boost::intrusive_ptr<X>(&x), _1), time);
		ASSERT(x.refcount==2);
		timer_link->cancel();
		ASSERT(x.refcount==1);
		ASSERT(timer_link->refcount==1);
	}
	{
		Y y;
		long long time(0);
		timer_link=tq.timer(boost::bind(&Y::fn, boost::intrusive_ptr<Y>(&y), _1), time);
		tq.run_queue(time);
	}
}

int main()
{
	timer_tests();
	return 0;
}
