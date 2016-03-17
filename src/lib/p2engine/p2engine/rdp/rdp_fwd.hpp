//
// tcp_rdp_fwd.hpp
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

#ifndef rdp_fwd_h__
#define rdp_fwd_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "p2engine/basic_dispatcher.hpp"
#include "p2engine/connection.hpp"
#include "p2engine/safe_buffer.hpp"

namespace p2engine{

	class basic_shared_layer_adaptor;
	class basic_flow_adaptor;
	class basic_connection_adaptor;
	class basic_acceptor_adaptor;

	struct adaptor_typedef
	{
		typedef basic_flow_adaptor			flow_type;
		typedef basic_connection_adaptor	connection_type;
		typedef basic_acceptor_adaptor		acceptor_type;
		typedef basic_shared_layer_adaptor	shared_layer_type;

		typedef boost::shared_ptr<flow_type>			flow_sptr;
		typedef boost::shared_ptr<connection_type>		connection_sptr;
		typedef boost::shared_ptr<acceptor_type>		acceptor_sptr;
		typedef boost::shared_ptr<shared_layer_type>	shared_layer_sptr;
	};

	class basic_shared_layer_adaptor
		:public adaptor_typedef
	{
		typedef basic_shared_layer_adaptor this_type;
		SHARED_ACCESS_DECLARE;

	public:
		virtual void register_flow(boost::shared_ptr<basic_flow_adaptor> flow, error_code& ec) = 0;
		virtual void unregister_flow(uint32_t flow_id, const basic_flow_adaptor* flow) = 0;
		virtual void register_acceptor(boost::shared_ptr<basic_acceptor_adaptor> acc, error_code& ec) = 0;
		virtual void unregister_acceptor(const basic_acceptor_adaptor* acptor) = 0;
	};

	class basic_flow_adaptor
		:public adaptor_typedef
	{
		typedef basic_flow_adaptor this_type;
		SHARED_ACCESS_DECLARE;

		//friend  flow_type; 
		friend  class basic_connection_adaptor;
		friend  class basic_acceptor_adaptor;

	protected:
		basic_flow_adaptor(){}
		virtual ~basic_flow_adaptor(){}

	public:
		//this will be called by acceptor when a passive flow is established
		virtual bool is_connected()const = 0;
		virtual void set_socket(connection_sptr sock) = 0;
		virtual uint32_t flow_id()const = 0;
		virtual void set_flow_id(uint32_t id) = 0;
		virtual endpoint remote_endpoint(error_code& ec)const = 0;
		virtual void close(bool graceful) = 0;

		virtual void on_received(const safe_buffer& data, const endpoint& from) = 0;
	};

	class basic_connection_adaptor
		:public adaptor_typedef
	{
		typedef basic_connection_adaptor this_type;
		SHARED_ACCESS_DECLARE;

		friend class basic_flow_adaptor;
		friend class basic_acceptor_adaptor;

	protected:
		basic_connection_adaptor(){}
		virtual ~basic_connection_adaptor(){}

	public:
		//these will be called by flow
		virtual void on_connected(const error_code&) = 0;
		virtual void on_disconnected(const error_code&) = 0;
		virtual void on_writable() = 0;
		virtual void on_received(const safe_buffer&) = 0;
		virtual void set_flow(flow_sptr sock) = 0;
	};

	class basic_acceptor_adaptor
		:public adaptor_typedef
	{
		typedef basic_acceptor_adaptor this_type;
		SHARED_ACCESS_DECLARE;

		friend class basic_flow_adaptor;
		friend class basic_connection_adaptor;

	protected:
		basic_acceptor_adaptor(){}
		virtual ~basic_acceptor_adaptor(){}

	public:
		//this will be called by flow when a passive flow is established
		virtual void accept_flow(flow_sptr flow) = 0;
		virtual const std::string& get_domain()const = 0;

		virtual int on_request(const endpoint&) = 0;
	};

}

#endif // basic_urdp_fwd_h__
