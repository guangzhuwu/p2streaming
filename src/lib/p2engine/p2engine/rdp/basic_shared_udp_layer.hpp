//
// basic_shared_udp_layer.hpp
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

#ifndef BASIC_RUDP_UDP_LAYER_H__
#define BASIC_RUDP_UDP_LAYER_H__

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "p2engine/push_warning_option.hpp"
#include "p2engine/config.hpp"
#include <queue>
#include <vector>
#include <list>
#include <map>
#include <boost/asio/ip/udp.hpp>
#include "p2engine/pop_warning_option.hpp"

#include "p2engine/handler_allocator.hpp"
#include "p2engine/basic_engine_object.hpp"
#include "p2engine/socket_utility.hpp"
#include "p2engine/safe_buffer.hpp"
#include "p2engine/logging.hpp"
#include "p2engine/local_id_allocator.hpp"
#include "p2engine/keeper.hpp"
#include "p2engine/speed_meter.hpp"
#include "p2engine/trafic_statistics.hpp"
#include "p2engine/spinlock.hpp"
#include "p2engine/rdp/const_define.hpp"
#include "p2engine/rdp/rdp_fwd.hpp"

#define RUDP_SCRAMBLE

namespace p2engine {
	namespace urdp{

		struct udp_acceptor_token;

		class basic_shared_udp_layer
			: public basic_engine_object
			, public basic_shared_layer_adaptor
		{
			typedef basic_shared_udp_layer this_type;
			SHARED_ACCESS_DECLARE;

			enum{ kBufferSize = MTU_SIZE + 128 };
		public:
			typedef asio::ip::udp::endpoint endpoint;
			typedef asio::ip::udp::socket	udp_socket_type;

			typedef shared_ptr shared_layer_impl_sptr;

			typedef std::vector < boost::weak_ptr<basic_flow_adaptor>, allocator<basic_flow_adaptor*> >
				flow_container;

			typedef std::map < std::string, boost::weak_ptr<basic_acceptor_adaptor>,
				std::less<std::string>,
				allocator<std::pair<const std::string, boost::weak_ptr<basic_acceptor_adaptor> > >
			> acceptor_container;

			typedef std::map < endpoint, boost::weak_ptr<this_type>, std::less<endpoint>,
				allocator<std::pair<const endpoint, boost::weak_ptr<this_type> > >
			> this_type_container;

			typedef basic_local_id_allocator<uint32_t> local_id_allocator;

		public:
			enum{ INIT, STARTED, STOPED };

			static shared_ptr create(io_service& ios, const endpoint& local_edp, error_code& ec);

			static bool is_shared_endpoint(const endpoint& edp);

		public:
			basic_shared_udp_layer(io_service& ios, const endpoint& local_edp, error_code& ec);
			virtual ~basic_shared_udp_layer();

			udp_socket_type& socket()
			{
				return socket_;
			}

			virtual bool is_open()const
			{
				return socket_.is_open();
			}

			variant_endpoint local_endpoint(error_code&ec)const
			{
				return socket_.local_endpoint(ec);
			}

			int flow_count()const
			{
				return flows_cnt_;
			}

			void start();

			void cancel()
			{
				OBJ_PROTECTOR(protector);
				close_without_protector();
			}

			void close_without_protector()
			{
				error_code ec;
				socket_.close(ec);
				state_ = STOPED;
			}

			void handle_receive(const error_code& ec, size_t bytes_transferred);
			void async_receive();

			virtual void register_flow(boost::shared_ptr<basic_flow_adaptor> flow, error_code& ec);
			virtual void unregister_flow(uint32_t flow_id, const basic_flow_adaptor* flow);
			virtual void register_acceptor(boost::shared_ptr<basic_acceptor_adaptor> acc, error_code& ec);
			virtual void unregister_acceptor(const basic_acceptor_adaptor* acptor);

		public:
			static double out_bytes_per_second()
			{
				return global_local_to_remote_speed_meter().bytes_per_second();
			}
			static double in_bytes_per_second()
			{
				return global_remote_to_local_speed_meter().bytes_per_second();
			}

			size_t async_send_to(const safe_buffer& safebuffer, const endpoint& ep, error_code& ec);

			template <typename ConstBuffers>
			size_t async_send_to(const ConstBuffers& bufs, const endpoint& ep, error_code& ec);

		protected:
			void __release_flow_id(int id);

			void do_handle_received(const safe_buffer& buffer);
			void do_handle_received_urdp_msg(safe_buffer buffer);

		protected:
			struct request_uuid{
				endpoint remoteEndpoint;
				uint32_t remotePeerID;
				uint32_t session;
				uint32_t flow_id;
				bool operator <(const request_uuid& rhs)const
				{
					if (session != rhs.session)
						return session < rhs.session;
					if (remotePeerID != rhs.remotePeerID)
						return remotePeerID < rhs.remotePeerID;
					return remoteEndpoint < rhs.remoteEndpoint;
				}
			};

			int state_;
			udp_socket_type socket_;
			endpoint local_endpoint_;
			safe_buffer recv_buffer_;
			endpoint sender_endpoint_;
			timed_keeper_set<request_uuid> request_uuid_keeper_;
			local_id_allocator id_allocator_;
			std::list<int, allocator<int> > released_id_catch_;
			timed_keeper_set<int> released_id_keeper_;
			timed_keeper_set<endpoint> unreachable_endpoint_keeper_;
			acceptor_container	  acceptors_;
			flow_container        flows_;
			int                   flows_cnt_;
			fast_mutex flow_mutex_;
			fast_mutex acceptor_mutex_;
			int	continuous_recv_cnt_;

			typedef handler_allocator_wrap<
				boost::function<void(const error_code&, size_t)>
			>::wrap_type allocator_wrap_handler;

			boost::scoped_ptr<allocator_wrap_handler> recv_handler_;

			static this_type_container s_shared_this_type_pool_;
			static spinlock s_shared_this_type_pool_mutex_;
			static allocator_wrap_handler s_dummy_callback;

#ifdef RUDP_SCRAMBLE
			safe_buffer zero_8_bytes_;
#endif
		};

		template <typename ConstBuffers>
		inline size_t basic_shared_udp_layer::async_send_to(const ConstBuffers& bufs,
			const endpoint& ep, error_code& ec)
		{
#ifdef RUDP_SCRAMBLE
			size_t len = 0;
			std::vector<asio::const_buffer, allocator<asio::const_buffer> >sndbufs;
			sndbufs.reserve(bufs.size() + 1);
			sndbufs.push_back(zero_8_bytes_.to_asio_const_buffer());
			to_asio_mutable_buffers(bufs, sndbufs);
			BOOST_ASSERT(sndbufs.size() == bufs.size() + 1);
			BOOST_FOREACH(const asio::const_buffer& buf, sndbufs)
			{
				len += p2engine::buffer_size(buf);
			}
			if (len == zero_8_bytes_.size())
				return 0;
			global_local_to_remote_speed_meter() += len;
			BOOST_ASSERT(socket_.is_open());
			if (socket_.send_to(sndbufs, ep, 0, ec) == 0 && ec)
			{
				ec.clear();
				socket_.async_send_to(sndbufs, ep, s_dummy_callback);
			}
			return len - zero_8_bytes_.size();
#else
			size_t len = 0;
			std::vector<asio::const_buffer, allocator<asio::const_buffer> >sndbufs;
			sndbufs.reserve(bufs.size());
			to_asio_mutable_buffers(bufs, sndbufs);
			BOOST_ASSERT(sndbufs.size() == bufs.size());
			BOOST_FOREACH(const asio::const_buffer& buf, sndbufs)
			{
				len += p2engine::buffer_size(buf);
			}
			if (len == 0)
				return 0;
			global_local_to_remote_speed_meter() += len;
			if (socket_.send_to(sndbufs, ep, 0, ec) == 0 && ec)
			{
				ec.clear();
				socket_.async_send_to(sndbufs, ep, s_dummy_callback);
			}
			return len;
#endif
		}

	}
}// namespace p2engine

#endif//BASIC_RUDP_UDP_LAYER_H__
