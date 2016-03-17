#ifndef _STREMA_ADAPTOR_RECEIVER_H__
#define _STREMA_ADAPTOR_RECEIVER_H__

#include "shunt/media_receiver.h"

#include <p2engine/push_warning_option.hpp>
#include <iostream>
#include <boost/regex.hpp>
#include <boost/lexical_cast.hpp>
#include <p2engine/pop_warning_option.hpp>

namespace p2shunt{

	class client_for_receiver
		:public client_service_logic_base
	{
	public:
		client_for_receiver(media_receiver&rcv)
			:client_service_logic_base(rcv.get_io_service())
			, media_receiver_(rcv)
		{}
		virtual ~client_for_receiver()
		{}

		void register_message_handler(message_socket *){}
	public:
		//登录失败
		virtual void on_login_failed(error_code_enum code, const std::string& errorMsg)
		{
			media_receiver_.is_connected_=false;
		};
		//登录成功
		virtual void on_login_success()
		{
			media_receiver_.is_connected_=true;
		}
		//掉线
		virtual void on_droped()
		{
			media_receiver_.is_connected_=false;
		}

		//一个新节点加入系统
		virtual void on_join_new_peer(const peer_id_t& newPeerID, const std::string& userInfo)
		{}
		//发现了一个早于本节点在线的节点
		virtual void on_known_online_peer(const peer_id_t& newPeerID, const std::string& userInfo)
		{}
		//节点离开
		virtual void on_known_offline_peer(const peer_id_t& newPeerID)
		{}

		virtual void on_user_info_changed(const peer_id_t& newPeerID, const std::string& oldUserInfo, 
			const std::string& newUserInfo)
		{}
		//收到媒体包
		virtual void  on_recvd_media(const void*data, size_t len, 
			const peer_id_t& srcPeerID, media_channel_id_t mediaChannelID)
		{
		}
		virtual void  on_recvd_media(const safe_buffer& buf, 
			const peer_id_t& srcPeerID, media_channel_id_t mediaChannelID)
		{
			media_receiver_.handle_media(buf);
		}
		virtual void on_recvd_msg(const std::string&msg, const peer_id_t& srcPeerID)
		{
		}
		virtual int overstocked_to_player_media_size()
		{
			return 0;
		}
	public:
		media_receiver& media_receiver_;
	};

	media_receiver::media_receiver(io_service& ios)
		: receiver(ios), is_connected_(false)
	{
		key_pair_=security_policy::generate_key_pair();
	}

	media_receiver::~media_receiver()
	{
		if (client_interface_)
		{
			client_interface_->stop_service();
			client_interface_.reset();
		}
	}

	bool media_receiver::updata(const std::string& url, error_code& ec)
	{
		uri u(url, ec);
		if (ec)
			return false;

		BOOST_ASSERT("shunt"==u.protocol());
		if("shunt"!=u.protocol())
			return false;

		std::string ip=u.host();
		if (ip.empty())
			ip="0.0.0.0";
		int port=u.port();
		variant_endpoint edp(asio::ip::address::from_string(ip, ec), port);
		if (the_edp_==edp&&client_interface_)
		{
			return false;
		}
	
		const std::string& key=(u.query_map())["key"];
		std::string pubkey=string_to_hex(key_pair_.first);
		md5_byte_t digest[16];
		md5_state_t pms;
		md5_init(&pms);
		md5_append(&pms, (const md5_byte_t *)key.c_str(), key.length());//模拟cms认证加密方式
		md5_append(&pms, (const md5_byte_t *)pubkey.c_str(), pubkey.length());
		md5_finish(&pms, digest);

		client_param_base param;
		param.type=LIVE_TYPE;
		param.channel_key.assign((char*)digest, (char*)digest+16);
		param.channel_uuid=default_channel_uuid;
		param.channel_link=default_channel_uuid;
		param.public_key=key_pair_.first;
		param.private_key=key_pair_.second;
		param.tracker_host=endpoint_to_string(edp);

		if (client_interface_)
		{
			client_interface_->stop_service();
			client_interface_.reset();
		}
		client_interface_.reset(new client_for_receiver(*this));
		client_interface_->start_service(param);
		return true;
	}
	
	inline void dispatch(media_receiver::media_handler_type media_handler
		, const safe_buffer&buf)
	{
		media_handler(buf);
	}

	void media_receiver::handle_media(const safe_buffer&buf)
	{
		speed_meter_+=buffer_size(buf);
		packet_speed_meter_+=1;
		instantaneous_speed_meter_+=buffer_size(buf);
		instantaneous_packet_speed_meter_+=1;
		
		media_handler(buf);
		//get_io_service().post(
		//	make_alloc_handler(boost::bind(&dispatch, media_handler, buf))
		//	);
	}

}

#endif//_STREMA_ADAPTOR_RECEIVER_H__

