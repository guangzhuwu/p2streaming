//
// basic_shared_tcp_layer.hpp
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

#ifndef BASIC_RDP_SHARED_TCP_LAYER_H__
#define BASIC_RDP_SHARED_TCP_LAYER_H__

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "p2engine/config.hpp"
#include "p2engine/handler_allocator.hpp"
#include "p2engine/local_id_allocator.hpp"
#include "p2engine/safe_buffer.hpp"
#include "p2engine/keeper.hpp"
#include "p2engine/speed_meter.hpp"
#include "p2engine/spinlock.hpp"

#include "p2engine/rdp/rdp_fwd.hpp"

namespace p2engine { namespace trdp{

	class trdp_flow;

	class basic_shared_tcp_layer 
		: public basic_engine_object
		, public basic_shared_layer_adaptor
	{
		typedef basic_shared_tcp_layer this_type;
		SHARED_ACCESS_DECLARE;
	public:
		typedef trdp_flow flow_impl_type;
		typedef boost::shared_ptr<flow_impl_type> flow_impl_sptr;

	protected:
		typedef std::map<std::string, acceptor_type*> acceptor_container;
		typedef std::map<endpoint, this_type*>   this_type_container;

	protected:
		typedef asio::ip::tcp::acceptor tcp_acceptor_type;
		typedef asio::ip::tcp::socket tcp_socket_type;
		enum{INIT, STARTED, STOPED};

	public:
		static shared_ptr create(io_service& ios, 
			const endpoint& local_edp, 
			error_code& ec, 
			bool realTimeUsage);

		static bool is_shared_endpoint_type(const endpoint& edp);

	public:
	
		virtual ~basic_shared_tcp_layer();

		virtual bool is_open()const
		{
			return tcp_acceptor_.is_open();
		}

		virtual endpoint local_endpoint(error_code&ec)const
		{
			return tcp_acceptor_.local_endpoint(ec);
		}

		acceptor_type* find_acceptor(const std::string& domainName);

		bool is_real_time_usage()const
		{
			return b_real_time_usage_;
		}

	protected:
		basic_shared_tcp_layer(io_service& ios, 
			const endpoint& local_edp, 
			error_code& ec, 
			bool realTimeUsage);

		void start();

		void cancel_without_protector();

		void handle_accept(const error_code& ec, flow_impl_sptr flow);

		void async_accept();

	public:
		virtual void register_flow(boost::shared_ptr<basic_flow_adaptor> flow, error_code& ec);
		virtual void unregister_flow(uint32_t flow_id, const basic_flow_adaptor* flow);
		virtual void register_acceptor(boost::shared_ptr<basic_acceptor_adaptor> acc, error_code& ec);
		virtual void unregister_acceptor(const basic_acceptor_adaptor* acptor);

	public:
		static double out_bytes_per_second()
		{
			return s_out_speed_meter_.bytes_per_second();
		}
		static double in_bytes_per_second()
		{
			return s_in_speed_meter_.bytes_per_second();
		}

	protected:
		tcp_socket_type socket_;
		tcp_acceptor_type tcp_acceptor_;
		endpoint local_endpoint_;
		acceptor_container	  acceptors_;
		int                   state_;
		timed_keeper_set<flow_sptr> flow_keeper_;
		bool b_real_time_usage_;

		static this_type_container s_this_type_pool_;
		static spinlock s_this_type_pool_mutex_;
		static rough_speed_meter s_out_speed_meter_;//(millisec(3000));
		static rough_speed_meter s_in_speed_meter_;//(millisec(3000));
	};

}//namespace trdp
}// namespace p2engine

#endif//
