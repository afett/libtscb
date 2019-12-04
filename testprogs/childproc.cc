/* -*- C++ -*-
 * (c) 2011 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <assert.h>
#include <pthread.h>
#include <unistd.h>

#include <tscb/childproc-monitor>

std::atomic<int> called_count(0);

static void proc_handler(void)
{
	called_count.fetch_add(1, std::memory_order_relaxed);
}

static void throwing_proc_handler(void)
{
	called_count.fetch_add(1, std::memory_order_relaxed);
	throw std::runtime_error("foo");
}

static pid_t launch_temp_process(void)
{
	pid_t pid = fork();
	if (pid == 0) {
		_exit(0);
	}
	return pid;
}

static pid_t launch_pers_process(void)
{
	pid_t pid = fork();
	if (pid == 0) {
		for (;;) {
			sleep(60);
		}
	}
	return pid;
}

class sigchld_guard {
public:
	sigchld_guard(void)
	{
		sigset_t sigset;
		sigemptyset(&sigset);
		sigaddset(&sigset, SIGCHLD);
		sigprocmask(SIG_BLOCK, &sigset, NULL);
	}
	~sigchld_guard(void)
	{
		sigset_t sigset;
		sigemptyset(&sigset);
		sigaddset(&sigset, SIGCHLD);
		sigprocmask(SIG_BLOCK, &sigset, NULL);
	}
	void wait(void)
	{
		sigset_t sigset;
		sigemptyset(&sigset);
		sigaddset(&sigset, SIGCHLD);
		sigwaitinfo(&sigset, NULL);
	}
};

void test_basic_operation(void)
{
	sigchld_guard guard;
	tscb::childproc_monitor m;

	called_count.store(0);

	pid_t pid = launch_temp_process();

	tscb::connection c = m.watch_childproc(std::bind(proc_handler), pid);

	assert(called_count == 0);

	/* wait until child process has terminated */
	guard.wait();

	m.dispatch();
	assert(called_count == 1);
}

void test_cancel(void)
{
	sigchld_guard guard;
	tscb::childproc_monitor m;

	called_count.store(0);

	pid_t pid = launch_pers_process();

	tscb::connection c = m.watch_childproc(std::bind(proc_handler), pid);

	m.dispatch();

	/* not exited yet */
	assert(called_count == 0);

	c.disconnect();

	kill(pid, SIGTERM);
	/* wait until child process has terminated */
	guard.wait();

	/* callback cancelled, should not reap */
	m.dispatch();
	assert(called_count == 0);

	int status;
	waitpid(pid, &status, 0);
}

void test_ignore_unknown(void)
{
	sigchld_guard guard;
	tscb::childproc_monitor m;

	called_count.store(0);

	pid_t pid = launch_temp_process();

	/* wait until child process has terminated */
	guard.wait();

	/* but dispatcher does not yet know about it, so won't reap */
	m.dispatch();
	assert(called_count == 0);

	tscb::connection c = m.watch_childproc(std::bind(proc_handler), pid);

	/* now that dispatcher knows, it will reap */
	m.dispatch();
	assert(called_count == 1);
}

void test_throwing_handler(void)
{
	sigchld_guard guard;
	tscb::childproc_monitor m;

	called_count.store(0);

	pid_t pid1 = launch_temp_process();
	/* wait until child process has terminated */
	guard.wait();

	pid_t pid2 = launch_temp_process();
	/* wait until child process has terminated */
	guard.wait();

	tscb::connection c1 = m.watch_childproc(std::bind(throwing_proc_handler), pid1);
	tscb::connection c2 = m.watch_childproc(std::bind(throwing_proc_handler), pid2);

	try {
		m.dispatch();
		assert(false);
	}
	catch(std::runtime_error const&) {
	}

	assert(called_count == 1);

	try {
		m.dispatch();
		assert(false);
	}
	catch(std::runtime_error const&) {
	}

	assert(called_count == 2);
}

int main()
{
	test_basic_operation();
	test_cancel();
	test_ignore_unknown();
	test_throwing_handler();
}
