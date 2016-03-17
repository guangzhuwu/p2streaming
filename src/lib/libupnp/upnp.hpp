/*

Copyright (c) 2007, Arvid Norberg
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

* Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in
the documentation and/or other materials provided with the distribution.
* Neither the name of the author nor the names of its
contributors may be used to endorse or promote products derived
from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/

#ifndef P2ENGINE_UPNP_HPP
#define P2ENGINE_UPNP_HPP

#include "libupnp/error_code.hpp"
#include "libupnp/http_parser.hpp"
#include "libupnp/device_xml_fetcher.h"

#include "p2engine/broadcast_socket.hpp"
#include "p2engine/intrusive_ptr_base.hpp"

#include "p2engine/push_warning_option.hpp"
#include <boost/function/function1.hpp>
#include <boost/function/function3.hpp>
#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>

#include <set>
#if defined(P2ENGINE_UPNP_LOGGING)
#include <fstream>
#endif
#include "p2engine/pop_warning_option.hpp"


namespace libupnp
{
	class device_xml_fetcher;
	namespace upnp_errors
	{
		enum error_code_enum
		{
			no_error = 0, 
			invalid_argument = 402, 
			action_failed = 501, 
			value_not_in_array = 714, 
			source_ip_cannot_be_wildcarded = 715, 
			external_port_cannot_be_wildcarded = 716, 
			port_mapping_conflict = 718, 
			internal_port_must_match_external = 724, 
			only_permanent_leases_supported = 725, 
			remote_host_must_be_wildcard = 726, 
			external_port_must_be_wildcard = 727
		};
	}

#if BOOST_VERSION < 103500
	extern asio::error::error_category upnp_category;
#else

	struct upnp_error_category : boost::system::error_category
	{
		virtual const char* name() const;
		virtual std::string message(int ev) const;
		virtual boost::system::error_condition default_error_condition(int ev) const
		{ return boost::system::error_condition(ev, *this); }
	};

	extern upnp_error_category upnp_category;
#endif

	// int: port-mapping index
	// address: external address as queried from router
	// int: external port
	// std::string: error message
	// an empty string as error means success
	// a port-mapping index of -1 means it's
	// an informational log message
	
	class  upnp 
		: public p2engine::basic_intrusive_ptr<upnp>
	{
		
		typedef p2engine::io_service io_service;
		typedef p2engine::address    address;
		typedef p2engine::error_code error_code;
		typedef p2engine::broadcast_socket broadcast_socket;
		typedef p2engine::rough_timer timer;
		typedef p2engine::deadlock_assert_mutex<p2engine::null_mutex>  mutex;

		typedef boost::function<void(int, address, int, error_code const&)> portmap_callback_t;
		typedef boost::function<void(char const*)> log_callback_t;
		typedef boost::function<void(bool _has_service)> discover_callback_t;
		typedef boost::function<void(mutex::scoped_lock&)> queue_function_type;

		struct global_mapping_t
		{
			global_mapping_t()
				: protocol(none)
				, external_port(0)
				, local_port(0)
			{}
			int protocol;
			int external_port;
			int local_port;
		};

		struct mapping_t
		{
			enum action_t { action_none, action_add, action_delete };
			mapping_t()
				: action(action_none)
				, local_port(0)
				, external_port(0)
				, protocol(none)
				, failcount(0)
			{}

			ptime expires;
			int action;
			int local_port;
			int external_port;

			// 2 = udp, 1 = tcp
			int protocol;
			int failcount;
		};

		struct rootdevice
		{
			rootdevice(): service_namespace(0)
				, port(0)
				, lease_duration(default_lease_time)
				, supports_specific_external(true)
				, disabled(false)
			{}

			void close() const
			{
				if (!upnp_fetcher) return;
				upnp_fetcher->close();
				upnp_fetcher.reset();
			}

			bool operator<(rootdevice const& rhs) const { return url < rhs.url; }

			std::string url;
			std::string control_url;
			char const* service_namespace;
			std::vector<mapping_t> mapping;

			std::string hostname;
			int port;
			std::string path;
			address external_ip;

			int lease_duration;
			bool supports_specific_external;
			bool disabled;
			mutable boost::shared_ptr<device_xml_fetcher> upnp_fetcher;
		};

		struct upnp_state_t
		{
			std::vector<global_mapping_t> mappings;
			std::set<rootdevice> devices;
		};

	public:
		enum protocol_type { none = 0, udp = 1, tcp = 2 };
		enum { default_lease_time = 0 };

		upnp(io_service& ios
			, std::string const& user_agent
			, portmap_callback_t const& cb, log_callback_t const& lcb
			, bool ignore_nonrouters=true, void* state = 0);
		virtual ~upnp();

		int  add_mapping(protocol_type p, int external_port, int local_port, bool exist_check=true);
		void delete_mapping(const int& mapping);
		void delete_mapping(int external_port, protocol_type p);
		void delete_all_mapping();
		void discover_device(const discover_callback_t& handler);
		void get_status_info();
		void close();

	private:
		bool pop_queue();
		// there are routers that's don't support timed
		// port maps, without returning error 725. It seems
		// safer to always assume that we have to ask for
		// permanent leases
		void resend_request(error_code const& e);

		void get_ip_address(rootdevice& d);
		void delete_port_mapping(rootdevice& d, int i);
		void create_port_mapping(rootdevice& d, int i);
		void get_status(rootdevice& d);

		void on_upnp_device_response(p2engine::udp::endpoint const& from, char* buffer
			, std::size_t bytes_transferred);
		void on_upnp_xml(error_code const& e
			, libupnp::http_parser const& p, rootdevice& d
			, device_xml_fetcher& c);
		void on_upnp_get_ip_address_response(error_code const& e
			, libupnp::http_parser const& p, rootdevice& d
			, device_xml_fetcher& c);
		void on_upnp_map_response(error_code const& e
			, libupnp::http_parser const& p, rootdevice& d
			, int mapping, device_xml_fetcher& c);
		void on_upnp_unmap_response(error_code const& e
			, libupnp::http_parser const& p, rootdevice& d
			, int mapping, device_xml_fetcher& c);
		void on_status_response(error_code const& e, libupnp::http_parser const& p, 
			rootdevice& d, device_xml_fetcher& c);
		void on_expire(error_code const& e);

	private:
		void __add_mapping(protocol_type p, int external_port, int local_port, 
			bool exist_check, mutex::scoped_lock& l);
		void __delete_mapping(const int& mapping);
		void __do_delete_mapping(const int& mapping, mutex::scoped_lock& l);
		bool __pop_queue(mutex::scoped_lock& l);

		void __discover_device_impl(mutex::scoped_lock& l);

		bool __get_mapping(int mapping_index, int& local_port, int& external_port, 
			int& protocol) const;

		void __next(rootdevice& d, int i, mutex::scoped_lock& l);
		void __update_map(rootdevice& d, int i, mutex::scoped_lock& l);

		void __disable(error_code const& ec, mutex::scoped_lock& l);
		void __return_error(int mapping, int code, mutex::scoped_lock& l);
		void __log(char const* msg, mutex::scoped_lock& l);

		void __do_post(const rootdevice& d, char const* soap, char const* soap_action, 
			mutex::scoped_lock&l);

		int __num_mappings() const { return int(m_mappings.size()); }

	protected:
		std::vector<global_mapping_t> m_mappings;
		std::string const m_user_agent;
		std::set<rootdevice> m_devices;

		portmap_callback_t m_callback;
		log_callback_t m_log_callback;
		discover_callback_t discover_callback_;

		int m_retry_count;
		io_service& m_io_service;
		broadcast_socket detect_socket_;
		boost::shared_ptr<timer> m_broadcast_timer;
		boost::shared_ptr<timer> m_refresh_timer;
		boost::shared_ptr<timer> start_timer_;

		bool m_disabled;
		bool m_closing;
		bool m_ignore_non_routers;

		mutex m_mutex;
		std::string m_model;
		std::queue<queue_function_type>  queue_;
	};

}


#endif

