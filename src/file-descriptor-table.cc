/* -*- C++ -*-
 * (c) 2010 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <string.h>
#include <tscb/config>
#include <tscb/file-descriptor-table>

namespace tscb {

	/* must be called under read lock */
	void file_descriptor_table::cancel_all(void) noexcept
	{
		volatile_table * tab = table_.load(std::memory_order_consume);
		for (size_t n = 0; n < tab->capacity_; n++) {
			file_descriptor_chain * entry = tab->entries_[n].load(std::memory_order_consume);
			if (!entry) {
				continue;
			}
			for (;;) {
				ioready_callback * cb = entry->active_.load(std::memory_order_consume);
				if (!cb) {
					break;
				}
				cb->disconnect();
			}
		}
	}

	/* must be called under write lock */
	void file_descriptor_table::insert(ioready_callback * cb, ioready_events & old_mask, ioready_events & new_mask) /*throw(std::bad_alloc)*/
	{
		volatile_table * tab = get_extend_table(cb->fd_);
		file_descriptor_chain * entry = tab->entries_[cb->fd_].load(std::memory_order_relaxed);
		if (!entry) {
			entry = new file_descriptor_chain;
			tab->entries_[cb->fd_].store(entry, std::memory_order_relaxed);
		}

		/* compute old event mask */
		old_mask = ioready_none;
		ioready_callback * tmp = entry->active_.load(std::memory_order_relaxed);
		while (tmp) {
			old_mask |= tmp->event_mask();
			tmp = tmp->active_next_.load(std::memory_order_relaxed);
		}
		new_mask = old_mask | cb->event_mask();

		/* prepare element */
		cb->prev_ = entry->last_;
		cb->next_ = nullptr;
		cb->active_next_.store(nullptr, std::memory_order_relaxed);

		/* we are now going to "publish" this element; since we may be
		inserting multiple references, just issue a thread fence once
		and use relaxed memory order */
		atomic_thread_fence(std::memory_order_release);

		/* add element to active list; find all elements that have been removed
		from the full list and thus terminate the active list; point them to
		the newly-added element */

		tmp = entry->last_;

		for (;;) {
			if (!tmp) {
				if (entry->active_.load(std::memory_order_relaxed) == 0) {
					entry->active_.store(cb, std::memory_order_relaxed);
				}
				break;
			}
			if (tmp->active_next_.load(std::memory_order_relaxed)) {
				break;
			}
			tmp->active_next_.store(cb, std::memory_order_relaxed);
			tmp = tmp->prev_;
		}

		if (entry->last_) {
			entry->last_->next_ = cb;
		} else {
			entry->first_ = cb;
		}

		entry->last_ = cb;
	}

	/* must be called under write lock */
	void file_descriptor_table::remove(ioready_callback * cb, ioready_events & old_mask, ioready_events & new_mask) noexcept
	{
		volatile_table * tab = table_.load(std::memory_order_relaxed);
		file_descriptor_chain * entry = tab->entries_[cb->fd_].load(std::memory_order_relaxed);

		/* remove protocol: remove element from active list; we have to make
		sure that all elements that pointed to "us" within
		the active chain now point to the following element,
		so this element is skipped from within the active chain */
		ioready_callback * tmp = cb->prev_;
		ioready_callback * next = cb->active_next_.load(std::memory_order_relaxed);
		for (;;) {
			if (!tmp) {
				if (entry->active_.load(std::memory_order_relaxed) == cb) {
					entry->active_.store(next, std::memory_order_release);
				}
				break;
			}
			if (tmp->active_next_.load(std::memory_order_relaxed) != cb) {
				break;
			}
			tmp->active_next_.store(next, std::memory_order_release);
			tmp = tmp->prev_;
		}

		/* compute old event mask */
		new_mask = ioready_none;
		tmp = entry->active_.load(std::memory_order_relaxed);
		while (tmp) {
			new_mask |= tmp->event_mask();
			tmp = tmp->active_next_.load(std::memory_order_relaxed);
		}
		old_mask = new_mask | cb->event_mask();

		/* If this is the last callback registered for this descriptor,
		then user might be tempted to synchronously close and reuse it;
		this could lead to a pending event being delivered for the new
		descriptor. Guard against this by changing the cookie for the
		callback chain. */
		if (entry->active_.load(std::memory_order_relaxed) == 0) {
			uint32_t old_cookie = cookie_.fetch_add(1, std::memory_order_relaxed);
			uint32_t new_cookie = old_cookie + 1;
			entry->cookie_.store(new_cookie, std::memory_order_relaxed);
			if (((old_cookie ^ new_cookie) & (1<<16)) != 0) {
				need_cookie_sync_ = true;
			}
		}

		/* put into list of elements marked for deferred cancellation */
		cb->inactive_next_ = inactive_;
		inactive_ = cb;
	}

	/* must be called after read_unlock/write_lock indicates that synchronization
	is required */
	ioready_callback * file_descriptor_table::synchronize(void) noexcept
	{
		volatile_table * tab = table_.load(std::memory_order_relaxed);
		volatile_table * old = tab->old_;
		/* deallocate old tables */
		tab->old_ = 0;
		while (old) {
			volatile_table * next = old->old_;
			delete old;
			old = next;
		}

		/* remove inactive callbacks */
		ioready_callback * link = inactive_;
		while (link) {
			file_descriptor_chain * entry = tab->entries_[link->fd_].load(std::memory_order_relaxed);
			if (link->prev_) {
				link->prev_->next_ = link->next_;
			} else {
				entry->first_ = link->next_;
			}
			if (link->next_) {
				link->next_->prev_ = link->prev_;
			} else {
				entry->last_ = link->prev_;
			}
			link = link->inactive_next_;
		}

		if (need_cookie_sync_) {
			need_cookie_sync_ = false;
			uint32_t current_cookie = cookie_.load(std::memory_order_relaxed);
			for (size_t n = 0; n < tab->capacity_; ++n) {
				file_descriptor_chain * entry = tab->entries_[n];
				if (!entry) {
					continue;
				}
				entry->cookie_.store(current_cookie, std::memory_order_relaxed);
			}
		}

		/* return first inactive callback so they can be deallocated
		outside the lock */
		link = inactive_;
		inactive_ = nullptr;
		return link;
	}

	file_descriptor_table::volatile_table *
	file_descriptor_table::get_extend_table_slow(volatile_table * tab, int maxfd) /*throw(std::bad_alloc)*/
	{
		size_t new_capacity = tab->capacity_ * 2;
		if (new_capacity <= (size_t)maxfd) {
			new_capacity = maxfd + 1;
		}

		volatile_table * newtab = new volatile_table(new_capacity);
		for (size_t n = 0; n < tab->capacity_; ++n) {
			newtab->entries_[n].store(tab->entries_[n].load(std::memory_order_relaxed), std::memory_order_relaxed);
		}

		newtab->old_ = tab;

		table_.store(newtab, std::memory_order_release);
		tab = newtab;

		return tab;
	}

}
