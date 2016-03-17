//
// server_service.h
// ~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2008 GuangZhu Wu (GuangZhuWu@gmail.com)
//
// All rights reserved. 
//
#ifndef server_server_service_h__
#define server_server_service_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "server/config.h"
#include "server/media_distributor.h"
#include "server/tracker_session.h"
#include "simple_server/simple_distributor.h"
#include "simple_server/distributor_scheduling.h"

namespace p2simple{
	class distributor_scheduling;
}
namespace p2server{

	class vod_distributor;
	class media_distributor;

	class server_service
		: public basic_engine_object
	{
		typedef server_service this_type;
		SHARED_ACCESS_DECLARE;
	public:
		typedef uint32_t piece_id;
		typedef variant_endpoint endpoint;
		typedef p2simple::simple_distributor_interface simple_distributor_interface;
		typedef boost::shared_ptr<simple_distributor_interface> simple_distributor_sptr;
		typedef p2simple::peer_connection peer_connection;
		typedef boost::function<void(const safe_buffer&)> info_channged_signal_type;

	protected:
		server_service(io_service& ios);
		virtual ~server_service();

	public:
		static shared_ptr create(io_service& ios)
		{
			return shared_ptr(new this_type(ios), 
				shared_access_destroy<this_type>());
		}

	public:
		//启动server_service(作为独立服务器启动)
		void start(const server_param_base& param, error_code& ec, bool embeddedInClient=false);
		//启动，作为一个在room中的节点，启动自身视频分发服务
		//void start(boost::shared_ptr<overlay>);
		void stop();

		bool is_stoped()const{return stoped_;}
	public:
		//重置欢迎词
		void reset_welcome(const std::string& welcome);
		//重置描述
		void reset_discription(const std::string& discription);
		//分发媒体
		void distribute(safe_buffer data, const std::string& srcPeerID, 
			int mediaChannelID, int level);
		void smooth_distribute(safe_buffer data, const std::string& srcPeerID, 
			int mediaChannelID, int level);
		void distribute(media_packet& pkt);
		void smooth_distribute(media_packet& pkt);
		seqno_t current_media_seqno();

	public:
		const std::deque<seqno_t>& iframe_list()const;
		int packet_rate()const;
		int bitrate()const;
		int out_kbps()const;
		double p2p_efficient()const;
		double out_multiple()const;
		int client_count()const{return client_count_;}
		double playing_quality()const{return server_info_.playing_quality();} 
		double global_remote_to_local_lost_rate()const{return server_info_.global_remote_to_local_lost_rate();}

	public:
		boost::shared_ptr<media_distributor>& distributor(){return distributor_;};
		simple_distributor_sptr get_simple_distributor(){return simple_distributor_;}
		peer_info& server_info(){return server_info_;};

		void change_tracker(const std::string& tracker_ipport);
		void add_tracker(const std::string& tracker_ipport);
		void del_tracker(const std::string& tracker_ipport);

	private:
		void on_info_gather_timer();

	protected:
		boost::shared_ptr<media_distributor> distributor_;
		simple_distributor_sptr	simple_distributor_;
		boost::shared_ptr<distributor_scheduling> sdistributor_scheduling_;
		typedef boost::unordered_map<std::string, tracker_session::shared_ptr> tracker_session_map;
		tracker_session_map tracker_sessions_;
		//info_channged_signal_type info_changed_sig_;

		bool stoped_;
		peer_info server_info_;
		int client_count_;
		server_param_sptr server_param_;

		boost::shared_ptr<rough_timer> info_gather_timer_;
	};
}

#endif//server_server_service_h__