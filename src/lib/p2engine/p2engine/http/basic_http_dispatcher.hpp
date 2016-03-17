//
// basic_http_dispatcher.hpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~~
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
#ifndef BASIC_HTTP_DISPATCHER_HPP
#define BASIC_HTTP_DISPATCHER_HPP

#include "p2engine/basic_dispatcher.hpp"
//#include "p2engine/fssignal.hpp"
#include "p2engine/safe_buffer.hpp"
#include "p2engine/logging.hpp"

namespace p2engine { namespace http {

	class request;
	class response;

	class http_connection_dispatcher
		:private basic_connection_dispatcher<void_message_extractor>
	{
		typedef http_connection_dispatcher this_type;
		typedef http_connection_dispatcher dispatcher_type;
		typedef basic_connection_dispatcher<void_message_extractor> basic_dispatcher;

		typedef boost::function<void(const request&)> received_request_handler_type;
		typedef boost::function<void(const response&)> received_response_handler_type;

		SHARED_ACCESS_DECLARE;

	protected:
		virtual ~http_connection_dispatcher(){}

	public:
		void register_request_handler(received_request_handler_type h)
		{
			request_handler_ = h;
		}
		void unregister_request_handler()
		{
			request_handler_.clear();
		}

		void register_response_handler(received_response_handler_type h)
		{
			response_handler_ = h;
		}
		void unregister_response_handler()
		{
			response_handler_.clear();
		}

		static void register_global_request_handler(received_request_handler_type h)
		{
			s_request_handler_ = h;
		}
		static void unregister_global_request_handler()
		{
			s_request_handler_.clear();
		}

		static void register_global_response_handler(received_response_handler_type h)
		{
			s_response_handler_ = h;
		}
		static void unregister_global_response_handler()
		{
			s_response_handler_.clear();
		}

		void register_data_handler(received_handle_type h)
		{
			this->register_message_handler(h);
		}
		void unregister_data_handler()
		{
			this->unregister_message_handler();
		}

		static void register_global_data_handler(received_handle_type h)
		{
			basic_dispatcher::register_global_message_handler(h);
		}
		static void unregister_global_data_handler()
		{
			basic_dispatcher::unregister_global_message_handler();
		}

		using basic_dispatcher::register_connected_handler;
		using basic_dispatcher::register_disconnected_handler;
		using basic_dispatcher::register_writable_handler;
		using basic_dispatcher::unregister_connected_handler;
		using basic_dispatcher::unregister_disconnected_handler;
		using basic_dispatcher::unregister_writable_handler;
		
		virtual void unregister_all_dispatch_handler()
		{
			basic_dispatcher::unregister_all_dispatch_handler();

			request_handler_.clear();
			response_handler_.clear();
		}

		using basic_dispatcher::dispatch_connected;
		using basic_dispatcher::dispatch_disconnected;
		using basic_dispatcher::dispatch_message;
		using basic_dispatcher::dispatch_sendout;

		bool dispatch_request(request& buf);
		bool dispatch_response(response& buf);

	public:
		received_request_handler_type request_handler_;
		received_response_handler_type response_handler_;

		static received_request_handler_type s_request_handler_;
		static received_response_handler_type s_response_handler_;
	};

	template<typename HttpSocket>
	class basic_http_acceptor_dispatcher
		:public basic_acceptor_dispatcher<HttpSocket>
	{
	};

}
}

#endif//BASIC_HTTP_DISPATCHER_HPP
