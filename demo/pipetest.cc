/* -*- C++ -*-
 * (c) 2004 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

/*
  This is a small test program that exercises the event_dispatcher class
  by passing around a token in a circle of pipes.
 */

#include <thread>
#include <vector>

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <tscb/ioready>
#include <tscb/timer>

const int second_threshold=1;
const int max_threads=2;
const int num_reserved_fds=max_threads*4;

std::vector<int> read_fds, write_fds;

void create_pipes(void)
{
	int filedes[2], error;
	std::vector<int> reserved_fds;

	for(int n = 0; n < num_reserved_fds; ++n) {
		reserved_fds.push_back(open("/dev/null", O_RDONLY));
	}

	int n = 0;
	for(;;) {
		error=pipe(filedes);
		if (error) {
			if ((errno==EMFILE) || (errno==ENFILE)) break;
			perror("pipe");
			abort();
		}

		int flags=fcntl(filedes[0], F_GETFL);
		flags |= O_NONBLOCK;
		fcntl(filedes[0], F_SETFL, flags);

		read_fds.push_back(filedes[0]);
		write_fds.push_back(filedes[1]);
		n++;
	}

	fprintf(stderr, "created %d pipe pairs\n", n);

	for(n=0; n<num_reserved_fds; n++) close(reserved_fds[n]);
}

void cleanup_pipes(void)
{
	char buffer[16];
	size_t n;
	for(n=0; n<read_fds.size(); n++)
		while(read(read_fds[n], buffer, 16)!=-1);
}

class perfcounter {
public:
	perfcounter(void);

	void count(void);

	int counter, iterations;
	std::chrono::steady_clock::time_point begin;
	double loopspersecond;

	volatile bool finished;
};

perfcounter::perfcounter(void)
{
	counter=0;
	iterations=256;
	begin = std::chrono::steady_clock::now();
	loopspersecond=0;
	finished=false;
}

void perfcounter::count(void)
{
	if (!finished) {
		counter++;
		if (counter>=iterations) {
			std::chrono::steady_clock::time_point end;
			end = std::chrono::steady_clock::now();
			long long d = std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count();
			if (d/1000000 >= second_threshold) {
				loopspersecond = counter*1000000.0/d;
				finished = true;
				return;
			}
			iterations = iterations*2;
			counter = 0;
			begin = end;
		}
	}
}


class receiver {
public:
	receiver(tscb::ioready_service *_io, int _from, int _to, perfcounter &counter);
	~receiver(void);
	void pass_token(tscb::ioready_events events);
	void release(void);

	tscb::ioready_connection link;
private:
	int from_, to_;
	perfcounter &counter_;
};

std::vector<receiver *> receivers;

receiver::receiver(tscb::ioready_service *io, int from, int to, perfcounter &counter)
	: from_(from), to_(to), counter_(counter)
{
	link = io->watch(std::bind(&receiver::pass_token, this, std::placeholders::_1),
		from_, tscb::ioready_input);
}

void receiver::release(void)
{
}

receiver::~receiver(void)
{
	link.disconnect();
}

void receiver::pass_token(tscb::ioready_events events)
{
	char buffer[1];
	read(from_, buffer, 1);
	write(to_, buffer, 1);
	counter_.count();
}

void cleanup_receivers(void)
{
	for (size_t n = 0; n<receivers.size(); n++) {
		delete receivers[n];
	}
	receivers.clear();
	cleanup_pipes();
}

tscb::ioready_dispatcher *prepare_ring(int start, int nelements,
	perfcounter &counter, int ninject=1)
{
	tscb::ioready_dispatcher *d=tscb::create_ioready_dispatcher();

	for(int n = 0; n < nelements; ++n)
		receivers.push_back(
			new receiver(d, read_fds[start + n],
				write_fds[start + (n + 1) % nelements],
				counter)
		);

	/* FIXME: inject n tokens */
	char buffer = 0;
	write(write_fds[start], &buffer, 1);

	return d;
}

class dispatcher_worker {
public:
	dispatcher_worker(std::unique_ptr<tscb::ioready_dispatcher> dispatcher)
		: dispatcher_(std::move(dispatcher))
		, cancelled_(false), flag_(dispatcher_->get_eventflag())
	{
	}

	void thread_func(void) noexcept {
		while (!cancelled_.load()) {
			dispatcher_->dispatch(nullptr);
		}
	}
	void cancel(void)
	{
		cancelled_.store(true);
		flag_.set();
	}

	std::unique_ptr<tscb::ioready_dispatcher> dispatcher_;
	std::atomic<bool> cancelled_;
	tscb::eventflag & flag_;
};

void run_independent(size_t nthreads, int nelements)
{
	std::vector<perfcounter> counter;
	std::vector<std::unique_ptr<dispatcher_worker>> dispatchers;
	std::vector<std::thread> threads;

	counter.resize(nthreads);

	for (size_t n = 0; n < nthreads; ++n) {
		dispatchers.emplace_back(new dispatcher_worker(
			std::unique_ptr<tscb::ioready_dispatcher>(prepare_ring(nelements * n, nelements, counter[n]))));
	}
	for (size_t n = 0; n < nthreads; ++n) {
		threads.emplace_back(&dispatcher_worker::thread_func, dispatchers[n].get());
	}

	for (;;) {
		bool all_finished = true;
		for (size_t n = 0; n < nthreads; ++n) {
			all_finished = all_finished && counter[n].finished;
		}
		if (all_finished) {
			break;
		} else {
			sleep(1);
		}
	}


	for (size_t n = 0; n < nthreads; ++n) {
		dispatchers[n]->cancel();
		threads[n].join();
	}

	double sum = 0.0;
	for (size_t n = 0; n < nthreads; ++n) {
		sum += counter[n].loopspersecond;
	}
	printf("%g", sum);
	for (size_t n = 0; n<nthreads; n++) {
		printf(" %g", counter[n].loopspersecond);
	}
	printf("\n");

	cleanup_receivers();
}

void run_independent(void)
{
	int nthreads=1;
	while(nthreads<=max_threads) {
		printf("%d thread(s)\n", nthreads);
		int nsockets=4;
		while(nsockets*nthreads<(int)read_fds.size()) {
			printf("%d ", nsockets);
			run_independent(nthreads, nsockets);
			nsockets=nsockets*2;
		}
		nthreads++;
	}
}

int main()
{
	create_pipes();

	run_independent();
}
