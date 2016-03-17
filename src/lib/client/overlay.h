//
// overlay.h
// ~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2008 GuangZhu Wu (GuangZhuWu@gmail.com)
//
// All rights reserved. 
//
#ifndef peer_topology_base_h__
#define peer_topology_base_h__

#include "client/typedef.h"
#include "client/neighbor_map.h"

namespace p2client{

	class scheduling_base;

	class overlay_base
	{
	public:
		virtual ~overlay_base(){}

		virtual void start()=0;
		virtual void stop(bool flushMedia = false) = 0;

		virtual void try_shrink_neighbors(bool explicitShrink) = 0;
		virtual bool is_black_peer(const peer_id_t& id) = 0;
		virtual bool can_be_neighbor(peer_sptr p) = 0;
	};

	class overlay
		: public basic_engine_object
		, public basic_client_object
		, public overlay_base
		, public basic_mix_acceptor < overlay, peer_acceptor >
	{
		typedef overlay this_type;
		SHARED_ACCESS_DECLARE;

		friend class basic_mix_acceptor < overlay, peer_acceptor > ;
	protected:
		typedef boost::unordered_set<peer_id_t>	router_list;
		typedef boost::unordered_map<peer_id_t, router_list>	router_table;
		typedef rough_timer timer;

	public:
		typedef variant_endpoint endpoint;
		typedef boost::unordered_map<peer_id_t, peer_sptr> peer_map;

		enum topology_id_t{ STREAM_TOPOLOGY = 0, HUB_TOPOLOGY, CACHE_TOPOLOGY, __MAX_TOPOLOGY_CNT = 8 };

	protected:
		overlay(client_service_sptr ovl, int topologyID);
		virtual ~overlay();

		virtual void start();
		virtual void stop(bool flushMedia = false);

		virtual void try_shrink_neighbors(bool explicitShrink) = 0;
		virtual bool is_black_peer(const peer_id_t& id) = 0;
		virtual bool can_be_neighbor(peer_sptr p) = 0;

	public:
		void restart();
		void set_play_offset(int64_t offset);
		void print_neighbors();
		const neighbor_map& get_neighbor_connections()const{ return neighbors_conn_; }
		client_service_sptr get_client_service(){ return client_service_.lock(); }
		virtual void known_new_peer(const peer_id_t&, peer_sptr);
		void erase_offline_peer(const peer_id_t& id);
		void erase_low_capacity_peer(const peer_id_t& id);
		void erase_route(const peer_id_t& id, const peer_id_t& routeID);
		void add_route(const peer_id_t& id, const peer_id_t& routeID)
		{
			router_table_[id].insert(routeID);
		}

		bool pending_to_member(const peer_sptr& p);
		bool neighbor_to_member(const peer_connection_sptr& conn);
		bool to_neighbor(const peer_connection_sptr& conn, int maxNbrCnt);
		bool member_to_pending(const peer_connection_sptr& conn);

		bool keep_pending_active_sockets(const peer_connection_sptr& conn)
		{
			return pending_active_sockets_.try_keep(conn, seconds(60));
		}

		bool keep_pending_passive_sockets(const peer_connection_sptr& conn)
		{
			return pending_passive_sockets_.try_keep(conn, seconds(20));
		}

		int neighbor_count()const{ return neighbors_conn_.size(); }

		void set_tcp_local_endpoint(const endpoint& edp){ tcp_local_endpoint_ = edp; }
		void set_udp_local_endpoint(const endpoint& edp){ udp_local_endpoint_ = edp; }

		endpoint urdp_acceptor_local_endpoint()const;
		endpoint trdp_acceptor_local_endpoint()const;

	protected:
		//网络事件处理
		void on_accepted(peer_connection_sptr conn, const error_code& ec);
		void on_connected(peer_connection* conn, const error_code& ec);
		void on_disconnected(peer_connection* conn, const error_code& ec);
	protected:
		//消息处理
		void on_recvd_handshake(peer_connection* conn, safe_buffer& buf);
		void on_recvd_neighbor_table_exchange(peer_connection* conn, safe_buffer buf);

	protected:
		//定时器事件
		void on_neighbor_maintainer_timer();
		void on_neighbor_exchange_timer();

	private:
		void __async_connect_peer(peer* p, bool peerIsNat, 
			bool sameSubnet, 
			peer_connection* conn = NULL, 
			int slotOfInternalIP = 0, 
			error_code ec = error_code(), 
			coroutine coro = coroutine()
			);
		void __send_neighbor_exchange_to(peer_connection* conn);
		void __close(bool flush = false);
		bool __insert_to_members(const peer_id_t& id, const peer_sptr& p)
		{
			return members_.insert(std::make_pair(id, p)).second;
		}
		bool __insert_to_unreachable_members(const peer_id_t& id, const peer_sptr& p)
		{
			return unreachable_members_.try_keep(std::make_pair(id, p), seconds(600));
		}
		bool __insert_to_neighbors(const peer_id_t& id, const peer_connection_sptr& conn)
		{
			BOOST_ASSERT(conn&&id == peer_id_t(conn->get_peer()->get_peer_info().peer_id()));
			return neighbors_conn_.insert(std::make_pair(id, conn)).second;
		}

	protected:
		client_service_wptr client_service_;

		boost::shared_ptr<scheduling_base> scheduling_;//调度器

		router_table router_table_;//路由表，路由消息用

		endpoint tcp_local_endpoint_;
		endpoint udp_local_endpoint_;

		int topology_id_;//拓扑ID, topology_id_t
		std::string topology_acceptor_domain_base_;//拓扑domain

		peer_map members_;//
		timed_keeper_map<peer_id_t, peer_sptr> unreachable_members_;//链接尝试多次，从未连接成功的members
		timed_keeper_map<peer_id_t, peer_sptr> pending_peers_;
		neighbor_map neighbors_conn_;
		int max_neighbor_cnt_;
		time_duration neighbor_ping_interval_;
		timed_keeper_set<peer_id_t> low_capacity_peer_keeper_;//低性能节点保存器

		boost::shared_ptr<timer> neighbor_maintainer_timer_;//邻居维护定时器
		timestamp_t last_exchange_neighbor_time_;//上次进行邻居交换的时刻
		timestamp_t last_member_request_time_;//上次请求邻居表的时间

		timed_keeper_set<peer_connection_sptr> pending_active_sockets_;//主动连接缓存
		timed_keeper_set<peer_connection_sptr> pending_passive_sockets_;//被动连接缓存
	};
}
#endif//peer_topology_base_h__

