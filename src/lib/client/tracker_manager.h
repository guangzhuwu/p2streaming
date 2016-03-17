//
// tracker_handler_base.h
// ~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2008 GuangZhu Wu (GuangZhuWu@gmail.com)
//
// All rights reserved. 
//
#ifndef peer_tracker_base_connection__
#define peer_tracker_base_connection__

#include "client/typedef.h"

namespace p2client{

	class tracker_manager
		: public basic_engine_object
		, public basic_client_object
		, public p2ts_challenge_responser
	{
		typedef tracker_manager this_type;
		SHARED_ACCESS_DECLARE;
	public:
		enum state_t{ state_init, state_connecting, state_logining, state_logined };
		enum type_t { live_type, cache_type };

	public:
		boost::function < void(error_code_enum, const ts2p_login_reply_msg&) >
			ON_LOGIN_FINISHED;
		boost::function < void(const relay_msg&) >
			ON_RECVD_USERLEVEL_RELAY;
		boost::function < void(const std::string& channelID, const std::vector<const peer_info*>&) >
			ON_KNOWN_NEW_PEER;

		static shared_ptr create(io_service& svc, client_param_sptr& param, type_t type)
		{
			return shared_ptr(new this_type(svc, param, type)
				, shared_access_destroy<this_type>());
		}

		void start(const std::string& domain);
		void stop();

		//设置channel ID，只是对cache type有用
		void set_channel_id(const std::string& channelID);
		void cache_changed(const std::vector<std::pair<std::string, int> >&);

		boost::optional<endpoint>& local_endpoint(){return local_edp_;}
		const std::list<seqno_t>& iframe_list()const{return iframes_;}

		//向tracker请求合作节点
		void request_peer(const std::string& channelID, bool definiteRequest = false);
		//向tracker报告失效节点
		void report_failure(const peer_id_t& id);
		//向tracker报告本地信息
		void report_local_info();
		//向tracker报告播放质量
		void report_play_quality(const p2ts_quality_report_msg&);

		//向tracker可靠发送
		void async_send_reliable(const safe_buffer& buf, message_type msgType)
		{
			if (socket_)
				socket_->async_send_reliable(buf, msgType);
		}
		//向tracker非可靠发送
		void async_send_unreliable(const safe_buffer& buf, message_type msgType)
		{
			if (socket_)
				socket_->async_send_unreliable(buf, msgType);
		}

	protected:
		tracker_manager(io_service& svc, client_param_sptr param, type_t type);
		virtual ~tracker_manager();

		void on_connected(message_socket*, error_code ec);
		void on_disconnected(message_socket*, const error_code& ec);
		
		void on_recvd_challenge(const safe_buffer&);
		void on_recvd_login_reply(const safe_buffer&);
		void on_recvd_relay(const safe_buffer&);
		void on_recvd_peer_reply(const safe_buffer&);

	protected:
		void on_timer();
		void start_timer();
		void connect_tracker(const endpoint& localEdp, message_socket*sock = NULL, 
			error_code ec = error_code(), coroutine coro = coroutine());
		void do_stop(bool clearSignal = false);

		void register_message_handler();

		bool is_online()const
		{
			BOOST_ASSERT(state_ != state_logined || socket_&&socket_->is_connected());
			return state_ == state_logined&&socket_&&socket_->is_connected();
		}
		bool is_cache_category()const{ return cache_type == tracker_type_; }
		bool is_state(state_t _state)const{ return _state == state_; }

	protected:
		state_t state_;
		std::string domain_;
		message_socket_sptr socket_;
		message_socket_sptr punch_socket_;
		boost::optional<endpoint> local_edp_;
		std::map<message_socket*, message_socket_sptr>socket_map_;
		rough_timer_sptr timer_;
		int relogin_times_;
		boost::optional<timestamp_t> last_connect_fail_time_, last_connect_ok_time_;
		boost::optional<timestamp_t> last_peer_request_time_;
		std::vector<std::pair<std::string, int> > cache_change_vector_;
		timed_keeper_set<uint64_t> flood_or_relay_msgid_keeper_;

		boost::optional<vod_channel_info>  vod_channel_info_;
		boost::optional<live_channel_info> live_channel_info_;

		int peer_cnt_;
		type_t tracker_type_;
		std::string channel_id_;

		std::list<seqno_t> iframes_;
	};

}

#endif//peer_tracker_base_connection__
