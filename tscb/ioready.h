/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1.
 * Refer to the file_event "COPYING" for details.
 */

#ifndef TSCB_IOREADY_H
#define TSCB_IOREADY_H

/**
	\page ioready_descr I/O readiness

	The class \ref tscb::ioready_service "ioready_service" defines the
	interface which receivers of I/O readiness callbacks can use to register
	themselves. Several concrete implementations of this interface exist and
	may be used on different platforms.

	\section ioready_registration Registration for events

	Interested receivers can register functions to be called when file
	descriptors are ready for IO at the
	\ref tscb::ioready_service "ioready_service" interface.
	Receivers can use the
	\ref tscb::ioready_service::watch "ioready_service::watch" functions
	for this purpose; they can be used in the following fashion:

	\code
		class IOHandler {
		public:
			void onDataReady(int fd, tscb::ioready_events event_mask) noexcept
			{
				// process data
			}

			void finish() noexcept
			{
				close(descriptor);
				delete this;
			}

			void register(tscb::ioready_service *service) throw(std::bad_alloc)
			{
				service->watch(boost::bind(&IOHandler::onDataReady, this, descriptor, _1),
					descriptor, tscb::ioready_input);
			}

			int descriptor;
		};
	\endcode

	In the previous example, the <TT>onDataReady</TT> method of the
	corresponding object would be called whenever data is
	available on <TT>descriptor</TT>. The same approach also allows
	free-standing functions to be called:

	\code
		class IOContext;

		void onDataReady(IOContext *ctx, int fd, tscb::ioready_events event_mask) noexcept
		{
			// process data
		}

		void finish(IOContext *ctx) noexcept
		{
			delete IOContext;
		}

		void registration(IOContext *ctx, int fd, tscb::ioready_service *service)
			throw(std::bad_alloc)
		{
			service->watch(boost::bind(&onDataReady, ctx, fd, _1),
				fd, tscb::ioready_input);
		}
	\endcode

	Like in the previous example, the function <TT>onDataReady</TT> will
	be called whenever data is available on the file descriptor. Note that
	it is generally advisable to use some sort of "smart pointer" to convey
	context data to the called function, e.g.:

	\code
		class IOHandler {
		public:
			void onDataReady(int fd, tscb::ioready_events event_mask) noexcept
			{
				// process data
			}

			void finish() noexcept
			{
				close(descriptor);
				delete this;
			}

			void register(tscb::ioready_service *service) throw(std::bad_alloc)
			{
				service->watch(boost::bind(&IOHandler::onDataReady,
					boost::intrusive_ptr<IOHandler>(this), descriptor, _1),
					descriptor, tscb::ioready_input);
			}

			int descriptor;
			int reference_count;
		};
		static inline void intrusive_ptr_add_ref(IOHandler *e) {...}
		static inline void intrusive_ptr_release(IOHandler *e) {...}
	\endcode

	Using this mechanism, the target object will be retained for as long
	as the function object is callable. The function object will be destroyed
	"soon after" cancelling the callback (see next section).

	\section ioready_callback_cookies Callback connection handles for ioready callbacks

	The \ref tscb::ioready_service::watch "ioready_service::watch" functions
	return a connection handle that represents the
	link between the callback service provider and the receiver. The
	return value can be stored by the caller:

	\code
		tscb::ioready_connection conn;
		conn=service->watch(boost::bind(&IOHandler::onDataReady, this, descriptor, _1),
			descriptor, tscb::ioready_input);
	\endcode

	The connection object can later be used for two purposes:
	1. Modify the event mask:

	\code
		conn.modify(tscb::ioready_input | tscb::ioready_output);
	\endcode

	This call changes the state the file descriptor is checked for. The
	event mask may legally be \ref tscb::ioready_none which temporarily
	disables notification for both input and output, but note that
	error notifications <I>may</I> still be generated.

	2. Break the connection:

	\code
		conn.disconnect();
	\endcode

	The function will no longer be invoked afterwards (see
	section \ref design_concurrency_reentrancy for a precise definition
	of the guarantee). The function object along with bound parameters will be
	destroyed (possibly immediately, possibly later). It is the caller's
	responsibility to ensure that the file descriptor remains valid (is
	not closed) until the function object is destroyed. The safest way
	to achieve this is to encapsulate the descriptor inside an object and
	bind a reference to it to the function object:

	\code
		class fd_tracker {
		public:
			fd_tracker(int _fd) : fd(_fd) {}
			~fd_tracker() {close(fd);}
			int fd;
		};

		void io_handler(boost::shared_ptr<fd_tracker> fd, tscb::ioready_events events);

		main()
		{

			int fd=socket(...);
			boost::shared_ptr<fd_tracker> fdt(new fd_tracker(fd));
			...
			ioready->watch(boost::bind(&io_handler, fdt, _1));
		}
	\endcode

	[Side node: \ref tscb::ioready_connection "ioready_connection" objects may be
	downcast to \ref tscb::connection "connection" objects, losing the ability to
	modify the event mask]

	\section ioready_dispatcher_descr ioready dispatchers

	Free-standing implementations of the \ref tscb::ioready_service
	"ioready_service" interface derive from the \ref tscb::ioready_dispatcher
	"ioready_dispatcher" interface. Operating system-dependent mechanisms are
	used to query the state information of watched file descriptors.
	Specifically, the following methods are supported:

	<UL>
		<LI>
			Using the <TT>select</TT> system call:
			\ref tscb::ioready_dispatcher_select "ioready_dispatcher_select"
			(available all Posix systems)
		</LI>
		<LI>
			Using the <TT>poll</TT> system call:
			\ref tscb::ioready_dispatcher_poll "ioready_dispatcher_poll"
			(available most Posix systems)
		</LI>
		<LI>
			Using the <TT>epoll</TT> family of system calls:
			\ref tscb::ioready_dispatcher_epoll "ioready_dispatcher_epoll"
			(available on Linux systems)
		</LI>
		<LI>
			Using the <TT>kqueue</TT> family of system calls:
			\ref tscb::ioready_dispatcher_kqueue "ioready_dispatcher_kqueue"
			(available on BSD and derived systems)
		</LI>
	</UL>

	The \ref tscb::ioready_dispatcher "ioready_dispatcher" interface adds two
	methods in addition to those inherited from \ref tscb::ioready_service
	"ioready_service". The first method,
	\ref tscb::ioready_dispatcher::dispatch "ioready_dispatcher::dispatch",
	drives the dispatching mechanism and invokes the callbacks registered
	previously (see section \ref ioready_registration):

	\code
		tscb::ioready_dispatcher *dispatcher;
		...
		std::chrono::steady_clock::duration timeout(1000);

		while(true) dispatcher->dispatch(&timeout, 16);
	\endcode

	(Refer to \ref tscb::ioready_dispatcher::dispatch
	"ioready_dispatcher::dispatch" for a full description
	of the function). Like in the example above, at least one thread
	must periodically call \ref tscb::ioready_dispatcher::dispatch
	"ioready_dispatcher::dispatch";
	the function will check the registered receivers and process pending
	IO readiness callbacks.

	The second method, \ref tscb::ioready_dispatcher::get_eventtrigger
	"ioready_dispatcher::get_eventtrigger",
	allows to obtain a reference to an event trigger associated with
	the dispatcher:

	\code
		tscb::ioready_dispatcher * dispatcher;
		...
		tscb::eventtrigger & trigger = dispatcher->get_eventtrigger();
	\endcode

	After returning from \ref tscb::ioready_dispatcher::dispatch
	"ioready_dispatcher::dispatch", the flag will be in <I>cleared</I>
	state. When the flag is in <I>set</I> state, the next call
	to \ref tscb::ioready_dispatcher::dispatch "ioready_dispatcher::dispatch"
	will not block waiting for a timeout, but return immediately.

	Raising the event flag will also interrupt any waiting operation
	inside \ref tscb::ioready_dispatcher::dispatch "ioready_dispatcher::dispatch"
	and cause the call to return as soon as possible:

	\code
		tscb::eventtrigger & trigger;
		...
		trigger.set();
	\endcode

	Applications can use this capability as a mechanism to force a
	premature exit from the dispatcher loop. This is useful when
	the thread busy with dispatching I/O readiness events must also
	check other event sources (e.g. timers, see section \ref
	timerqueue_dispatcher_descr).

	Note:

	<UL>
		<LI>
			the \ref eventtrigger is associated with the <I>dispatcher</I> and not
			one particular <I>thread</I> entering
			\ref tscb::ioready_dispatcher::dispatch
			"ioready_dispatcher::dispatch";
		</LI>
		<LI>
			the lifetime of the \ref eventtrigger matches the lifetime of
			the dispatcher
		</LI>
		<LI>
			depending on the implementation, activating the trigger may
			cause one, many, or all threads currently in
			\ref tscb::ioready_dispatcher::dispatch "ioready_dispatcher::dispatch"
			to return prematurely.
		</LI>
		<LI>
			activating the trigger causes interruption to
			\ref tscb::ioready_dispatcher::dispatch
			"ioready_dispatcher::dispatch" only once -- unless
			the trigger is activated again after the call
			returns, the next call to \ref tscb::ioready_dispatcher::dispatch
			"ioready_dispatcher::dispatch" will wait for events
			normally.
		</LI>
	</UL>
*/

#include <chrono>
#include <functional>
#include <mutex>

#include <tscb/eventflag.h>
#include <tscb/signal.h>

namespace tscb {

class ioready_service;
class ioready_callback;

/**
	\brief I/O readiness event mask

	Bitmask encoding possible events on a file descriptor.
	When requesting notification through \ref tscb::ioready_service::watch
	"ioready_service::watch", the caller should build a mask
	consisting of the bitwise | (or) of all events it is
	interested in.

	The called function will receive a mask with bits set for all
	events that occurred intermittently.
*/
typedef enum {
	ioready_none = 0,
	/**
		\brief Input event mask

		Bit indicating "descriptor ready for input"; see
		\ref tscb::ioready_service::watch "ioready_service::watch".
	*/
	ioready_input = 0x0001,
	/**
		\brief Output event mask

		Bit indicating "descriptor ready for output"; see
		\ref tscb::ioready_service::watch "ioready_service::watch".
	*/
	ioready_output = 0x0002,
	/**
		\brief Error event mask

		Bit indicating "error on descriptor"; see
		\ref tscb::ioready_service::watch "ioready_service::watch".
		Note that you do not have to explicitly request this
		type of event -- when requesting \ref input or \ref output
		events, this event may always be delivered on an error
		condition.
	*/
	ioready_error = 0x0100,
	/**
		\brief Hangup event mask

		Bit indicating "hangup by peer on descriptor"; see
		\ref tscb::ioready_service::watch "ioready_service::watch".
		Note that you do not have to explicitly request this
		type of event -- when requesting \ref input or \ref output
		events, this event may always be delivered on an error
		condition.
	*/
	ioready_hangup = 0x0200
} ioready_events;

static inline ioready_events
operator|(ioready_events a, ioready_events b)
{return (ioready_events)((int)a|(int)b);}

static inline ioready_events
operator&(ioready_events a, ioready_events b)
{return (ioready_events)((int)a&(int)b);}

static inline ioready_events
operator^(ioready_events a, ioready_events b)
{return (ioready_events)((int)a^(int)b);}

static inline ioready_events
operator~(ioready_events a)
{return (ioready_events)~(int)a;}

static inline ioready_events &
operator|=(ioready_events &a, ioready_events b)
{a=a|b; return a;}
static inline ioready_events &
operator&=(ioready_events &a, ioready_events b)
{a=a&b; return a;}
static inline ioready_events &
operator^=(ioready_events &a, ioready_events b)
{a=a^b; return a;}

/**
	\brief IO readiness callback link
*/
class ioready_connection {
public:
	/**
		\brief callback link for I/O readiness events on file descriptors

		This class represents a link that has been established by registering
		a callback for I/O readiness events on file (or socket) descriptor
		events (see \ref tscb::ioready_service). Like its base class,
		\ref tscb::abstract_callback, it supports cancellation, but additionally
		it also allows to dynamically change the event notification mask
		(which is much more efficient than to cancel the previous callback and
		register a new one).
	*/
	class link_type : public connection::link_type {
	public:
		using pointer = boost::intrusive_ptr<link_type>;

		~link_type() noexcept override;

		void
		disconnect() noexcept override = 0;

		bool
		is_connected() const noexcept override = 0;

		virtual void
		modify(ioready_events new_event_mask) noexcept = 0;

		virtual ioready_events
		event_mask() const noexcept = 0;

	};

	inline
	ioready_connection() noexcept {}

	inline explicit
	ioready_connection(boost::intrusive_ptr<link_type> link) noexcept
		: link_(std::move(link))
	{
	}

	inline
	ioready_connection(const ioready_connection & other) noexcept
		: link_(other.link_)
	{
	}

	inline
	ioready_connection(ioready_connection && other) noexcept
		: link_(std::move(other.link_))
	{
	}

	inline ioready_connection &
	operator=(const ioready_connection & other) noexcept
	{
		link_ = other.link_;
		return *this;
	}

	inline ioready_connection &
	operator=(ioready_connection && other) noexcept
	{
		link_ = std::move(other.link_);
		return *this;
	}

	inline void
	swap(ioready_connection & other) noexcept
	{
		link_.swap(other.link_);
	}

	inline void
	disconnect() noexcept
	{
		if (link_) {
			link_->disconnect();
			link_.reset();
		}
	}

	inline bool
	is_connected() const noexcept
	{
		return link_ && link_->is_connected();
	}

	inline void
	modify(ioready_events events) noexcept
	{
		if (link_) {
			link_->modify(events);
		}
	}

	inline ioready_events
	event_mask() const noexcept
	{
		return link_ ? link_->event_mask() : ioready_none;
	}

	inline const link_type::pointer &
	link() const noexcept
	{
		return link_;
	}

	inline link_type *
	get() const noexcept
	{
		return link_.get();
	}

	inline operator connection() const noexcept
	{
		return connection(link_);
	}

private:
	link_type::pointer link_;
};

/**
	\brief Scoped connection between signal and receiver

	Variant of \ref ioready_connection object that automatically
	breaks the connection when this object goes out of scope.

	\warning This class can be used by an object to track
	signal connections to itself, and have all connections
	broken automatically when the object is destroyed.
	Only do this when you know that all callback invocations
	as well as the destructor will always run from the
	same thread.
*/
class scoped_ioready_connection {
public:
	inline ~scoped_ioready_connection () noexcept {
		disconnect();
	}

	inline scoped_ioready_connection() noexcept {}
	inline scoped_ioready_connection (const ioready_connection & c) noexcept : connection_(c) {}
	inline scoped_ioready_connection (ioready_connection && c) noexcept : connection_(std::move(c)) {}

	scoped_ioready_connection (const scoped_ioready_connection  & other) = delete;
	scoped_ioready_connection  & operator=(const scoped_ioready_connection  & other) = delete;

	scoped_ioready_connection (scoped_ioready_connection  && other) noexcept
	{
		swap(other);
	}
	scoped_ioready_connection  &
	operator=(scoped_ioready_connection  && other) noexcept
	{
		swap(other);
		return *this;
	}

	inline scoped_ioready_connection & operator=(const ioready_connection & c) noexcept
	{
		disconnect();
		connection_ = c;
		return *this;
	}

	inline void
	swap(scoped_ioready_connection & other) noexcept
	{
		connection_.swap(other.connection_);
	}

	inline bool
	is_connected() const noexcept
	{
		return connection_.is_connected();
	}

	inline void
	disconnect() noexcept
	{
		connection_.disconnect();
	}

	inline void
	modify(ioready_events events) noexcept
	{
		connection_.modify(events);
	}

	inline ioready_events
	event_mask() const noexcept
	{
		return connection_.event_mask();
	}

protected:
	ioready_connection connection_;
};

/**
	\brief Registration for IO readiness events

	This class provides the registration interface for IO readiness
	callbacks. Receivers can use the \ref watch methods
	of this class to indicate their interest in readiness events
	on a specific file descriptor. See section \ref ioready_registration
	for examples on how to use this interface.
*/
class ioready_service {
public:
	virtual ~ioready_service() noexcept;

	/**
		\brief register callback for file event

		\param function
			Function to be called in case of readiness for IO
		\param fd
			The file descriptor which should be monitored for events
		\param event_mask
			Set of events for which notification should be delivered
		\return
			Link object

		This function requests callbacks for IO readiness on a file
		descriptor (on Unix systems the objects for which events can be
		delivered include pipes, sockets, terminal lines, and a number of
		special device files).

		The event_mask indicates what events the callee
		is interested in; it is a bitwise "or" of one of the
		following constants:

		<UL>
			<LI>
				<TT>tscb::ioready_input</TT>: descriptor is ready
				for reading
			</LI>
			<LI>
				<TT>tscb::ioready_output</TT>: descriptor is ready
				for writing
			</LI>
			<LI>
				<TT>tscb::ioready_error</TT>: an (unspecified) error occurred on the descriptor
			</LI>
			<LI>
				<TT>tscb::ioready_hangup</TT>: remote end has closed the connection
			</LI>
		</UL>

		The passed function object will be called with a parameter
		indicating the set of events that have occurred. The returned
		link object may be used to modify the set of watched events
		or cancel the callback.
	*/
	virtual ioready_connection
	watch(std::function<void(tscb::ioready_events)> function, int fd, tscb::ioready_events event_mask) = 0;
};

/**
	\brief Dispatcher for IO readiness events

	This class provides the interface for free-standing
	IO readiness callback dispatchers; it is implemented by several
	classes that use operating system-specific methods for collecting
	the required information.
*/
class ioready_dispatcher : public ioready_service {
public:
	~ioready_dispatcher() noexcept override;

	/**
		\brief Dispatch an event or wait until timeout

		\param timeout
			Timeout, or a NULL pointer
		\param max
			Maximum number of events to be processed
		\return
			Number of events processed

		Check state of all registered file descriptors and process
		registered callback functions.

		All pending events are processed up to the indicated maximum
		number of events; unprocessed events will be processed in further
		calls to \ref dispatch.

		The function will return indicating the number of events processed
		as soon as one the following conditions becomes true:
		<UL>
			<LI>
				at least one (and up to <TT>max</TT>) number of events
				have been processed
			</LI>
			<LI>
				no event was processed, but a timeout has occured waiting
				for events, as indicated by the <TT>timeout</TT> parameter;
				if <TT>timeout</TT> is a NULL pointer, \ref dispatch
				will wait indefinitely
			</LI>
			<LI>
				the \ref tscb::eventtrigger associated with the dispatcher
				has been raised (see \ref get_eventtrigger)
			</LI>
			<LI>
				registration: depending on the dispatcher implementation
				dispatching may be interrupted if callbacks
				are registered, modified or cancelled by another
				thread
			</LI>
		</UL>

		The function is generally reentrant, multiple threads
		can enter the dispatching loop simultaneously. However depending
		on the concrete dispatcher implementation the behaviour
		may not be terribly useful, as some dispatchers will attempt
		to dispatch the same event in multiple threads.
	*/
	virtual size_t
	dispatch(
		const std::chrono::steady_clock::duration * timeout,
		std::size_t limit = std::numeric_limits<std::size_t>::max()) = 0;

	/**
		\brief Dispatch a number of pending events

		\param max
			Maximum number of events to be processed
		\return
			Number of events processed

		Check state of all registered file descriptors and process
		registered callback functions.

		All pending events are processed up to the indicated maximum
		number of events; unprocessed events will be processed in further
		calls to \ref dispatch.

		The function will return after processing the specified maximum
		number of events. It will not wait for events if none is pending.

		The function is generally reentrant, multiple threads
		can enter the dispatching loop simultaneously. However depending
		on the concrete dispatcher implementation the behaviour
		may not be terribly useful, as some dispatchers will attempt
		to dispatch the same event in multiple threads.
	*/
	virtual size_t
	dispatch_pending(
		std::size_t limit = std::numeric_limits<std::size_t>::max()) = 0;

	/**
		\brief Event trigger to wake up prematurely

		Returns a pointer to an \ref eventtrigger that allows interrupting
		waiting for the timeout specified as argument to \ref dispatch.
		Activating the trigger (possibly from another thread) via
		\ref tscb::eventtrigger::set "eventtrigger::set" will cause
		the \ref dispatch function to return prematurely. Activating
		the trigger (possibly multiple times) after one call to
		\ref dispatch has returned will only cause the next call to
		\ref dispatch to return, any later invocation will wait for
		events normally.

		This functionality is mostly useful in multi-threaded
		programs to interrupt one dispatcher, e.g. because a
		timeout has to be shortened. It is also permissible to
		activate the trigger from a posix signal handler to have
		the main thread process asynchronously queued events.

		The \ref eventtrigger returned is inseparably associated
		with the dispatcher; its lifetime is implicitly determined
		by the lifetime of the dispatcher object, and it must
		not be destroyed by the caller. Multiple calls to
		\ref get_eventtrigger will likely always return the same trigger
		object (instead of creating a new one).
	*/
	virtual eventtrigger &
	get_eventtrigger() noexcept = 0;

	/**
		\brief Instantiate ioready_dispatcher

		Instantiates a "standalone" \ref ioready_dispatcher,
		using the dispatching mechanism most suitable for the
		current platform. The function
		will select the "best" dispatcher available on the current
		platform dynamically; the order of preference is:

		<UL>
			<LI>\ref tscb::ioready_dispatcher_kqueue "ioready_dispatcher_kqueue"</LI>
			<LI>\ref tscb::ioready_dispatcher_epoll "ioready_dispatcher_epoll"</LI>
			<LI>\ref tscb::ioready_dispatcher_poll "ioready_dispatcher_poll"</LI>
			<LI>\ref tscb::ioready_dispatcher_select "ioready_dispatcher_select"</LI>
		</UL>
	*/
	static
	ioready_dispatcher *
	create() /* throw(std::bad_alloc, std::runtime_error) */;
};

/**
	\brief Create dispatcher for ioready events

	Select an \ref tscb::ioready_dispatcher "ioready_dispatcher"
	implementation and instantiate dispatcher class. The function
	will select the "best" dispatcher available on the current
	platform dynamically; the order of preference is:

	<UL>
		<LI>\ref tscb::ioready_dispatcher_kqueue "ioready_dispatcher_kqueue"</LI>
		<LI>\ref tscb::ioready_dispatcher_epoll "ioready_dispatcher_epoll"</LI>
		<LI>\ref tscb::ioready_dispatcher_poll "ioready_dispatcher_poll"</LI>
		<LI>\ref tscb::ioready_dispatcher_select "ioready_dispatcher_select"</LI>
	</UL>
*/
ioready_dispatcher *
create_ioready_dispatcher() /*throw(std::bad_alloc, std::runtime_error)*/;

}

#endif
