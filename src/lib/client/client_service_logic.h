//
// client_service_logic.h
// ~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2008 GuangZhu Wu (GuangZhuWu@gmail.com)
//
// All rights reserved. 
//
#ifndef client_client_service_logic_h__
#define client_client_service_logic_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "client/typedef.h"

namespace p2client{

	class  client_service;

	class client_service_logic_base
		:public basic_engine_object
		, public basic_client_object
	{
		typedef client_service_logic_base this_type;
		SHARED_ACCESS_DECLARE;
	public:
		typedef variant_endpoint endpoint;
		typedef rough_timer timer;

	protected:
		client_service_logic_base(io_service& iosvc);
		virtual ~client_service_logic_base();

	public:
		void start_service(const client_param_base& param);
		void stop_service(bool flush=false);

		void set_play_offset(int64_t offset);

	public:
		//网络消息处理
		virtual void register_message_handler(message_socket*)=0;

	public:
		//挤压未处理的媒体数据长度（一般是播放器缓冲填满后会发生）
		virtual int overstocked_to_player_media_size()=0;

	public:
		//登录失败
		virtual void on_login_failed(error_code_enum code, const std::string& errorMsg)=0;
		//登录成功
		virtual void on_login_success()=0;
		//掉线
		virtual void on_droped()=0;

		//一个新节点加入系统
		virtual void on_join_new_peer(const peer_id_t& newPeerID, const std::string& userInfo)=0;
		//发现了一个早于本节点在线的节点
		virtual void on_known_online_peer(const peer_id_t& newPeerID, const std::string& userInfo)=0;
		//节点离开
		virtual void on_known_offline_peer(const peer_id_t& newPeerID)=0;

		//节点信息改变
		virtual void on_user_info_changed(const peer_id_t& newPeerID, const std::string& oldUserInfo, 
			const std::string& newUserInfo)=0;

		virtual  void on_recvd_media(const safe_buffer& buf, 
			const peer_id_t& srcPeerID, media_channel_id_t mediaChannelID)=0;

		virtual void on_media_end(const peer_id_t& srcPeerID, media_channel_id_t mediaChannelID){}
		//收到消息
		virtual void  on_recvd_msg(const std::string&msg, const peer_id_t& srcPeerID)=0;

	protected:
		boost::shared_ptr<client_service> client_service_;
	};
}

#endif//tracker_client_service_h__