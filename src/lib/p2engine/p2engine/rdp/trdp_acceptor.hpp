//
// trdp_acceptor.hpp
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

#ifndef tcp_rdp_acceptor_h__
#define tcp_rdp_acceptor_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "p2engine/push_warning_option.hpp"
#include "p2engine/config.hpp"
#include <list>
#include <string>
#include <boost/noncopyable.hpp>
#include "p2engine/pop_warning_option.hpp"

#include "p2engine/typedef.hpp"
#include "p2engine/shared_access.hpp"
#include "p2engine/timer.hpp"
#include "p2engine/safe_buffer.hpp"
#include "p2engine/wrappable_integer.hpp"
#include "p2engine/handler_allocator.hpp"

#include "p2engine/rdp/rdp_fwd.hpp"
#include "p2engine/rdp/const_define.hpp"

namespace p2engine{ namespace trdp{

	template<typename ConnectionType, typename ConnectionBaseType>
	class basic_trdp_acceptor 
		: public basic_acceptor_adaptor
		, public basic_acceptor<ConnectionBaseType>
	{
		typedef basic_trdp_acceptor<ConnectionType, ConnectionBaseType> this_type;
		SHARED_ACCESS_DECLARE;

	public:
		typedef this_type acceptor_type;
		typedef ConnectionBaseType connection_base_type;
		typedef ConnectionType connection_impl_type;
		typedef typename connection_impl_type::flow_impl_type flow_impl_type;
		typedef typename connection_impl_type::shared_layer_impl_type shared_layer_impl_type;

		typedef rough_timer timer_type;
		typedef boost::shared_ptr<timer_type> timer_sptr;

		typedef boost::shared_ptr<shared_layer_impl_type> shared_layer_impl_sptr;
		typedef boost::shared_ptr<flow_impl_type> flow_impl_sptr;
		typedef boost::shared_ptr<connection_impl_type> connection_impl_sptr;

		enum state{CLOSED, LISTENING};

	public:
		//if this is used in a utility that low delay is important(
		//such as p2p live streaming or VOIP), please set "realtimeUtility"
		//be true, otherwise, please set "realtimeUtility" be false.
		static shared_ptr create(io_service& ios, bool realTimeUtility)
		{
			return shared_ptr(new this_type(ios, realTimeUtility), 
				shared_access_destroy<this_type>()
				);
		}

	protected:
		basic_trdp_acceptor(io_service& ios, bool realTimeUtility)
			: basic_acceptor<ConnectionBaseType>(ios, realTimeUtility)
			, b_keep_accepting_(true)
			, state_(CLOSED)
		{
			this->set_obj_desc("trdp_acceptor");
		}

		virtual ~basic_trdp_acceptor()
		{
			error_code ec;
			__close(ec);
		}

	public:
		//listen on a local_edp to accept connections of domainName
		//listen(localEdp, "bittorrent/p2p", ec)
		virtual error_code listen(const endpoint& local_edp, const std::string& domainName, 
			error_code& ec)
		{
			if (state_==LISTENING)
				ec=asio::error::already_started;
			else
			{
				ec.clear();
				BOOST_ASSERT(!shared_layer_);
				this->domain_ = domainName;
				endpoint localEndpoint = local_edp;
				shared_layer_=flow_impl_type::create_shared_layer_for_acceptor(
					this->get_io_service(), localEndpoint, 
					boost::static_pointer_cast<flow_impl_type::acceptor_type>(SHARED_OBJ_FROM_THIS),
					this->b_real_time_usage_, ec);
				if (ec)
				{
					local_endpoint_ = endpoint();
					shared_layer_.reset();
					this->domain_=INVALID_DOMAIN;
				}
				else
				{
					local_endpoint_ = localEndpoint;
					this->domain_=domainName;
					state_=LISTENING;
				}
			}
			return ec;
		}

		virtual error_code listen(const endpoint& local_edp, error_code& ec)
		{
			listen(local_edp, DEFAULT_DOMAIN, ec);
			return ec;
		}

		virtual void keep_async_accepting()
		{
			if (!b_keep_accepting_)
			{
				b_keep_accepting_ = true;
				this->get_io_service().post(
					make_alloc_handler(boost::bind(&this_type::do_async_accept, SHARED_OBJ_FROM_THIS))
					);
			}
		}

		virtual void block_async_accepting()
		{
			b_keep_accepting_=false;
		}

		virtual error_code close(error_code& ec)
		{
			return __close(ec);
		}

		virtual endpoint local_endpoint(error_code& ec) const
		{
			if (shared_layer_)
			{
				return local_endpoint_;
			}
			else 
			{
				ec=asio::error::not_socket;
				return endpoint();
			}
		}

		const std::string& get_domain() const
		{
			return this->domain();
		}
	protected:
		virtual void accept_flow(flow_sptr flow)
		{
			pending_sockets_.push(flow);
			if (pending_sockets_.size()<8)
			{
				do_async_accept();
			}
			else
			{
				if (pending_sockets_.front())
					pending_sockets_.front()->close(false);
				pending_sockets_.pop();
			}
		}
		virtual int on_request(const endpoint&)
		{
			BOOST_ASSERT(0);
			return -1;
		}

	protected:
		void do_async_accept()
		{
			if (state_==CLOSED||!b_keep_accepting_)
				return;

			if (!shared_layer_)
			{
				__on_accepted(asio::error::bad_descriptor);
			}
			else if(!pending_sockets_.empty())
			{
				__on_accepted(error_code());
			}
		}

		void __on_accepted(const error_code&ec)
		{
			if (!b_keep_accepting_||state_==CLOSED)
				return;

			if (ec)
			{
				connection_impl_sptr sock=pop_first_pending_socket();
				for(;sock;sock=pop_first_pending_socket())
					sock->close();
				this->dispatch_accepted(connection_impl_sptr(), ec);
			}
			else
			{
				connection_impl_sptr sock = pop_first_pending_socket();
				if (sock->is_open())
				{
					sock->keep_async_receiving();
					this->dispatch_accepted(sock, ec);
				}
				if (!pending_sockets_.empty())
					do_async_accept();
			}
		}

		error_code __close(error_code& ec)
		{
			if (state_==LISTENING)
			{
				ec.clear();
				if (shared_layer_)
				{
					shared_layer_->unregister_acceptor(this);
					shared_layer_.reset();
					this->domain_ = INVALID_DOMAIN;
				}
				connection_impl_sptr sock=pop_first_pending_socket();
				for(;sock;sock=pop_first_pending_socket())
					sock->close();
				state_=CLOSED;
			}
			else
			{
				BOOST_ASSERT(!shared_layer_);
				BOOST_ASSERT(this->domain_==INVALID_DOMAIN);
				ec=asio::error::not_socket;
			}
			return ec;
		}
	protected:
		connection_impl_sptr pop_first_pending_socket()
		{
			if (pending_sockets_.empty())
				return connection_impl_sptr();
			flow_sptr flow=pending_sockets_.front();
			pending_sockets_.pop();
			connection_impl_sptr sock =
				connection_impl_type::create(this->get_io_service(), this->is_real_time_usage(), true);
			sock->set_flow(flow);
			flow->set_socket(sock);
			return sock;
		}

	protected:
		state state_;
		endpoint local_endpoint_;
		shared_layer_sptr shared_layer_;
		std::queue<flow_sptr>pending_sockets_;
		bool b_keep_accepting_;
	};

}
}//p2engine

#endif//tcp_rdp_acceptor_h__
