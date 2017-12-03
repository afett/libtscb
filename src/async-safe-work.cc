#include <signal.h>

#include <tscb/async-safe-work>

namespace tscb {

	async_safe_callback::~async_safe_callback(void) throw()
	{
	}

	void
	async_safe_callback::disconnect(void) throw()
	{
		registration_mutex_.lock();

		if (disconnected_) {
			registration_mutex_.unlock();
			return;
		}

		service_->list_mutex_.lock();

		disconnected_ = true;

		if (prev_) {
			prev_->next_ = next_;
		} else {
			service_->first_ = next_;
		}
		if (next_) {
			next_->prev_ = prev_;
		} else {
			service_->last_ = prev_;
		}

		if (activation_flag_.test_and_set(std::memory_order_release)) {
			/* if triggered already, it either has been or will subsequently
			be enqueued (this may race with the "trigger" method) */
			service_->async_cancel_count_.fetch_add(1, std::memory_order_relaxed);
			service_->list_mutex_.unlock();
			registration_mutex_.unlock();
		} else {
			service_->list_mutex_.unlock();
			registration_mutex_.unlock();
			release();
		}
	}

	bool
	async_safe_callback::connected(void) const throw()
	{
		return !disconnected_;
	}


	async_safe_connection::~async_safe_connection(void) throw()
	{
		if (callback_) {
			callback_->release();
		}
	}


	async_safe_work_service::~async_safe_work_service(void) throw()
	{
	}


	async_safe_work_dispatcher::async_safe_work_dispatcher(eventtrigger & trigger)
		: pending_(nullptr), async_cancel_count_(0), first_(nullptr), last_(nullptr), trigger_(trigger)
	{
	}

	/* temporarily and optimistically dequeue all items, but re-add them in case
	not all were processed */
	class async_pending_dequeue_helper {
	public:
		inline async_pending_dequeue_helper(std::atomic<async_safe_callback *> & pending_list, eventtrigger & trigger)
			: pending_list_(pending_list), trigger_(trigger)
		{
			head_ = pending_list_.exchange(nullptr, std::memory_order_acquire);
		}

		inline ~async_pending_dequeue_helper(void)
		{
			if (!head_) {
				return;
			}

			async_safe_callback * last = head_;
			while (last->pending_next_) {
				last = last->pending_next_;
			}

			async_safe_callback * tmp = pending_list_.load(std::memory_order_relaxed);
			do {
				last->pending_next_ = tmp;
			} while (!pending_list_.compare_exchange_weak(tmp, head_, std::memory_order_release, std::memory_order_relaxed));
			trigger_.set();
		}

		inline async_safe_callback *
		dequeue(void) throw()
		{
			async_safe_callback * current = head_;
			head_ = head_->pending_next_;
			return current;
		}

		inline operator bool(void) const throw()
		{
			return head_ != nullptr;
		}

		std::atomic<async_safe_callback *> & pending_list_;
		async_safe_callback * head_;
		eventtrigger & trigger_;
	};

	size_t
	async_safe_work_dispatcher::dispatch(void)
	{
		size_t handled = 0;
		/* fast-path check */
		if (pending_.load(std::memory_order_relaxed) == nullptr) {
			return 0;
		}

		async_pending_dequeue_helper deq(pending_, trigger_);

		while (deq) {
			async_safe_callback * proc = deq.dequeue();

			list_mutex_.lock();
			proc->activation_flag_.clear();

			if (!proc->disconnected_) {
				list_mutex_.unlock();
				/* if this throws, the current proc will be considered "processed",
				while the remaining are re-added to the queue */
				proc->function_();
				handled ++;
			} else {
				list_mutex_.unlock();
				proc->release();
				async_cancel_count_.fetch_sub(1, std::memory_order_relaxed);
			}
		}

		return handled;
	}

	async_safe_work_dispatcher::~async_safe_work_dispatcher(void) throw()
	{
		list_mutex_.lock();
		while (first_) {
			async_safe_callback * tmp = first_;
			tmp->pin();
			list_mutex_.unlock();
			tmp->disconnect();
			tmp->release();
			list_mutex_.lock();
		}
		list_mutex_.unlock();

		while (async_cancel_count_.load(std::memory_order_relaxed)) {
			async_safe_callback * proc = pending_.exchange(nullptr, std::memory_order_acquire);

			while (proc) {
				async_safe_callback * next = proc->pending_next_;
				proc->release();
				proc = next;
				async_cancel_count_.fetch_sub(1, std::memory_order_relaxed);
			}
		}
	}

	async_safe_connection
	async_safe_work_dispatcher::async_procedure(std::function<void(void)> function)
	{
		async_safe_callback * cb = new async_safe_callback(std::move(function), this);

		list_mutex_.lock();
		cb->prev_ = last_;
		cb->next_ = nullptr;
		if (last_) {
			last_->next_ = cb;
		} else {
			first_ = cb;
		}
		last_ = cb;
		list_mutex_.unlock();

		return cb;
	}

}
