/* -*- C++ -*-
 * (c) 2009 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1.
 * Refer to the file "COPYING" for details.
 */

#ifndef TSCB_SIGNAL_H
#define TSCB_SIGNAL_H

/**
	\page signal_descr Signals and slots
	
	\ref tscb::signal "signal" and related classes provide a
	thread-safe and highly efficient mechanism to implement the
	observer pattern: The "observer" wants to observe the state of
	another object, and for this purpose the object to be observed
	(the "provider") provides a number of "signals" that are
	activated on specific events (such as state changes) and to
	which the "observer" can listen by connecting a callback
	function to signals of interest.
	
	Complex multi-threaded applications pose a challenge to an
	implementation of this mechanism as callbacks may be
	registered to, deregistered from or activated through signals
	from many threads concurrently.
	
	<TT>tscb</TT> supports this through the following class
	templates:
	
	- \ref tscb::signal_proxy "signal_proxy" provides the interface
	used by observers for registering notifications
	
	- \ref tscb::signal "signal" derives from \ref tscb::signal_proxy
	and additionally provides a mechanism for notifying all
	callbacks registered with the signal
	
	- \ref tscb::connection "connection" objects represent
	a callback connected to a signal; its main purpose is
	to provide a handle through with this connection can later
	be broken
	
	The \ref tscb::signal "signal" ... template class provides
	a generic callback mechanism. It allows one object
	(the "sender") to notify an arbitray number of other objects
	(the "receivers") of events by calling specified functions.
	
	\section callback_declaration Declaration of signals
	
	Signals are declared as (global or member) variables and
	should generally be declared in the following way:
	
	\code
		class MyClass {
		public:
			tscb::signal_proxy<void (int,int)> &
			onValueChange(void) throw() {return valueChange;}
			// will report old and new value to callbacks
			
			void setValue(int newval);
		private:
			tscb::signal<void (int, int)> valueChange;
			int value;
		}
	\endcode
	
	\section signal_emit Emitting signals
	
	\ref signal objects (as defined in the previous example) provide
	an overloaded () operator which will inform all callback functions
	registered with it:
	
	\code
		void MyClass::setValue(int newval)
		{
			int oldval=value;
			value=newval;
			// notify all registered callbacks
			valueChange(oldval, newval);
		}
	\endcode
	
	The overloaded () operator expects exactly the number and type
	of arguments as were used when the callback chain was declared.
	
	\section signal_register Registration
	
	All \ref tscb::signal "signals" derive from a
	\ref tscb::signal_proxy "signal_proxy" base clase that provide a
	\ref tscb::signal_proxy::connect "connect"
	member function to allow observers to add a new callback
	function. They can be used in the following fashion:
	
	\code
		class MyObserver {
		public:
			MyObserver(MyClass *c)
			{
				c->onValueChange().connect(boost::bind(&MyObserver::notify_change, this, _1, _2));
			}
		protected:
			void notify_change(int oldval, int newval)
			{
				printf("Value changed from %d to %d!\n", oldval, newval);
			}
		};
	\endcode
	
	In the previous example, the <TT>notify_change</TT> method of the
	corresponding <TT>MyObserver</TT> object would be called
	whenever the callback chain is being activated
	(see section \ref signal_emit above). Note the the
	function object created via <TT>boost::bind</TT> will be
	destroyed as soon as the callback is cancelled (see section \ref signal_connections below).
	
	"Free" functions can be bound in the same way:
	
	\code
		char *msg="Incredible!";
		
		void notify_change(char *ctx, int oldval, int newval)
		{
			printf("%s Value changed from %d to %d!\n", ctx, oldval, newval);
		}
		
		int main(void)
		{
			MyObject obj;
			obj.onValueChange.connect(boost::bind(notify_change, msg, _1, _2));
			obj.setValue(42);
		}
	\endcode
	
	Like in the previous example, the function <TT>notify_change</TT>
	will be called whenever the callback chain is being activated.
	
	\section signal_connections Connection management
	
	The
	\ref tscb::signal::connect "connect" method
	returns a connection object that represents the
	connection between the provider and the obverver. The
	return value can be stored by the caller:
	
	\code
		tscb::connection conn;
		conn=c->onValueChange.connect(boost::bind(&MyObserver::notify_change, this, _1, _2));
	\endcode
	
	The conection object can later be used to cancel the
	callback:
	
	\code
		conn.disconnect();
	\endcode
	
	The associated callback function will not be invoked subsequently,
	see section \ref design_concurrency_reentrancy for the precise guarantee.
	The data
	associated with the function object will be released as soon as it is guaranteed
	that the callback function cannot be called again (e.g. from other threads).
	This is useful e.g. if <TT>boost::shared_ptr</TT> or
	<TT>boost::intrusive_ptr</TT> is used to assist in
	tracking the lifetime of objects:
	
	\code
		tscb::connection conn;
		boost::shared_ptr<MyObserver> obj(new MyObserver);
		conn=c->onValueChange.connect(boost::bind(&MyObserver::notify_change)
			boost::shared_ptr(obj), _1, _2));
	\endcode
	
	In this case, a <TT>boost::shared_ptr</TT> instance is kept for as long as
	the callback can be activated -- thus the object will not be deallocated
	until it is guaranteed that the callback cannot be delivered any longer.
	
	\section signal_connections_single_threaded Connection management, single-threaded
	
	\warning The below is potentially not thread-safe and should
	be done with care.
	
	The return value of the \ref tscb::signal::connect "connect" method
	may be assigned to a \ref tscb::scoped_connection "scoped_connection"
	object (instead of a \ref tscb::connection "scoped_connection"):
	
	\code
		class MyObserver {
		public:
			MyObserver(MyClass *c)
			{
				conn = c->onValueChange().connect(boost::bind(&MyObserver::notify_change, this, _1, _2));
			}
		protected:
			void notify_change(int oldval, int newval)
			{
				printf("Value changed from %d to %d!\n", oldval, newval);
			}
			
			scoped_connection conn;
		};
	\endcode
	
	The impact of this is that the connection will be broken automatically
	when the \ref tscb::scoped_connection "scoped_connection" object
	goes out of scope or is destroyed otherwise. This provides a
	convenient mechanism for an object to track pending callbacks to
	itself, and have them broken automatically when the object
	is destroyed:
	
	\code
		MyObserver * observer = new MyObserver(c);
		...
		delete observer; // will implicitly perform conn.disconnect();
	\endcode
	
	\warning It is very important that you are on your own in ensuring that
	no other thread may race to perform a signal delivery while the object
	is being destroyed. You are safe if all signal delivery operations happen
	in the same thread as the deletion of the object (as is trivially the
	case when the program is single-threaded). 
	
	\section signal_connections_multi_threaded Connection management, multi-threaded
	
	The recommended pattern for dynamically managed objects that need to receive
	callbacks from multiple threads:
	
	\code
		class MyObserver;
		static inline void intrusive_ptr_add_ref(const MyObserver *);
		static inline void intrusive_ptr_release(const MyObserver *);
		
		class MyObserver {
		public:
			typedef boost::intrusive_ptr<MyObserver> pointer;
			
			static pointer
			create(MyClass * c)
			{
				// constructor initializes refcount to 1, so no initial increment
				return pointer(new MyClass(c), false);
			}
			
			void
			disconnect(void)
			{
				conn.disconnect();
			}
		protected:
			MyObserver(MyClass *c)
				: refcount(1)
			{
				// use ref-counted pointer instead of naked pointer in
				// binding function object: causes functional to acquire
				// a reference to this object, thus this object will not
				// be deleted when there might be outstanding callbacks
				conn = c->onValueChange().connect(boost::bind(&MyObserver::notify_change, pointer(this), _1, _2));
				// it is important that the constructor itself holds a reference
				// before registering the callback: there could be a race
				// causing the connection to be broken from the other side
				// before the constructor finishes, which would cause the
				// function object to drop its reference. If the reference
				// count were dropped to zero, the object would be
				// destroyed before it was fully constructed!
			}
			
			void notify_change(int oldval, int newval)
			{
				std::cout << "Value changed from " << oldval << " to " << newval << "!\n";
			}
			
			connection conn;
			
			// atomic reference count implementation
			mutable atomic_int refcount;
			friend void intrusive_ptr_add_ref(const MyObserver * obj)
			{
				obj->fetch_add(memory_order_relaxed, 1);
			}
			friend void intrusive_ptr_release(const MyObserver * obj)
			{
				if (obj->fetch_sub(memory_order_relaxed, 1) == 1) {
					atomic_thread_fence(memory_order_acquire);
					delete obj;
				}
			}
		};
	\endcode
	
*/

#include <stdexcept>

#include <boost/intrusive_ptr.hpp>
#include <boost/function.hpp>

#include <tscb/thread>
#include <tscb/deferred>
#include <tscb/compiler>

namespace tscb {
	
	/**
		\brief Abstract base of all callback objects
		
		This object represents the link between a sender/caller, from
		which notification is requested, to a reciever/callee, to which
		notification is to be delivered. It is an abstract base class
		for all different kinds of links established through the various
		notification interfaces (callback_chains, file or timer events).
	*/
	class abstract_callback {
	public:
		inline abstract_callback(void) throw() : refcount(1) {}
		virtual ~abstract_callback(void) throw();
		/**
			\brief Break the connection
			
			Calling this function will break the notification connection. It will
			usually cease notifications to be delivered some time after this
			function has returned. The exact semantic guarantee is:
			
			<UL>
				<LI>
					no notification will be delivered within the same thread
					that has called \ref disconnect after \ref disconnect has
					returned (i.e. within the same thread, \ref disconnect
					is synchronous)
				</LI>
				<LI>
					notifications in other threads may be delivered after
					\ref disconnect has returned in one thread, but only
					for events that occured before \ref disconnect has returned
					(i.e. for other threads, cancellation is asynchronous).
				</LI>
			</UL>
			
			The weak synchronicity guarantee allows implementations that
			provide excellent concurrency. Furthermore it allows
			\ref disconnect to be called from arbitrary contexts: from within
			the callback to be cancelled, from different threads etc. It
			is guaranteed to be deadlock free.
			
		*/
		virtual void disconnect(void) throw()=0;
		/**
			\brief Test if connection is alive
			
			\return True if connected, false if disconnected
		*/
		virtual bool connected(void) const throw()=0;
		
		/** \internal \brief Increase reference count */
		inline void pin(void) throw() {refcount.fetch_add(1, memory_order_relaxed);}
		/** \internal \brief Decrease reference count */
		inline void release(void) throw()
		{
			if (refcount.fetch_sub(1, memory_order_release) == 1) {
				atomic_thread_fence(memory_order_acquire);
				delete this;
			}
		}
		
	private:
#ifdef _LIBTSCB_CALLBACK_UNITTESTS
	public:
#endif
		atomic_int refcount;
	};
	
	static inline void intrusive_ptr_add_ref(abstract_callback *t) throw()
	{
		t->pin();
	}
	
	static inline void intrusive_ptr_release(abstract_callback *t) throw()
	{
		t->release();
	}
	
	/**
		\brief Connection between signal and receiver
		
		Every other connection implemented in this library can be
		downcast to this type.
	*/
	class connection {
	public:
		inline connection(abstract_callback *_callback = 0, bool add_ref = true) throw() : callback(_callback)
		{if (callback && add_ref) callback->pin();}
		
		inline ~connection(void) throw()
		{if (callback) callback->release();}
		
		inline connection(const connection & conn) throw()
		{callback=conn.callback; if (callback) callback->pin();}
		
		inline connection &operator=(const connection & conn) throw()
		{if (callback) callback->release(); callback=conn.callback; if (callback) callback->pin(); return *this;}
		
		inline connection &operator=(abstract_callback * _callback) throw()
		{if (callback) callback->release(); callback=_callback; if (callback) callback->pin(); return *this;}
		
		inline void disconnect(void) throw()
		{if (callback) {callback->disconnect(); callback->release(); callback=0;}}
		
		inline bool connected(void) const throw()
		{return callback && callback->connected();}
		
		#ifdef __GXX_EXPERIMENTAL_CXX0X__
		template<typename ConnectionType>
		inline connection(ConnectionType && conn) throw()
		{callback=conn.callback; conn.callback=0;}
		
		template<typename ConnectionType>
		inline connection &operator=(ConnectionType && conn) throw()
		{callback=conn.callback; conn.callback=0; return *this;}
		#endif
		
		inline abstract_callback * get(void) const throw()
		{return callback;}
		
	private:
		abstract_callback *callback;
	};
	
	/**
		\brief Scoped connection between signal and receiver
		
		Variant of \ref connection object that automatically
		breaks the connection when this object goes out of scope.
		
		\warning This class can be used by an object to track
		signal connections to itself, and have all connections
		broken automatically when the object is destroyed.
		Only do this when you know that all callback invocations
		as well as the destructor will always run from the
		same thread.
	*/
	class scoped_connection {
	public:
		inline scoped_connection(void) throw() {}
		inline ~scoped_connection(void) throw() {disconnect();}
		
		inline bool connected(void) const throw() {return conn.connected();}
		
		inline void disconnect(void) throw() {conn.disconnect();}
		
		inline scoped_connection(const connection & _conn) throw() : conn(_conn) {}
		
		inline scoped_connection & operator=(const connection & _connection) throw()
		{disconnect(); conn = _connection; return *this;}
		
		inline scoped_connection(abstract_callback * callback) throw()
		{disconnect(); conn = callback;}
		
		inline scoped_connection & operator=(abstract_callback * callback) throw()
		{disconnect(); conn = callback; return *this;}
	protected:
		scoped_connection(const scoped_connection & other); /* deleted */
		scoped_connection & operator=(const scoped_connection & other); /* deleted */
		connection conn;
	};
	
	template<typename Signature> class signal_callback;
	template<typename Signature> class signal_proxy;
	template<typename Signature> class signal;
	
	/**
		\brief Callback from signals
		
		This object is the callback established by the receiver
		of a signal. It is used for "generic" callbacks,
		i.e. those registered with a \ref signal.
		
		See \ref signal_descr for usage.
	*/
	template<typename Signature>
	class signal_callback : public abstract_callback {
	public:
		/** \internal \brief Instantiate callback link */
		signal_callback(const boost::function<Signature> &_function) throw(std::bad_alloc)
			: function(_function), active_next(0), prev(0), next(0), chain(0)
		{}
		virtual ~signal_callback(void) throw()
		{}
		virtual void disconnect(void) throw()
		{
			registration_mutex.lock();
			if (chain) chain->remove(this);
			else registration_mutex.unlock();
		}
		virtual bool connected(void) const throw()
		{
			return chain!=0;
		}
		/** \internal \brief Called after cancellation to destroy the function object */
		void cancelled(void) throw()
		{
			function=0;
		}
	private:
		friend class signal<Signature>;
		
		/** \internal \brief Functional to be called on activation */
		boost::function<Signature> function;
		
		/** \internal \brief Next element in list of active callbacks */
		atomic<signal_callback *> active_next;
		/** \internal \brief Previous element in list */
		signal_callback *prev;
		/** \internal \brief Next element in list */
		signal_callback *next;
		/** \internal \brief Next element in list of callbacks with pending cancellation */
		signal_callback *deferred_cancel_next;
		
		/** \internal \brief Chain to which this object is registered */
		signal<Signature> *chain;
		
		/** \internal \brief Serialize deregistration */
		mutex registration_mutex;
	};
	
	/**
		\brief Registration interface for generic notifiers
		
		This object allows interested receivers to register themselves
		for notification.
		
		See \ref signal_descr for usage.
	*/
	template<typename Signature>
	class signal_proxy {
	public:
		/** \internal \brief Type of link connection to this chain */
		typedef signal_callback<Signature> callback_type;
		
		virtual ~signal_proxy(void) throw() {}
		
		/**
			\brief Add callback to signal
			
			\param function
				Function to be called when signal is activated
		*/
		virtual
		connection
		connect(const boost::function<Signature> &function) throw(std::bad_alloc)=0;
	};
	
	/**
		\brief Generic notifier chain
		
		This object allows interested receivers to register themselves
		for notification, and it allows a sender to deliver notification
		to all registered receivers.
		
		See \ref signal_descr for usage.
	*/
	template<typename Signature>
	class signal : public signal_proxy<Signature> {
	public:
		/** \internal \brief Type of link connection to this chain */
		friend class signal_callback<Signature>;
		/** \internal \brief Type of link connection to this chain */
		typedef signal_callback<Signature> callback_type;
		
		virtual
		connection
		connect(const boost::function<Signature> &function) throw(std::bad_alloc)
		{
			callback_type *l=new callback_type(function);
			push_back(l);
			return connection((abstract_callback *)l);
		}
		
		/**
			\brief Call all callback functions registered with the chain
			
			Calls all callback functions registered trough \ref connect
			with the given arguments.
		*/
		inline void operator()(void)
		{
			read_guard<signal<Signature> > guard(*this);
			callback_type *l=active.load(memory_order_consume);
			while(l) {
				l->function();
				l=l->active_next.load(memory_order_consume);
			}
		}
		/**
			\brief Call all callback functions registered with the chain
			
			Calls all callback functions registered trough \ref connect
			with the given arguments.
		*/
		template<typename Arg1>
		inline void operator()(Arg1 arg1)
		{
			read_guard<signal<Signature> > guard(*this);
			callback_type *l=active.load(memory_order_consume);
			while(l) {
				l->function(arg1);
				l=l->active_next.load(memory_order_consume);
			}
		}
		/**
			\brief Call all callback functions registered with the chain
			
			Calls all callback functions registered trough \ref connect
			with the given arguments.
		*/
		template<typename Arg1, typename Arg2>
		inline void operator()(Arg1 arg1, Arg2 arg2)
		{
			read_guard<signal<Signature> > guard(*this);
			callback_type *l=active.load(memory_order_consume);
			while(l) {
				l->function(arg1, arg2);
				l=l->active_next.load(memory_order_consume);
			}
		}
		/**
			\brief Call all callback functions registered with the chain
			
			Calls all callback functions registered trough \ref connect
			with the given arguments.
		*/
		template<typename Arg1, typename Arg2, typename Arg3>
		inline void operator()(Arg1 arg1, Arg2 arg2, Arg3 arg3)
		{
			read_guard<signal<Signature> > guard(*this);
			callback_type *l=active.load(memory_order_consume);
			while(l) {
				l->function(arg1, arg2, arg3);
				l=l->active_next.load(memory_order_consume);
			}
		}
		/**
			\brief Call all callback functions registered with the chain
			
			Calls all callback functions registered trough \ref connect
			with the given arguments.
		*/
		template<typename Arg1, typename Arg2, typename Arg3, typename Arg4>
		inline void operator()(Arg1 arg1, Arg2 arg2, Arg3 arg3, Arg4 arg4)
		{
			read_guard<signal<Signature> > guard(*this);
			callback_type *l=active.load(memory_order_consume);
			while(l) {
				l->function(arg1, arg2, arg3, arg4);
				l=l->active_next.load(memory_order_consume);
			}
		}
		/**
			\brief Call all callback functions registered with the chain
			
			Calls all callback functions registered trough \ref connect
			with the given arguments.
		*/
		template<typename Arg1, typename Arg2, typename Arg3, typename Arg4, typename Arg5>
		inline void operator()(Arg1 arg1, Arg2 arg2, Arg3 arg3, Arg4 arg4, Arg5 arg5)
		{
			read_guard<signal<Signature> > guard(*this);
			callback_type *l=active.load(memory_order_consume);
			while(l) {
				l->function(arg1, arg2, arg3, arg4, arg5);
				l=l->active_next.load(memory_order_consume);
			}
		}
		
		/**
			\brief Call all callback functions registered with the chain
			
			Calls all callback functions registered trough \ref connect
			with the given arguments.
		*/
		template<typename Arg1, typename Arg2, typename Arg3, typename Arg4, typename Arg5, typename Arg6>
		inline void operator()(Arg1 arg1, Arg2 arg2, Arg3 arg3, Arg4 arg4, Arg5 arg5, Arg6 arg6)
		{
			read_guard<signal<Signature> > guard(*this);
			callback_type *l=active.load(memory_order_consume);
			while(l) {
				l->function(arg1, arg2, arg3, arg4, arg5, arg6);
				l=l->active_next.load(memory_order_consume);
			}
		}
		
		/**
			\brief Call all callback functions registered with the chain
			
			Calls all callback functions registered trough \ref connect
			with the given arguments.
		*/
		template<typename Arg1, typename Arg2, typename Arg3, typename Arg4, typename Arg5, typename Arg6, typename Arg7>
		inline void operator()(Arg1 arg1, Arg2 arg2, Arg3 arg3, Arg4 arg4, Arg5 arg5, Arg6 arg6, Arg7 arg7)
		{
			read_guard<signal<Signature> > guard(*this);
			callback_type *l=active.load(memory_order_consume);
			while(l) {
				l->function(arg1, arg2, arg3, arg4, arg5, arg6, arg7);
				l=l->active_next.load(memory_order_consume);
			}
		}
		
		/**
			\brief Call all callback functions registered with the chain
			
			Calls all callback functions registered trough \ref connect
			with the given arguments.
		*/
		template<typename Arg1, typename Arg2, typename Arg3, typename Arg4, typename Arg5, typename Arg6, typename Arg7, typename Arg8>
		inline void operator()(Arg1 arg1, Arg2 arg2, Arg3 arg3, Arg4 arg4, Arg5 arg5, Arg6 arg6, Arg7 arg7, Arg8 arg8)
		{
			read_guard<signal<Signature> > guard(*this);
			callback_type *l=active.load(memory_order_consume);
			while(l) {
				l->function(arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8);
				l=l->active_next.load(memory_order_consume);
			}
		}
		
		signal(void) throw()
			: active(0), first(0), last(0), deferred_cancel(0)
		{}
		
		~signal(void) throw()
		{
			/* we cannot protect against anyone concurrently adding
			a new callback, but we must protect against concurrent
			removal */
			
			while(lock.read_lock()) synchronize();
			for(;;) {
				callback_type *tmp=active.load(memory_order_relaxed);
				if (!tmp) break;
				tmp->disconnect();
			}
			if (lock.read_unlock()) {
				/* the above cancel operations will cause synchronization
				to be performed at the next possible point in time; if
				there is no concurrent cancellation, this is now */
				synchronize();
			} else {
				/* this can only happen if some callback link was
				cancelled while this object is being destroyed; in
				that case we have to suspend the thread that is destroying
				the object until we are certain that synchronization has
				been performed */
				
				lock.write_lock_sync();
				synchronize();
				
				/* note that synchronize implicitly calls sync_finished,
				which is equivalent to write_unlock_sync for deferrable_rwlocks */
			}
		}
		
		/**
			\brief Disconnect all registered callbacks
			
			Disconnects all registered callbacks. The result is the
			same as if \ref connection::disconnect had been called on
			\ref connection object returned by \ref connect.
		*/
		inline void disconnect_all(void)
		{
			read_guard<signal<Signature> > guard(*this);
			callback_type * l = active.load(memory_order_consume);
			while(l) {
				l->disconnect();
				l = l->active_next.load(memory_order_consume);
			}
		}
		
	protected:
		/** \internal \brief Add link to end of chain */
		void push_back(callback_type *l) throw()
		{
			/* note: object has been fully constructed at this point,
			but the following lock acquisition only provides "acquire"
			semantics so that the memory references constructing
			this object are allowed to "leak" into the locked
			region. We therefore need an explicit fence here in
			order to avoid making an uninitialized element visible
			during traversal of the chain */
			atomic_thread_fence(memory_order_release);
			
			l->registration_mutex.lock();
			bool sync=lock.write_lock_async();
			
			l->next=0;
			l->prev=last;
			
			l->active_next.store(0, memory_order_relaxed);
			
			/* add element to active list; find all elements that have been removed
			from the full list and thus terminate the active list; point them to
			the newly-added element */
			
			callback_type *tmp=last;
			while(true) {
				if (!tmp) {
					if (!active.load(memory_order_relaxed)) active.store(l, memory_order_release);
					break;
				}
				if (tmp->active_next.load(memory_order_relaxed)) break;
				tmp->active_next.store(l, memory_order_release);
				tmp=tmp->prev;
			}
			
			/* insert into list of all elements*/
			if (last) last->next=l;
			else first=l;
			last=l;
			
			l->chain=this;
			
			l->registration_mutex.unlock();
			
			if (sync) synchronize();
			else lock.write_unlock_async();
		}
		/** \internal \brief Remove link from chain */
		void remove(callback_type *l) throw()
		{
			bool sync=lock.write_lock_async();
			if (l->chain==this) {
				/* remove element from active list; we have to make
				sure that all elements that pointed to "us" within
				the active chain now point to the following element,
				so this element is skipped from within the active chain */
				
				callback_type *tmp=l->prev;
				callback_type *next=l->active_next.load(memory_order_relaxed);
				while(true) {
					if (!tmp) {
						if (active.load(memory_order_relaxed)==l) active.store(next, memory_order_release);
						break;
					}
					if (tmp->active_next.load(memory_order_relaxed)!=l) break;
					tmp->active_next.store(next, memory_order_release);
					tmp=tmp->prev;
				}
				
				/* put on list of elements marked for deferred cancellation */
				l->deferred_cancel_next=deferred_cancel;
				deferred_cancel=l;
				
				/* remove pointer to chain, so a second call to ->cancel()
				will do nothing */
				l->chain=0;
			}
			
			l->registration_mutex.unlock();
			
			if (sync) synchronize();
			else lock.write_unlock_async();
		}
		/** \internal \brief Synchronize when reaching quiescent state */
		void synchronize(void) throw()
		{
			callback_type *do_cancel=deferred_cancel;
			
			/* first, "repair" the list structure by "correcting" all prev
			pointers */
			while(do_cancel) {
				/* we can now safely remove the elements from the list */
				if (do_cancel->prev) do_cancel->prev->next=do_cancel->next;
				else first=do_cancel->next;
				if (do_cancel->next) do_cancel->next->prev=do_cancel->prev;
				else last=do_cancel->prev;
				
				do_cancel=do_cancel->deferred_cancel_next;
			}
			
			/* now swap pointers while still under the lock; this is
			necessary to make sure that the destructor for each
			callback link object is called exactly once */
			do_cancel=deferred_cancel;
			deferred_cancel=0;
			lock.sync_finished();
			
			/* now we can release the callbacks, as we are sure that no one
			can "see" them anymore; the lock is dropped so side-effest
			of finalizing the links cannot cause deadlocks */
			
			while(do_cancel) {
				callback_type *tmp=do_cancel->deferred_cancel_next;
				do_cancel->cancelled();
				do_cancel->release();
				do_cancel=tmp;
			}
		}
		/** \internal \brief singly-linked list of active elements */
		atomic<callback_type *> active;
		
		/** \internal \brief thread synchronization */
		deferrable_rwlock lock;
		friend class read_guard<signal<Signature> >;
		
		/** \internal \brief First element in the chain, whether active or not */
		callback_type *first;
		/** \internal \brief Last element in the chain, whether active or not */
		callback_type *last;
		
		/** \internal \brief List of elements to be cancelled
		 
			singly-linked list of elements that have been removed from
			the active list, but are not yet removed from the full list
			and have not been discarded yet
		*/
		callback_type *deferred_cancel;
	};

}

#endif

