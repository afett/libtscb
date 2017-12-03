#include <stdio.h>

#include <atomic>
#include <chrono>
#include <mutex>

#include <tscb/thread>
#include <tscb/deferred>
#include <tscb/signal>

std::chrono::steady_clock::time_point start, end;

volatile long var;
std::atomic_int atomic_var;
std::mutex mutex;
tscb::deferred_rwlock deferred_rwlock;

void increment(int times)
{
	while (times--) {
		var++;
	}
}

void atomic_increment(int times)
{
	while (times--) {
		atomic_var.fetch_add(1, std::memory_order_relaxed);
	}
}

void atomic_condincr(int times)
{
	atomic_var.store(1, std::memory_order_relaxed);
	while (times--) {
		int expected;
		expected = atomic_var.load(std::memory_order_relaxed);
		do {
			if (expected == 0) {
				break;
			}
		} while(!atomic_var.compare_exchange_strong(expected, expected + 1, std::memory_order_relaxed));
	}
}

void atomic_decandtest(int times)
{
	atomic_var.store(-1, std::memory_order_relaxed);
	while (times--) {
		if (atomic_var.fetch_sub(1, std::memory_order_relaxed) == 1) {
			break;
		}
	}
}

void pthread_mutex_lockunlock(int times)
{
	while (times--) {
		mutex.lock();
		mutex.unlock();
	}
}

static inline void synchronize(void)
{
	deferred_rwlock.sync_finished();
}

void deferred_rwlock_lockunlock(int times)
{
	while(times--) {
		while (deferred_rwlock.read_lock()) {
			synchronize();
		}
		if (deferred_rwlock.read_unlock()) {
			synchronize();
		}
	}
}

void deferred_rwlock_write_lockunlock(int times)
{
	while(times--) {
		bool sync = deferred_rwlock.write_lock_async();
		if (sync) {
			synchronize();
		} else {
			deferred_rwlock.write_unlock_async();
		}
	}
}

class CallbackReceiver {
public:
	void callback(int arg) {}
};

CallbackReceiver receiver;

int ncallbacks;

void callback_obj(int times)
{
	tscb::signal<void (int)> chain;

	int num=ncallbacks;

	while(num--)
		chain.connect(std::bind(&CallbackReceiver::callback, &receiver, std::placeholders::_1));

	while(times--)
		chain(0);
}

inline void callback_fn(int n)
{}

void callback_function(int times)
{
	tscb::signal<void (int)> chain;

	int num=ncallbacks;

	while(num--)
		chain.connect(&callback_fn);

	while(times--)
		chain(0);
}

struct simple_cb;

struct simple_cb {
	simple_cb *prev, *next;
	std::function<void (int)> fn;
};

void simple_cb(int times)
{
	struct simple_cb *cb=0;

	for(int n = 0; n<ncallbacks; n++) {
		struct simple_cb *tmp = new struct simple_cb;
		tmp->next = cb;
		cb = tmp;
		tmp->fn = callback_fn;
	}
	while(--times) {
		struct simple_cb *tmp=cb;
		while (tmp) {
			(tmp->fn)(0);
			tmp = tmp->next;
		}
	}
}

void run(void (*function)(int times), const char *description)
{
	long times = 128;

	while (1) {
		start = std::chrono::steady_clock::now();
		function(times);
		end = std::chrono::steady_clock::now();
		if (end-start > std::chrono::microseconds(500000)) {
			break;
		}
		times = times*2;
	}

	double usec = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
	double ops = times / usec * 1000000;
	double nsec = usec / times * 1000;

	printf("%30s: %12.1lf ops/sec %7.1lf nsecs/op\n", description, ops, nsec);
}

int main()
{
	run(increment, "Increment integer");
	run(atomic_increment, "Atomic increment integer");
	run(atomic_increment, "Atomic cond increment and test");
	run(atomic_decandtest, "Atomic decrement and test");
	run(pthread_mutex_lockunlock, "pthread_mutex lock+unlock");
	run(deferred_rwlock_lockunlock, "rwlock read_lock+read_unlock");
	run(deferred_rwlock_write_lockunlock, "rwlock write_lock+write_unlock");
	ncallbacks=0;
	printf("Empty chain\n");
	run(callback_obj, "Callback chain, member fn");
	run(callback_function, "Callback chain, static fn");
	run(simple_cb, "Simple callback, static fn");
	ncallbacks=1;
	printf("Single function\n");
	run(callback_obj, "Callback chain, member fn");
	run(callback_function, "Callback chain, static fn");
	run(simple_cb, "Simple callback, static fn");
	ncallbacks=2;
	printf("%d functions\n", ncallbacks);
	run(callback_obj, "Callback chain, member fn");
	run(callback_function, "Callback chain, static fn");
	run(simple_cb, "Simple callback, static fn");
	ncallbacks=3;
	printf("%d functions\n", ncallbacks);
	run(callback_obj, "Callback chain, member fn");
	run(callback_function, "Callback chain, static fn");
	run(simple_cb, "Simple callback, static fn");
	ncallbacks=4;
	printf("%d functions\n", ncallbacks);
	run(callback_obj, "Callback chain, member fn");
	run(callback_function, "Callback chain, static fn");
	run(simple_cb, "Simple callback, static fn");
	ncallbacks=5;
	printf("%d functions\n", ncallbacks);
	run(callback_obj, "Callback chain, member fn");
	run(callback_function, "Callback chain, static fn");
	run(simple_cb, "Simple callback, static fn");
	ncallbacks=10;
	printf("%d functions\n", ncallbacks);
	run(callback_obj, "Callback chain, member fn");
	run(callback_function, "Callback chain, static fn");
	run(simple_cb, "Simple callback, static fn");
	ncallbacks=15;
	printf("%d functions\n", ncallbacks);
	run(callback_obj, "Callback chain, member fn");
	run(callback_function, "Callback chain, static fn");
	run(simple_cb, "Simple callback, static fn");
	ncallbacks=20;
	printf("%d functions\n", ncallbacks);
	run(callback_obj, "Callback chain, member fn");
	run(callback_function, "Callback chain, static fn");
	run(simple_cb, "Simple callback, static fn");
	ncallbacks=30;
	printf("%d functions\n", ncallbacks);
	run(callback_obj, "Callback chain, member fn");
	run(callback_function, "Callback chain, static fn");
	run(simple_cb, "Simple callback, static fn");
	ncallbacks=40;
	printf("%d functions\n", ncallbacks);
	run(callback_obj, "Callback chain, member fn");
	run(callback_function, "Callback chain, static fn");
	run(simple_cb, "Simple callback, static fn");
}
