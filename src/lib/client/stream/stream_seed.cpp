#include "client/stream/stream_seed.h"
#include "client/stream/stream_topology.h"
#include "client/stream/stream_scheduling.h"
#include "client/client_service.h"

#if !defined(_DEBUG_SCOPE) && defined(NDEBUG)
#	define  SCHEDULING_DBG(x)
#else 
#	define  SCHEDULING_DBG(x)/* x*/
#endif

#define GUARD_TOPOLOGY(returnValue) \
	BOOST_AUTO(topology, scheduling_->get_topology());\
	if (!topology) {return returnValue;}

#define GUARD_CLIENT_SERVICE(returnValue)\
	GUARD_TOPOLOGY(returnValue)\
	BOOST_AUTO(clientService, topology->get_client_service());\
	if (!clientService) {return returnValue;}

#define GUARD_CLIENT_SERVICE_LOGIC(returnValue)\
	GUARD_CLIENT_SERVICE(returnValue)\
	BOOST_AUTO(svcLogic, clientService->get_client_service_logic());\
	if (!svcLogic) {return returnValue;}


NAMESPACE_BEGIN(p2client);

static timestamp_t connect_start_time;

stream_seed::stream_seed(stream_scheduling& scheduling)
	:basic_stream_scheduling(scheduling)
	, connect_fail_count_(0)
{
}
stream_seed::~stream_seed()
{
}
void stream_seed::stop()
{
	if(server_connection_tmp_)
		server_connection_tmp_->close();
	if(server_connection_)
		server_connection_->close();
	last_connect_server_time_.reset();
}

void stream_seed::start()
{
}

void stream_seed::reset()
{
	if(server_connection_tmp_)
		server_connection_tmp_->close();
	if(server_connection_)
		server_connection_->close();
	last_connect_server_time_.reset();
}

int stream_seed::on_timer(timestamp_t now)
{
	connect_server();
	return 1;
}

void stream_seed::connect_server()
{
	//TODO("改过来!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!")return;
	if ((server_connection_
		&&server_connection_->connection_category()==peer_connection::UDP
		&&server_connection_->is_connected()
		)
		||
		(server_connection_tmp_
		&&server_connection_tmp_->is_open()
		)
		)
	{
		return;
	}

	GUARD_CLIENT_SERVICE(;);
	timestamp_t now=timestamp_now();
	if(topology->neighbor_count()<(STREAM_NEIGHTBOR_PEER_CNT*4/5)
		||(!last_connect_server_time_.is_initialized())
		||is_time_passed(10000, *last_connect_server_time_, now)
		||(scheduling_->is_player_started()&&scheduling_->get_buffer_health()<0.5
		&&is_time_passed(2000, *last_connect_server_time_, now)
		)
		)
	{
		SCHEDULING_DBG(
			connect_start_time=timestamp_now();
		std::cout<<"----------------------connect_server--------------------------------:"
			<<connect_start_time<<"\n";
		if(server_connection_)
			std::cout<<"  ----------------: "<<server_connection_->is_connected()<<std::endl;
		);
		//随机选择一个server链接，负载均衡
		BOOST_AUTO(const&servers, clientService->server_peers());
		BOOST_AUTO(itr, random_select(servers.begin(), servers.size()));
		if (itr!=servers.end())
		{
			__connect_server((itr->second).get(), error_code(), coroutine());
		}
	}
}

void stream_seed::__connect_server(peer* srv_peer, error_code ec, coroutine coro)
{
	struct get_domain {
		std::string operator ()(const std::string& channel_id, peer* srv_peer)const
		{
			if (!srv_peer->playing_the_same_channel())
				return cache_server_demain+"/"+srv_peer->get_peer_info().peer_id();
			else
				return server_and_peer_demain+"/"+channel_id;
		}
	};

	BOOST_ASSERT(get_client_param_sptr()->b_streaming_using_rudp
		||get_client_param_sptr()->b_streaming_using_rtcp);
	GUARD_TOPOLOGY(;);
	CORO_REENTER(coro)
	{	
		last_connect_server_time_=timestamp_now();
		//UDP通信未确认为不可行，则优先使用UDP
		if (get_client_param_sptr()->b_streaming_using_rudp
			&&!srv_peer->is_udp_restricte()
			/*srv_peer->get_peer_info().nat_type()!=NAT_UDP_BLOCKED
			&&ovl->local_nat_type()!=NAT_UDP_BLOCKED
			*/)
		{
			server_connection_tmp_=urdp_peer_connection::create(get_io_service(), true);
			server_connection_tmp_->open(topology->urdp_acceptor_local_endpoint(), ec);
			if(server_connection_tmp_)
			{
				SCHEDULING_DBG(
					std::cout
					<<"__connect_server:"<<external_udp_endpoint(srv_peer->get_peer_info())
					<<", domain:"<<get_domain()(get_client_param_sptr()->channel_uuid, srv_peer)
					<<std::endl;
				);
				server_connection_tmp_->set_peer(srv_peer->shared_obj_from_this<peer>());
				CORO_YIELD(
					server_connection_tmp_->register_connected_handler(
					boost::bind(&this_type::__connect_server, this, srv_peer, _1, coro)
					);
				server_connection_tmp_->async_connect(
					external_udp_endpoint(srv_peer->get_peer_info()), 
					get_domain()(get_client_param_sptr()->channel_uuid, srv_peer), 
					seconds(6)
					);
				);
				if (!ec)
				{
					SCHEDULING_DBG(
						std::cout<<"**********************************urdp connect server("
						<<external_udp_endpoint(srv_peer->get_peer_info())<<") OK!"
						<<std::endl;
					);
					on_connected(server_connection_tmp_.get(), ec);
					return;
				}
				else
				{
					SCHEDULING_DBG(
						std::cout<<"**********************************urdp connect server("
						<<external_udp_endpoint(srv_peer->get_peer_info())<<") error:"
						<<ec.message()<<std::endl;
					);
				}
			}
		}
		else{
			SCHEDULING_DBG(
				GUARD_CLIENT_SERVICE(;);
			if(srv_peer->get_peer_info().nat_type()==NAT_UDP_BLOCKED)
				std::cout<<"connect server: srv.nat_type()==NAT_UDP_BLOCKED, so, try tcp\n";
			if(clientService->local_nat_type()==NAT_UDP_BLOCKED)
				std::cout<<"connect server: local_nat_type()==NAT_UDP_BLOCKED, so, try tcp\n";
			);
		}
		//UDP未能联通，尝试TCP
		if (server_connection_tmp_)
		{
			server_connection_tmp_->close();
			server_connection_tmp_.reset();
		}
		if(get_client_param_sptr()->b_streaming_using_rtcp)
		{
			server_connection_tmp_=trdp_peer_connection::create(get_io_service(), true);
			if(server_connection_tmp_)
			{
				SCHEDULING_DBG(
					std::cout<<"TCP CONNECT SERVER: tcp_endpoint="
					<<external_tcp_endpoint(srv_peer->get_peer_info())
					<<"\n";
				);
				server_connection_tmp_->set_peer(srv_peer->shared_obj_from_this<peer>());
				server_connection_tmp_->register_connected_handler(
					boost::bind(&this_type::on_connected, this, server_connection_tmp_.get(), _1)
					);
				server_connection_tmp_->async_connect(
					external_tcp_endpoint(srv_peer->get_peer_info()), 
					get_domain()(get_client_param_sptr()->channel_uuid, srv_peer), 
					seconds(5));
			}
		}
		else
		{
			get_io_service().post(make_alloc_handler( 
				boost::bind(&this_type::on_connected, SHARED_OBJ_FROM_THIS, 
				server_connection_tmp_.get(), asio::error::not_socket)
				));
		}
	}
}

//此处唯一接受对server_connection的处理
void stream_seed::on_connected(peer_connection* conn, const error_code& ec)
{
	BOOST_ASSERT(server_connection_tmp_.get()==conn);
	if (server_connection_tmp_.get()!=conn)
		return;
	if (!ec)
	{
		SCHEDULING_DBG(
			std::cout<<"----------------------connect_server ok-------------------------------- elapse:"
			<<time_minus((timestamp_t)timestamp_now(), connect_start_time)<<"\n";
			);

		connect_fail_count_ = 0;
		if(!server_connection_
			||server_connection_->connection_category()==peer_connection::TCP
			||!server_connection_->is_open()
			)
		{
			if (server_connection_)
			{
				server_connection_->close(true);
				server_connection_.reset();
			}
			std::swap(server_connection_, server_connection_tmp_);

			BOOST_ASSERT(server_connection_->is_connected());
			server_connection_->register_disconnected_handler(
				boost::bind(&this_type::on_disconnected, this, server_connection_.get(), _1)
				);

			scheduling_->register_message_handler(server_connection_.get());

			conn->keep_async_receiving();
			if (is_live())
			{
				conn->ping_interval(get_client_param_sptr()->server_seed_ping_interval);
			}
			else//VoD的server都是SIMPLE_DISTRBUTOR
			{
				conn->ping_interval(SIMPLE_DISTRBUTOR_PING_INTERVAL);
				join_channel_msg msg;
				msg.set_channel_id(get_client_param_sptr()->channel_uuid); 
				conn->async_send_reliable(serialize(msg), global_msg::join_channel);
			}

			SCHEDULING_DBG(
				peer_sptr peer = server_connection_->get_peer();
			std::cout<<"connected mds id:"
				<<string_to_hex(peer->get_peer_info().peer_id())
				<<"\n";
			SCHEDULING_DBG(
				std::cout<<"connected mds server\n";
			);
			);
		}
		else
		{
			server_connection_tmp_->close();
			server_connection_tmp_.reset();
		}
	}
	else
	{
		++connect_fail_count_;
		if(connect_fail_count_ >= 3)
		{
			scheduling_->require_update_server_info();
		}
		server_connection_tmp_.reset();
		SCHEDULING_DBG(
			std::cout<<"can't connect mds: "<<ec.message()<<std::endl;
		);
	}
}

void stream_seed::on_disconnected(peer_connection* conn, const error_code& ec)
{
	//GUARD_TOPOLOGY;
	//BOOST_ASSERT(server_connection_.get()==conn);//除server外的其他节点的网络事件是在topolog中处理的
	if(server_connection_.get()==conn)
	{
		SCHEDULING_DBG(
			std::cout<<"disconnected frome server: "<<ec.message()<<"\n";
		);
		server_connection_.reset();
	}
	else if(server_connection_&&!server_connection_->is_connected())
	{
		SCHEDULING_DBG(
			std::cout<<"server connection on_disconnected ignoraled\n";
		);
	}
}

NAMESPACE_END(p2client);


