//
// basic_dispatcher.hpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2009, GuangZhu Wu  <guangzhuwu@gmail.com>
//
//This program is free software; you can redistribute it and/or modify it 
//under the terms of the GNU General Public License or any later version.
//
//This program is distributed in the hope that it will be useful, but 
//WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY 
//or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License 
//for more details.
//
//You should have received a copy of the GNU General Public License along 
//with this program; if not, contact <guangzhuwu@gmail.com>.
//
#ifndef basic_dispatcher_h__
#define basic_dispatcher_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "p2engine/push_warning_option.hpp"
#include "p2engine/config.hpp"
#include <map>
#include <boost/function.hpp>
#include "p2engine/pop_warning_option.hpp"

#include "p2engine/logging.hpp"
//#include "p2engine/fssignal.hpp"
#include "p2engine/safe_buffer_io.hpp"

namespace p2engine
{
	template<typename MessageType>
	struct messsage_extractor
	{
		typedef MessageType message_type;
		static message_type invalid_message()
		{
			return (~MessageType(0));
		}
		MessageType operator()(const safe_buffer& buf)
		{
			message_type messageType(~MessageType(0));
			if (buf.length() >= sizeof(message_type))
			{
				safe_buffer_io io((safe_buffer*)&buf);
				io >> messageType;
			}
			return messageType;
		}
	};

	template<>
	struct messsage_extractor<void>
	{
		typedef void message_type;
		static message_type invalid_message()
		{
			return;
		}
		void operator()(const safe_buffer&)
		{
			return;
		}
	};

	typedef messsage_extractor<void> void_message_extractor;

	template<typename MesssageExtraction> class basic_message_dispatcher;

	template<typename MesssageExtraction> class basic_connection_dispatcher;

	template<typename MessageSocket> class basic_acceptor_dispatcher;


#define basic_message_dispatcher_typedef(MesssageExtraction, typename)\
public:\
	typedef  MesssageExtraction   messsage_extractor_type; \
	typedef  typename messsage_extractor_type::message_type   message_type; \
	typedef  basic_message_dispatcher<messsage_extractor_type> dispatcher_type; \
	\
	typedef boost::function<void(safe_buffer)>      message_handle_type; \
	typedef boost::function<void(message_type, safe_buffer)> unknown_message_handle_type; \
	\
	typedef std::map<message_type, message_handle_type> message_dispatch_map; \


	template<typename MesssageExtraction>
	class basic_message_dispatcher
	{
		typedef basic_message_dispatcher<MesssageExtraction> this_type;
		SHARED_ACCESS_DECLARE;
		basic_message_dispatcher_typedef(MesssageExtraction, typename)

	protected:
		virtual ~basic_message_dispatcher(){}

	public:
		void register_message_handler(const message_type& msgType, message_handle_type h)
		{
			msg_handler_map_[msgType] = h;
		}
		void unregister_message_handler(const message_type& msgType)
		{
			msg_handler_map_.erase(msgType);
		}

		void register_invalid_message_handler(message_handle_type h)
		{
			invalid_message_handle_ = h;
		}
		void unregister_invalid_message_handler()
		{
			invalid_message_handle_.clear();
		}

		void register_unknown_message_handler(unknown_message_handle_type h)
		{
			unknown_message_handle_ = h;
		}
		void unregister_unknown_message_handler()
		{
			unknown_message_handle_.clear();
		}

		static void register_global_message_handler(const message_type& msgType, message_handle_type h)
		{
			s_receive_handler_map_[msgType] = h;
		}
		static void unregister_global_message_handler(const message_type& msgType)
		{
			s_receive_handler_map_.erase(msgType);
		}

		bool extract_and_dispatch_message(const safe_buffer& buf)
		{
			message_type msg_type = messsage_extractor_type()(buf);
			if (msg_type == messsage_extractor_type().invalid_message())
			{
				if (invalid_message_handle_.empty())
				{
					BOOST_ASSERT(0 && "dispatcher is not found for invalid message"&&msg_type);
					LOG(
						LogError("dispatcher is not found for invalid message %s", boost::lexical_cast<std::string>(msg_type).c_str());
					);
					return false;
				}
				else
				{
					invalid_message_handle_(buf);
				}
			}
			return dispatch_message(buf, msg_type);
		}

		virtual void unregister_all_dispatch_handler()
		{
			while (!msg_handler_map_.empty())
			{
				msg_handler_map_.begin()->second.clear();
				msg_handler_map_.erase(msg_handler_map_.begin());
			}
			invalid_message_handle_.clear();
			unknown_message_handle_.clear();
		}

		bool dispatch_message(const safe_buffer& buf, const message_type& msg_type)
		{
			typedef typename message_dispatch_map::iterator iterator;

			//1. search <message_soceket, net_event_handler_type> bind in this socket
			if (!msg_handler_map_.empty())
			{
				iterator itr(msg_handler_map_.find(msg_type));
				if (itr != msg_handler_map_.end())
				{
					if (itr->second)
						(itr->second)(buf);
					return true;
				}
			}

			//2. search <message_soceket, net_event_handler_type> bind in all socket
			if (!s_receive_handler_map_.empty())
			{
				iterator itr = s_receive_handler_map_.find(msg_type);
				if (itr != s_receive_handler_map_.end())
				{
					if (itr->second)
						(itr->second)(buf);
					return true;
				}
			}

			//3. not find, alert error
			//BOOST_ASSERT(0&&"can't find message dispatch_packet slot for message "&&msg_type);
			LOG(
				LogError("can't find message dispath slot for message %s", boost::lexical_cast<std::string>(msg_type).c_str());
			);
			if (!unknown_message_handle_.empty())
				unknown_message_handle_(msg_type, buf);

			return false;
		}

	public:
		message_dispatch_map msg_handler_map_;
		message_handle_type invalid_message_handle_;
		unknown_message_handle_type unknown_message_handle_;
		static message_dispatch_map s_receive_handler_map_;
	};
	template<typename MesssageExtraction>
	typename basic_message_dispatcher<MesssageExtraction>::message_dispatch_map
		basic_message_dispatcher<MesssageExtraction>::s_receive_handler_map_;


#define  basic_connection_dispatcher_typedef(MesssageExtraction, typename)\
public:\
	typedef boost::function<void(safe_buffer)>       received_handle_type; \
	typedef boost::function<void(const error_code&)> connected_handle_type; \
	typedef boost::function<void(const error_code&)> disconnected_handle_type; \
	typedef boost::function<void()>				  writable_handle_type; \

	template<typename MesssageExtraction>
	class basic_connection_dispatcher
		:public basic_message_dispatcher<MesssageExtraction>
	{
		typedef basic_connection_dispatcher<MesssageExtraction> this_type;
		SHARED_ACCESS_DECLARE;
		basic_connection_dispatcher_typedef(MesssageExtraction, typename);

	protected:
		virtual ~basic_connection_dispatcher(){}

	public:
		void register_connected_handler(connected_handle_type h)
		{
			on_connected_ = h;
		}
		void ununregister_connected_handler()
		{
			on_connected_.clear();
		}

		void register_disconnected_handler(disconnected_handle_type h)
		{
			on_disconnected_ = h;
		}
		void ununregister_disconnected_handler()
		{
			on_disconnected_.clear();
		}

		void register_writable_handler(writable_handle_type h)
		{
			on_writable_ = h;
		}
		void unregister_writable_handler()
		{
			on_writable_.clear();
		}

		virtual void unregister_all_dispatch_handler()
		{
			on_connected_.clear();
			on_disconnected_.clear();
			on_writable_.clear();
			basic_message_dispatcher<MesssageExtraction>::unregister_all_dispatch_handler();
		}

		void dispatch_disconnected(const error_code& ec)
		{
			if (ec)
			{
				disconnected_handle_type signal;
				signal.swap(on_disconnected_);
				unregister_all_dispatch_handler();
				if (signal)
					signal(ec);
			}
			else
			{
				if (on_disconnected_)
					on_disconnected_(ec);
			}
		}
		void dispatch_connected(const error_code& ec)
		{
			if (ec)
			{
				connected_handle_type signal;
				signal.swap(on_connected_);
				unregister_all_dispatch_handler();
				if (signal)
					signal(ec);
			}
			else
			{
				if (on_connected_)
					on_connected_(ec);
			}
		}

		void dispatch_writable()
		{
			dispatch_sendout();
		}
		void dispatch_sendout()
		{
			if (on_writable_)
				on_writable_();
		}
	public:
		disconnected_handle_type on_disconnected_;
		connected_handle_type    on_connected_;
		writable_handle_type     on_writable_;
	};

	template< >
	class basic_connection_dispatcher<void_message_extractor>
	{
		typedef basic_connection_dispatcher<void_message_extractor> this_type;
		SHARED_ACCESS_DECLARE;

		basic_connection_dispatcher_typedef(void_message_extractor, );

	protected:
		virtual ~basic_connection_dispatcher(){}

	public:
		void register_message_handler(received_handle_type h)
		{
			msg_handler_ = h;
		}
		void unregister_message_handler()
		{
			msg_handler_.clear();
		}

		static void register_global_message_handler(received_handle_type h)
		{
			s_msg_handler_ = h;
		}
		static void unregister_global_message_handler()
		{
			s_msg_handler_.clear();
		}

		void register_connected_handler(connected_handle_type h)
		{
			on_connected_ = h;
		}
		void unregister_connected_handler()
		{
			on_connected_.clear();
		}

		void register_disconnected_handler(disconnected_handle_type h)
		{
			on_disconnected_ = h;
		}
		void unregister_disconnected_handler()
		{
			on_disconnected_.clear();
		}

		void register_writable_handler(writable_handle_type h)
		{
			on_writable_ = h;
		}
		void unregister_writable_handler()
		{
			on_writable_.clear();
		}

		bool extract_and_dispatch_message(const safe_buffer& buf)
		{
			return dispatch_message(buf);
		}


		void unregister_all_dispatch_handler()
		{
			msg_handler_.clear();
			on_connected_.clear();
			on_disconnected_.clear();
			on_writable_.clear();
		}
		void dispatch_disconnected(const error_code& ec)
		{
			if (ec)
			{
				disconnected_handle_type signal;
				signal.swap(on_disconnected_);
				unregister_all_dispatch_handler();
				if (signal)
					signal(ec);
			}
			else
			{
				if (on_disconnected_)
					on_disconnected_(ec);
			}
		}
		void dispatch_connected(const error_code& ec)
		{
			if (ec)
			{
				connected_handle_type signal;
				signal.swap(on_connected_);
				unregister_all_dispatch_handler();
				if (signal)
					signal(ec);
			}
			else
			{
				if (on_connected_)
					on_connected_(ec);
			}
		}

		void dispatch_writable()
		{
			dispatch_sendout();
		}
		void dispatch_sendout()
		{
			if (on_writable_)
				on_writable_();
		}
		bool dispatch_message(const safe_buffer& buf)
		{
			//1. search <message_soceket, net_event_handler_type> bind in this socket
			if (!msg_handler_.empty())
			{
				msg_handler_(buf);
				return true;
			}

			//2. search <message_soceket, net_event_handler_type> bind in all socket
			if (!s_msg_handler_.empty())
			{
				s_msg_handler_(buf);
				return true;
			}

			//3. not find, alert error
			BOOST_ASSERT(0 && "can't find message dispatch_packet slot");
			LOG(
				LogError("can't find message dispath slot");
			);
			return false;
		}

	public:
		disconnected_handle_type on_disconnected_;
		connected_handle_type on_connected_;
		writable_handle_type on_writable_;
		received_handle_type msg_handler_;
		static received_handle_type s_msg_handler_;
	};

	//////////////////////////////////////////////////////////////////////////

#define  basic_acceptor_dispatcher_typedef(MessageSocket, typename)\
public:\
	typedef MessageSocket socket_type; \
	typedef basic_acceptor_dispatcher<socket_type> dispatcher_type; \
	typedef typename boost::shared_ptr<socket_type> connection_sptr; \
	typedef boost::function<void(connection_sptr, const error_code&)> accepted_handle_type; \

	template<typename MessageSocket>
	class basic_acceptor_dispatcher
	{
		typedef basic_acceptor_dispatcher<MessageSocket> this_type;

		SHARED_ACCESS_DECLARE;

		basic_acceptor_dispatcher_typedef(MessageSocket, typename);

	protected:
		virtual ~basic_acceptor_dispatcher(){}

	public:
		void register_accepted_handler(accepted_handle_type h)
		{
			accept_handler_ = h;
		}
		void unregister_accepted_handler()
		{
			accept_handler_.clear();
		}
		void unregister_all_dispatch_handler()
		{
			accept_handler_.clear();
		}

		void dispatch_accepted(connection_sptr sock, const error_code& ec)
		{
			if (accept_handler_)
				accept_handler_(sock, ec);
		}

	protected:
		accepted_handle_type accept_handler_;
	};
}

#endif

