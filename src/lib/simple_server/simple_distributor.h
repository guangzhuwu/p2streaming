//
// simple_distributor.h
// ~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2008 GuangZhu Wu (GuangZhuWu@gmail.com)
//
// All rights reserved. 
//
#ifndef common_simple_distributor_h__
#define common_simple_distributor_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include <p2engine/push_warning_option.hpp>
#include <boost/unordered_map.hpp>
#include <boost/filesystem/path.hpp>
#include <iostream>
#include <fstream>

#include <p2engine/http/http.hpp>
#include "simple_server/config.h"
#include "simple_server/peer_connection.h"

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/member.hpp>

#include "asfio/async_dskcache.h"
#include "common/utility.h"
#include <p2engine/pop_warning_option.hpp>

namespace p2simple{

	/************************************************************************/
	/*           interface of simple distributor                            */
	/************************************************************************/
	class simple_distributor_interface;
	namespace multi_index=boost::multi_index;
	typedef  int64_t   off_type;

	struct  channel_session
	{
		enum {SHIFT_INTERVAL=1000};
		boost::filesystem::path name_;
		uint32_t    id_;
		peer_id_t   assist_id_;
		uint64_t    length_;
		uint64_t    duration_;
		timestamp_t stamp_started_;
		int32_t     min_seqno_;
		int32_t     max_seqno_;
		int32_t     min_seqno_candidate_;//文件被覆盖后新的seqno区间
		int32_t     max_seqno_candidate_;
		boost::shared_ptr<simple_distributor_interface> distributor_;

		channel_session(const boost::filesystem::path& name, uint32_t ID, 
			peer_id_t assistID, uint64_t len, uint64_t duration, uint64_t startStamp)
			:name_(name), id_(ID), assist_id_(assistID), 
			length_(len), duration_(duration), stamp_started_((timestamp_t)startStamp)
		{}

		template<typename ShiftFileHandler>
		void shift(seqno_t seqno_interval, const ShiftFileHandler& handler)
		{
			min_seqno_ -= seqno_interval;
			max_seqno_ -= seqno_interval;

			handler();
		}
		void assign_candidate(int32_t min_seq, int32_t max_seq)
		{
			min_seqno_candidate_ = min_seq;
			max_seqno_candidate_ = max_seq;
		}
		void reschedule(int32_t min_seq, int32_t max_seq)
		{
			min_seqno_ = min_seq;
			max_seqno_ = max_seq;
			min_seqno_candidate_ = -1;
			max_seqno_candidate_ = -1;
		}
	};

	//////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////
	class simple_distributor_interface
	{
	public:
		enum e_type{NONE, SERVER_ADMIN, SERVER_ASSIST};
		virtual void start(peer_info& local_info) = 0;
		virtual void start_assistant(peer_info& local_info, channel_session* channel_info)=0;
		virtual void stop() = 0;
		virtual void cal_channel_file_info(const boost::filesystem::path&)=0;
		virtual off_type get_duration() = 0;
		virtual off_type get_length() = 0;
		virtual void set_len_duration(off_type len, off_type dur)=0;
		virtual int packet_rate()const = 0;
		virtual int bit_rate()const=0;
		virtual int out_kbps()const=0;
		virtual int p2p_efficient()const=0;
		virtual void set_delegate_handler(boost::shared_ptr<simple_distributor_interface> )=0;
		virtual void read_request_media(peer_connection_sptr conn, const media_request_msg& msg)=0;
		virtual void upgrade(e_type)=0;

	protected:
		simple_distributor_interface(){}
		virtual ~simple_distributor_interface(){}
	};

	//////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////

	class distributor_scheduling;
	class simple_distributor
		: public simple_distributor_interface
		, public basic_engine_object
		, public basic_mix_acceptor<simple_distributor, peer_acceptor>
	{
		typedef simple_distributor this_type;
		typedef boost::shared_ptr<simple_distributor_interface> simple_distributor_interface_sptr;
		SHARED_ACCESS_DECLARE;

		friend class basic_mix_acceptor<simple_distributor, peer_acceptor>;
	protected:
		typedef variant_endpoint endpoint;
		typedef rough_timer timer;
		typedef uint32_t piece_id;

	public:
		virtual void start(peer_info& local_info);
		virtual void start_assistant(peer_info& local_info, channel_session* channel_info);
		virtual void stop();
		virtual void cal_channel_file_info(const boost::filesystem::path&);
		virtual off_type get_duration();
		virtual off_type get_length();
		virtual void set_len_duration(off_type len, off_type dur);
		virtual int packet_rate()const;
		virtual int bit_rate()const;
		virtual int out_kbps()const;
		virtual int p2p_efficient()const;
		virtual void set_delegate_handler(boost::shared_ptr<simple_distributor_interface> handler){delegate_handler_=handler;};
		virtual void read_request_media(peer_connection_sptr conn, const media_request_msg& msg);
		virtual void upgrade(e_type type){type_=type;}

	protected:
		simple_distributor(io_service& net_svc, distributor_scheduling* scheduling=NULL);
		virtual ~simple_distributor();

	public:
		//网络事件处理
		void on_accepted(peer_connection_sptr conn, const error_code& ec);
		void on_disconnected(peer_connection* conn, const error_code& ec);

	protected:
		//网络消息处理
		virtual void register_message_handler(peer_connection_sptr con);
		virtual void on_recvd_media_request(peer_connection*, safe_buffer);
		virtual void on_recvd_buffermap_request(peer_connection*, safe_buffer);
		virtual void on_recvd_join_channel(peer_connection*, safe_buffer);
		virtual void __on_disconnected(peer_connection* conn, const error_code& ec){};
		virtual void send_media_packet(peer_connection_sptr conn, const std::string& channelID, 
			seqno_t seqno, bool isCompletePkt, const safe_buffer& buf, const error_code& ec);

	protected:
		void __start(const std::string& domain, peer_info& local_info);
		void __stop();
		void __send_cache_media(peer_connection_sptr conn, const std::string& ID/*peerID or channelID*/
			, seqno_t seqno, const safe_buffer& buf);

		template<typename HandlerType>
		void __on_recvd_cached_media(safe_buffer buf, HandlerType& handler)
		{
			safe_buffer_io io(&buf);
			int32_t size=0;
			seqno_t seqno;
			std::string ID;

			io>>size;
			io.read(ID, size);
			io>>seqno;

			handler(ID, seqno, buf);
		}

	protected:
		void on_dskcache_opened(const error_code&ec){}
		void set_media_pathname(const boost::filesystem::path& pszfile){media_pathname_ = pszfile;}
		rough_speed_meter& media_packet_rate(){return media_packet_rate_;}
		const boost::filesystem::path& media_path_name(){return media_pathname_;}
		channel_session* get_channel_file(){return channel_file_;}
		bool is_admin(){return type_==SERVER_ADMIN;}
		bool is_assist(){return type_==SERVER_ASSIST;}
		std::string cas_string(){return cas_string_;}

	private:
		boost::unordered_set<peer_connection_sptr> sockets_;
		timed_keeper_set<peer_connection_sptr> pending_sockets_;
		bool running_;
		std::string cas_string_;

		int32_t   film_duration_;
		off_type  file_length_;
		boost::optional<int> bit_rate_;
		rough_speed_meter  media_packet_rate_;
		rough_speed_meter  incomming_packet_rate_;
		boost::filesystem::path media_pathname_;

		smoother pull_distrib_smoother_;
		channel_session* channel_file_;
		simple_distributor_interface_sptr delegate_handler_;
		e_type type_;
		distributor_scheduling* scheduling_;
	};

	

}

#endif//common_simple_distributor_h__