//
// member_service.h
// ~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2008 GuangZhu Wu (GuangZhuWu@gmail.com)
//
// All rights reserved. 
//
#ifndef tracker_member_service_h__
#define tracker_member_service_h__

#include <map>
#include <vector>
#include <boost/optional.hpp>
#include <boost/unordered_map.hpp>

#include "tracker/config.h"
#include "tracker/member_table.h"

namespace p2tracker{

	class tracker_service;
	class channel;
	class tracker_service_logic_base;

	class member_service
		:public basic_engine_object
		, public basic_tracker_object
		, public basic_mix_acceptor<member_service, message_acceptor>
		, public ts2p_challenge_checker
	{
	private:
		typedef member_service this_type;
		typedef boost::optional<channel&> optional_channel;
		typedef channel::suspend_channel  empty_channel;
		typedef boost::unordered_map<std::string, boost::shared_ptr<empty_channel> > empty_channel_map;
		typedef boost::unordered_map<std::string, channel::shared_ptr > channel_map_type;
		typedef channel_map_type::value_type channel_pair_type;
		SHARED_ACCESS_DECLARE;

		friend class basic_mix_acceptor<member_service, message_acceptor>;
	public:
		typedef variant_endpoint endpoint;
		typedef member_table::peer peer;
		typedef rough_timer  timer;
		typedef boost::shared_ptr<member_table> member_table_sptr;

	protected:
		member_service(io_service& net_svc, tracker_param_sptr param);
		virtual ~member_service();

	public:
		static shared_ptr create(io_service& net_svc, tracker_param_sptr param)
		{
			return shared_ptr(new this_type(net_svc, param), 
				shared_access_destroy<this_type>());
		}
	public:
		//启动一个直播频道
		void start(boost::shared_ptr<tracker_service_logic_base>tsLogic, 
			const peer_info&   serverInfo, 
			const message_socket_sptr server_socket, 
			const live_channel_info& channelInfo);

		//启动一个点播频道
		void start(boost::shared_ptr<tracker_service_logic_base>tsLogic, 
			const peer_info&  serverInfo, 
			const message_socket_sptr server_socket, 
			const vod_channel_info& channelInfo
			);
		void stop();
		void known_offline(const peer& p);
		void remove_channel(const std::string& channelID);
		void recvd_peer_info_report(peer*, const p2ts_quality_report_msg&);

	protected:
		channel::shared_ptr get_channel(const std::string& channelID);
		void kickout(const std::string& channelID, const peer_id_t& id);
		channel::shared_ptr create_channel(const std::string& channelID, message_socket_sptr conn);
		bool broad_cast_condition(uint32_t min_channel_cnt = 100, float change_thresh = 0.1);

	protected:
		//网络事件处理
		void on_accepted(message_socket_sptr conn, const error_code& ec);

	protected:
		//网络消息处理
		void register_message_handler(message_socket*);
		void on_recvd_login(message_socket*, safe_buffer);

	protected:
		//helper functions
		template<typename ChannelInfoType>
		void start_any_channel(boost::shared_ptr<tracker_service_logic_base>tsLogic, 
			const std::string&  channelID, 
			const peer_info&  serverInfo, 
			const message_socket_sptr server_socketm, 
			const ChannelInfoType& channelInfo);
		void update_channel_broadcast_timer();
		void __start(boost::shared_ptr<tracker_service_logic_base>tsLogic, const std::string& channelID);
		bool pending_socket_check(message_socket_sptr conn);
		void play_point_translate(const std::string& channelID, peer_info& peerInfo);
		bool challenge_channel_check(message_socket_sptr conn, p2ts_login_msg& msg);	
		bool restore_channel(channel& des_channel, boost::shared_ptr<empty_channel>& ept_channel);
		boost::uint32_t reset_channel_broadcaster(channel& this_channel, boost::uint32_t counter, 
			float channel_cnt_per_sec);

	protected:
		boost::weak_ptr<tracker_service_logic_base> tracker_service_logic_;
		channel_map_type  channel_set_;	
		uint32_t	channel_change_cnt_;
		empty_channel_map  empty_channels_;
		timed_keeper_set<message_socket_sptr>  pending_sockets_;
	};
}

#endif//tracker_member_service_h__