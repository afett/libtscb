#include <sys/resource.h>
#include <sys/wait.h>

#include <tscb/childproc-monitor>

namespace tscb {

	childproc_monitor_service::~childproc_monitor_service(void) noexcept
	{
	}

	childproc_callback::~childproc_callback(void) noexcept
	{
	}

	void childproc_callback::disconnect(void) noexcept
	{
		registration_mutex_.lock();
		if (service_) {
			service_->remove(this);
		} else {
			registration_mutex_.unlock();
		}
	}

	bool childproc_callback::connected(void) const noexcept
	{
		std::unique_lock<std::mutex> guard(registration_mutex_);
		return service_ != nullptr;
	}

	childproc_monitor::childproc_monitor(bool reap_all_children)
		: reap_all_children_(reap_all_children),
		active_(nullptr), first_(nullptr), last_(nullptr), deferred_cancel_(nullptr)
	{
	}

	childproc_monitor::~childproc_monitor(void) noexcept
	{
		while(lock_.read_lock()) {
			synchronize();
		}

		for(;;) {
			childproc_callback * cb = active_.load(std::memory_order_relaxed);
			if (!cb) {
				break;
			}
			cb->disconnect();
		}
		if (lock_.read_unlock()) {
			synchronize();
		} else {
			lock_.write_lock_sync();
			synchronize();
		}
	}

	connection
	childproc_monitor::watch_childproc(std::function<void(int, const rusage &)> function, pid_t pid)
	{
		childproc_callback * cb = new childproc_callback(pid, std::move(function));

		cb->registration_mutex_.lock();
		bool sync = lock_.write_lock_async();

		cb->next_ = nullptr;
		cb->prev_ = last_;

		cb->active_next_.store(nullptr, std::memory_order_relaxed);

		childproc_callback * tmp = last_;
		for (;;) {
			if (!tmp) {
				if (!active_.load(std::memory_order_relaxed)) {
					active_.store(cb, std::memory_order_release);
				}
				break;
			}
			if (tmp->active_next_.load(std::memory_order_relaxed)) {
				break;
			}
			tmp->active_next_.store(cb, std::memory_order_release);
			tmp = tmp->prev_;
		}

		/* insert into list of all elements*/
		if (last_) {
			last_->next_ = cb;
		} else {
			first_ = cb;
		}
		last_ = cb;

		cb->service_ = this;

		cb->registration_mutex_.unlock();

		if (sync) {
			synchronize();
		} else {
			lock_.write_unlock_async();
		}

		return connection(cb, true);
	}

	void
	childproc_monitor::dispatch(void)
	{
		read_guard<childproc_monitor> guard(*this);

		childproc_callback * current = active_.load(std::memory_order_consume);

		while (current) {
			int status;
			struct rusage res;
			pid_t pid = wait4(current->pid_, &status, WNOHANG, &res);
			if (pid) {
				current->disconnect();
				current->function_(status, res);
			}

			current = current->active_next_.load(std::memory_order_consume);
		}
	}

	void childproc_monitor::remove(childproc_callback * cb) noexcept
	{
		bool sync = lock_.write_lock_async();
		if (cb->service_ == this) {
			/* remove element from active list; we have to make
			sure that all elements that pointed to "us" within
			the active chain now point to the following element,
			so this element is skipped from within the active chain */

			childproc_callback * tmp = cb->prev_;
			childproc_callback * next = cb->active_next_.load(std::memory_order_relaxed);
			for (;;) {
				if (!tmp) {
					if (active_.load(std::memory_order_relaxed) == cb) {
						active_.store(next, std::memory_order_release);
					}
					break;
				}
				if (tmp->active_next_.load(std::memory_order_relaxed) != cb) {
					break;
				}
				tmp->active_next_.store(next, std::memory_order_release);
				tmp = tmp->prev_;
			}

			/* put on list of elements marked for deferred cancellation */
			cb->deferred_cancel_next_ = deferred_cancel_;
			deferred_cancel_ = cb;

			cb->service_ = nullptr;
		}

		cb->registration_mutex_.unlock();

		if (sync) {
			synchronize();
		} else {
			lock_.write_unlock_async();
		}
	}

	void childproc_monitor::synchronize(void)
	{
		childproc_callback * do_cancel = deferred_cancel_;

		/* first, "repair" the list structure by "correcting" all prev
		pointers */
		while (do_cancel) {
			/* we can now safely remove the elements from the list */
			if (do_cancel->prev_) {
				do_cancel->prev_->next_ = do_cancel->next_;
			} else {
				first_ = do_cancel->next_;
			}
			if (do_cancel->next_) {
				do_cancel->next_->prev_ = do_cancel->prev_;
			} else {
				last_ = do_cancel->prev_;
			}

			do_cancel = do_cancel->deferred_cancel_next_;
		}

		/* now swap pointers while still under the lock; this is
		necessary to make sure that the destructor for each
		callback link object is called exactly once */
		do_cancel = deferred_cancel_;
		deferred_cancel_ = nullptr;
		lock_.sync_finished();

		/* now we can release the callbacks, as we are sure that no one
		can "see" them anymore; the lock is dropped so side-effects
		of finalizing the links cannot cause deadlocks */
		while (do_cancel) {
			childproc_callback * tmp = do_cancel->deferred_cancel_next_;
			do_cancel->cancelled();
			do_cancel->release();
			do_cancel = tmp;
		}
	}

}
