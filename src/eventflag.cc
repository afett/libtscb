/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1.
 * Refer to the file_event "COPYING" for details.
 */

#include <tscb/eventflag>

namespace tscb {
	
	eventflag::~eventflag(void) throw()
	{
	}
	
	
	pipe_eventflag::pipe_eventflag(void) throw(std::runtime_error)
		: flagged(0)
	{
		int filedes[2];
		int error=pipe(filedes);
		
		if (error) throw std::runtime_error("Unable to create control pipe");
		
		readfd=filedes[0];
		writefd=filedes[1];
	}
	
	pipe_eventflag::~pipe_eventflag(void) throw()
	{
		close(readfd);
		close(writefd);
	}
	
	void pipe_eventflag::set(void) throw()
	{
		/* setting a flag always occurs after a writing state to memory
		objects to indicate that a certain condition now holds; ensure
		that setting the condition does not overtake setting the
		event flag */
		memory_barrier();
		
		/* fast path (to avoid atomic op) if flag is already set */
		if (flagged!=0) return;
		
		/* atomic exchange to ensure only one setter can "see" the
		0->1 transition; otherwise we could have spurious wakeups */
		int oldval=flagged.cmpxchg(0, 1);
		if (oldval!=0) return;
		
		/* we are now certain that we have switched the flag from 0 to 1;
		if no one has been waiting before we switched the flag,
		there is no one to wakeup */
		
		memory_barrier();
		if (__builtin_expect(waiting==0, true)) return;
		
		/* at least one thread has been marked "waiting"; we have to
		post a wakeup; the last thread that was waiting will clear
		the control pipe */
		
		oldval=flagged.cmpxchg(1, 2);
		if (oldval!=1) return;
		
		char c;
		write(writefd, &c, 1);
	}
	
	void pipe_eventflag::start_waiting(void) throw()
	{
		/* slow path */
		waiting++;
		memory_barrier();
	}
	
	void pipe_eventflag::wait(void) throw()
	{
		/* fast path to avoid atomic op if flag is already set */
		if (flagged!=0) return;
		
		/* slow path */
		start_waiting();
		
		if (flagged==0) {
			/* poll file descriptor */
		}
		
		stop_waiting();
		
	}
	
	void pipe_eventflag::stop_waiting(void) throw()
	{
		--waiting;
	}
	
	void pipe_eventflag::clear(void) throw()
	{
		int oldval, tmp;
		{
			oldval=flagged;
			/* fast path (to avoid atomic op) if flag is already cleared */
			if (oldval==0) return;
			tmp=flagged.cmpxchg(oldval, 0);
		} while(tmp!=oldval);
		
		if (__builtin_expect(oldval==1, true)) {
			/* after clearing a flag, the application will test a
			condition in a data structure; make sure test of the
			condition and clearing of the flag are not reordered */
			memory_barrier();
			return;
		}
		
		/* a wakeup has been sent the last time the flag was raised;
		clear the control pipe */
		char c;
		read(readfd, &c, 1);
		/* we assume that a system call is an implicit memory barrier */
	}
	
	#if 0
	
	signal_eventflag::signal_eventflag(pthread_t _thread, int _signo) throw()
		: thread(_thread), signo(_signo)
	{
	}
	
	signal_eventflag::~signal_eventflag(void) throw()
	{
	}
	
	void signal_eventflag::set(void) throw()
	{
		if (!flagged) {
			/* ordering is important here; we can allow spurious
			wakeups (through spurious signal to the thread), but not missed
			wakeups */
			flagged=true;
			
			/* PREMISE: system calls are implicit memory barriers */
			/* I think I can safely assume this premise to hold; if
			it does not, I have to add a memory_barrier here */
			
			pthread_kill(thread, signo);
		}
	}
	
	void signal_eventflag::wait(void) throw()
	{
		sigset_t set;
		
		sigemptyset(&set);
		sigaddset(&set, signo);
		
		int s;
		sigwait(&set, &s);
	}
	
	void signal_eventflag::clear(void) throw()
	{
		if (flagged) { sigtimedwait
		sigset_t set;
		
		sigemptyset(&set);
		sigaddset(&set, signo);
		
		int s;
		if (sigpending
	}
	
	#endif
	
}
