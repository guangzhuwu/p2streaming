//
// media_distributor.h
// ~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2008 GuangZhu Wu (GuangZhuWu@gmail.com)
//
// All rights reserved. 
//
#ifndef server_stream_distribution_h__
#define server_stream_distribution_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include <p2engine/push_warning_option.hpp>
#include <vector>
#include <deque>
#include <boost/unordered_set.hpp>
#include <p2engine/pop_warning_option.hpp>

#include "server/config.h"
#include "server/seed_connection.h"

namespace p2server{
	namespace multi_index=boost::multi_index;

	class server;

	class media_distributor
		:public basic_engine_object
		, public basic_server_object
		, public basic_mix_acceptor<media_distributor, seed_acceptor>
	{
		typedef media_distributor this_type;
		SHARED_ACCESS_DECLARE;

	protected:
		typedef variant_endpoint endpoint;
		typedef rough_timer timer;
		typedef std::set<seed_connection_sptr, seed_connection::seed_connection_score_less> seed_peer_set;
		friend  class basic_mix_acceptor<media_distributor, seed_acceptor>;

	public:
		static shared_ptr create(io_service& net_svc, server_param_sptr param)
		{
			shared_ptr obj(new this_type(net_svc, param), 
				shared_access_destroy<this_type>());
			return obj;
		}

		void start(peer_info& local_info, error_code& ec);
		void stop();

	protected:
		media_distributor(io_service& net_svc, server_param_sptr param);
		virtual ~media_distributor();

	public:
		//分发
		void distribute(const media_packet& pkt)
		{
			do_distribute(pkt, 0);
		}
		//平滑后分发(会有一个几十秒的延迟)
		void smooth_distribute(const media_packet& pkt);
		//将包原封不动的发送出去
		void relay_distribute(const media_packet& pkt);
		//当前分发seqno
		seqno_t current_media_seqno()const
		{
			return media_pkt_seq_seed_;
		}
		timestamp_t current_time()const
		{
			return timestamp_now();
		}
		int packet_rate()const
		{
			return (int)media_packet_rate_long_.bytes_per_second();
		}
		double out_multiple()const
		{
			double media_bps=media_bps_.bytes_per_second();
			return total_out_bps_.bytes_per_second()/(media_bps+FLT_MIN);
		}
		int bitrate()const
		{
			double media_bps=media_bps_.bytes_per_second();
			return (int)(media_bps)*8/1024;//kb
		}
		int out_kbps()const
		{
			return (int)(total_out_bps_.bytes_per_second())*8/1024;//kb
		}
		int total_seed_count()const
		{
			return seed_peers_.size()+super_seed_peers_.size();
		}
		const std::deque<seqno_t>& iframe_list()const
		{
			return iframe_list_;
		}

	protected:
		//网络事件处理
		void on_accepted(seed_connection_sptr conn, const error_code& ec);
		void on_connected(seed_connection* conn, const error_code& ec);
		void on_disconnected(seed_connection* conn, const error_code& ec);
	protected:
		//网络消息处理
		void register_message_handler(seed_connection_sptr con);
		void on_recvd_media_request(seed_connection*, safe_buffer);
		void on_recvd_info_report(seed_connection*, safe_buffer);
	protected:
		//定时事件处理
		void on_check_seed_peer_timer();
		void on_piece_notify_timer();

	protected:
		//helper functions
		void flood_message(safe_buffer& buf, int level);
		void distribution_push(media_packet& pkt);

	protected:
		void check_super_seed();
		bool try_accept_seed(seed_connection_sptr conn, seed_peer_set& peerset, 
			size_t maxCnt, bool shrink);

		//分发
		void do_distribute(const media_packet& p, int ptsOffset)
		{
			__do_distribute(p, ptsOffset, false);
		}
		void do_pull_distribute(seed_connection_sptr conn, seqno_t seq, bool direct, 
			int smoothDelay);

	protected:
		//记录当前的seqno等
		void store_info();
		void write_packet(const media_packet& pkt);
		void __do_distribute(const media_packet& p, int ptsOffset, bool bFecPacket);

	protected:
		boost::weak_ptr<server> server_;

		//seed_acceptor_sptr urdp_acceptor_;//监听seed_peer的链接请求
		//seed_acceptor_sptr trdp_acceptor_;//监听seed_peer的链接请求

		seed_peer_set super_seed_peers_;//seed 节点
		seed_peer_set seed_peers_;//seed 节点
		uint8_t session_id_;
		timed_keeper_set<peer_id_t> undirect_peers_;//非直连client，这一client不能和本server直连，但希望接收本server的流
		timer::shared_ptr seed_peer_check_timer_;//周期检查seed节点
		timer::shared_ptr piece_notify_timer_;//周期检查seed节点
		uint32_t  piece_notify_times_;//计数器            

		packet_buffer packet_buffer_;//媒体包缓存 
		seqno_t media_pkt_seq_seed_;//生成媒体包序列号的种子
		rough_speed_meter media_packet_rate_;//包速率
		rough_speed_meter media_packet_rate_long_;//包速率

		double current_push_multiple_;//主动推送倍率
		rough_speed_meter media_bps_;//媒体码率测量bit per s
		rough_speed_meter media_bps_short_;//媒体码率测量bit per s
		rough_speed_meter total_out_bps_;//总上行带宽测量（包括主推流和补丁流）bit per s
		rough_speed_meter push_out_bps_;//推送带宽测量（主推流）bit per s
		rough_speed_meter pull_out_bps_;//接近于当前推送点的拉（距离退送点较远的不算）
		ptime   last_modify_push_multiple_time_;
		double  last_alf_;

		std::string cas_string_;
		ptime   last_erase_super_seed_time_;
		ptime   last_change_super_seed_time_;
		ptime   last_recommend_seed_time_;

		boost::unordered_set<unsigned long> pushed_ips_;

		bool running_;

		boost::shared_ptr<boost::thread> store_info_thread_;

		std::deque<seqno_t> iframe_list_;
		boost::optional<seqno_t> last_iframe_seqno_;
		
		std::vector<double> send_multiples_;

		fec_encoder fec_encoder_;
		std::vector<media_packet> fec_results_;

		//smoother push_distrib_smoother_;
		smoother pull_distrib_smoother_, push_distrib_smoother_;
		boost::shared_ptr<media_cache_service> async_dskcache_; //live cache

	};
}

#endif//server_stream_distribution_h__
