//
// peer.h
// ~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2008 GuangZhu Wu (GuangZhuWu@gmail.com)
//
// All rights reserved. 
//
#ifndef peer_peer_h__
#define peer_peer_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "client/typedef.h"

namespace p2client{

	class peer
		: public basic_object
	{
		typedef peer this_type;
		SHARED_ACCESS_DECLARE;
		enum{IDLE, CONNECTING, CONNECTED};
		enum{MAX_TOPOLOGY_CNT=4};
		enum{CHUNK_MAP_SIZE=1024/8};

	public:
		static shared_ptr create(bool sameChannel, bool isServer)
		{
			return shared_ptr(new peer(sameChannel, isServer), 
				shared_access_destroy<peer>() );
		}
	protected:
		peer(bool sameChannel, bool isServer);
		~peer();

		//----member operation and infor--------
	public:
		peer_info& get_peer_info(){ return peer_info_; }
		bool is_server(){ return is_server_; }
		timestamp_t& last_connect_time(int i){return last_connect_time_[i];}
		int mark_unreachable(int topologyID){return ++unreachable_mark_cnt_[topologyID];}
		void mark_reachable(int topologyID){unreachable_mark_cnt_[topologyID]=0;}
		int unreachable_count(int topologyID)const{return unreachable_mark_cnt_[topologyID];}
		bool playing_the_same_channel()const { return the_same_channel_;}
		bool is_udp_restricte()const{return is_udp_restricte_;}
		void set_udp_restricte(bool restricte){is_udp_restricte_=restricte; }
		bool is_rtt_accurate()const{return is_rtt_accurate_;}
		bool is_server()const{return is_server_;}
		int rtt()const
		{
			if (rtt_<=0) return 1500;
			return rtt_;
		}
		int rtt_var()const
		{
			if (rtt_var_<0) return std::max(rtt()/4, 200/2);
			return std::max(rtt_var_, 200/4);
		}
		int rto()const;

		char* chunk_map()
		{
			if (!chunk_map_)
			{
				chunk_map_ =(char*)memory_pool::malloc(CHUNK_MAP_SIZE*sizeof(chunk_map_[0]));
				memset(chunk_map_, 0, CHUNK_MAP_SIZE*sizeof(chunk_map_[0]));
			}
			return chunk_map_;
		}
		int chunk_map_size()const{return CHUNK_MAP_SIZE;}

		bool is_known_peer(const peer_id_t& peerID)
		{
			return known_peers_.is_keeped(peerID);
		}
		void set_known_peer(const peer_id_t& peerID)
		{
			known_peers_.try_keep(peerID, seconds(1200));
		}
		bool is_known_me_known(const peer_id_t& peerID)
		{
			return known_me_known_peers_.is_keeped(peerID);
		}
		void set_known_me_known(const peer_id_t& peerID)
		{
			known_me_known_peers_.try_keep(peerID, seconds(1200));
		}
	protected:
		timed_keeper_set<peer_id_t> known_peers_;
		timed_keeper_set<peer_id_t> known_me_known_peers_;
		peer_info peer_info_;
		timestamp_t last_connect_time_[MAX_TOPOLOGY_CNT];
		int8_t unreachable_mark_cnt_[MAX_TOPOLOGY_CNT];
		int rtt_, rtt_var_, old_rtt_;
		char* chunk_map_;
		bool    is_server_:1;
		bool	is_rtt_accurate_:1;
		bool	the_same_channel_:1;
		bool	is_udp_restricte_:1;
		bool	is_udp_:1;

		
		//----neighbor operation and infor--------
	public:
		void ignore_subscription_seqno(seqno_t seqno, int substreamID)
		{
			BOOST_ASSERT(neighbor_info_);
			if (neighbor_info_)
				neighbor_info_->substream_info_[substreamID].m_ignore_seqnos.try_keep(seqno, seconds(60));
		}

		bool is_ignored_subscription_seqno(seqno_t seqno, int substreamID)
		{
			BOOST_ASSERT(neighbor_info_);
			if (neighbor_info_)
				return neighbor_info_->substream_info_[substreamID].m_ignore_seqnos.is_keeped(seqno);
			return false;
		}

		boost::optional<int>& average_push_to_remote_delay(int subStreamID)
		{
			BOOST_ASSERT(neighbor_info_);
			if (!neighbor_info_)
				return get_static_null_value<boost::optional<int> >();
			return neighbor_info_->substream_info_[subStreamID].m_push_to_remote_delay;
		}

		int max_push_to_remote_delay(int subStreamID)
		{
			BOOST_ASSERT(neighbor_info_);
			if (!neighbor_info_ || !neighbor_info_->substream_info_[subStreamID].m_push_to_remote_delay)
				return -1;
			return neighbor_info_->substream_info_[subStreamID].m_push_to_remote_delay.get()
				+ neighbor_info_->substream_info_[subStreamID].m_push_to_remote_delay_var/4;
		}

		int download_from_local_speed()const
		{
			BOOST_ASSERT(neighbor_info_);
			if (!neighbor_info_)
				return 0;
			return (int)neighbor_info_->down_speed_meter_long_time_.bytes_per_second();
		}

		int upload_to_local_speed()const
		{
			BOOST_ASSERT(neighbor_info_);
			if (!neighbor_info_)
				return 0;
			return (int)neighbor_info_->up_speed_meter_long_time_.bytes_per_second();
		}

		int push_to_local_speed()const
		{
			BOOST_ASSERT(neighbor_info_);
			if (!neighbor_info_)
				return 0;
			return (int)neighbor_info_->push_to_local_speed_meter_.bytes_per_second();
		}

		int residual_tast_count();

		void set_connection(const message_socket_sptr& s);

		timestamp_t& last_buffermap_exchange_time()
		{
			BOOST_ASSERT(neighbor_info_);
			if (!neighbor_info_)
				return get_static_null_value<timestamp_t>();
			return neighbor_info_->last_buffermap_exchange_time_;
		}
		timestamp_t& last_neighbor_exchange_time()
		{
			BOOST_ASSERT(neighbor_info_);
			if (!neighbor_info_)
				return get_static_null_value<timestamp_t>();
			return neighbor_info_->last_neighbor_exchange_time_;
		}

		timestamp_t& last_unsubscription_time()
		{
			BOOST_ASSERT(neighbor_info_);
			if (!neighbor_info_)
				return get_static_null_value<timestamp_t>();
			return neighbor_info_->last_unsubscription_time_;
		}
		timestamp_t& last_subscription_time()
		{
			BOOST_ASSERT(neighbor_info_);
			if (!neighbor_info_)
				return get_static_null_value<timestamp_t>();
			return neighbor_info_->last_subscription_time_;
		}
		int& subscription_count()
		{
			BOOST_ASSERT(neighbor_info_);
			if (!neighbor_info_)
				return get_static_null_value<int>();
			return neighbor_info_->subscription_count_;
		}
		timestamp_t& last_subscription_time(int substreamID)
		{
			BOOST_ASSERT(neighbor_info_);
			if (!neighbor_info_)
				return get_static_null_value<timestamp_t>();
			return neighbor_info_->substream_info_[substreamID].m_last_subscription_time;
		}
		timestamp_t& last_unsubscription_time(int substreamID)
		{
			BOOST_ASSERT(neighbor_info_);
			if (!neighbor_info_)
				return get_static_null_value<timestamp_t>();
			return neighbor_info_->substream_info_[substreamID].m_last_unsubscription_time;
		}
		bool known_current_playing_timestamp()const
		{			
			BOOST_ASSERT(neighbor_info_);
			if (!neighbor_info_)
				return false;
			return neighbor_info_->last_known_current_playing_timestamp_time_.is_initialized();
		}
		void set_current_playing_timestamp(timestamp_t t, timestamp_t now=timestamp_now())
		{
			BOOST_ASSERT(neighbor_info_);
			if (!neighbor_info_)
				return ;
			neighbor_info_->current_playing_timestamp_=t;
			neighbor_info_->last_known_current_playing_timestamp_time_=now;
		}
		timestamp_t current_playing_timestamp(timestamp_t now=timestamp_now())const
		{
			BOOST_ASSERT(known_current_playing_timestamp());
			if (!neighbor_info_)
				return 0;
			return neighbor_info_->current_playing_timestamp_
				+time_minus(now, neighbor_info_->last_known_current_playing_timestamp_time_.get());
		}
		boost::optional<seqno_t>& smallest_buffermap_exchange_seqno()
		{
			if (!neighbor_info_)
				return get_static_null_value<boost::optional<seqno_t> >();
			return neighbor_info_->smallest_buffermap_exchange_seqno_;
		}
		boost::optional<seqno_t>& smallest_cached_seqno()
		{
			if (!neighbor_info_)
				return get_static_null_value<boost::optional<seqno_t> >();
			return neighbor_info_->smallest_cached_seqno_;
		}
		boost::optional<seqno_t>& last_push_to_local_seqno()
		{			
			if (!neighbor_info_)
				return get_static_null_value<boost::optional<seqno_t> >();
			return neighbor_info_->last_push_to_local_seqno_;
		}

		timestamp_t& last_media_confirm_time()
		{			
			if (!neighbor_info_)
				return get_static_null_value<timestamp_t>();
			return neighbor_info_->last_media_confirm_time_;
		}
		std::vector<seqno_t>& media_download_from_local()
		{			
			if (!neighbor_info_)
				return get_static_null_value<std::vector<seqno_t> >();
			BOOST_ASSERT(neighbor_info_->seqno_download_from_local_.capacity() < 1024);
			return neighbor_info_->seqno_download_from_local_;
		}
		bool register_expires_handler(timed_keeper_set<seqno_t>::expires_handle_type h)
		{
			if (!neighbor_info_)
				return false;
			neighbor_info_->task_keeper_.register_expires_handler(h);
			return true;
		}
		void be_member(message_socket* conn)
		{
			if (!neighbor_info_)
				return;
			neighbor_info_->hold_sockets_.erase(conn);
			if (neighbor_info_->hold_sockets_.empty())
				neighbor_info_.reset();
		}
		void be_neighbor(const message_socket_sptr& conn)
		{
			if (!neighbor_info_)
				neighbor_info_.reset(new neighbor_info(is_server_));
			neighbor_info_->hold_sockets_.insert(conn.get());
		}
		int max_task_delay(double lostRate);
		int keep_task(seqno_t seqno, double lostRate);
		void task_success(seqno_t seqno, bool dupe);
		void task_fail(seqno_t seqno);
		int  tast_count();
		void tasks(std::vector<seqno_t>& tsks);
		void on_upload_to_local(int bytes_recvd, bool ispush);
		void on_download_from_local(int bytes_sendt, seqno_t seqno);
		void on_push_to_remote(int delay, int subStreamID);

	private:
		int shrink_cwnd(size_t sizeBeforClear, size_t sizeAfterClear);

		struct neighbor_info:object_allocator{
			neighbor_info(bool isServer);

			boost::optional<seqno_t> smallest_buffermap_exchange_seqno_;
			boost::optional<seqno_t> smallest_cached_seqno_;

			rough_speed_meter dupe_speed_meter_long_time_;//
			rough_speed_meter up_speed_meter_long_time_;//向本地的上传速度
			rough_speed_meter down_speed_meter_long_time_;//从本地的下载速度
			rough_speed_meter up_speed_meter_;//向本地的上传速度
			rough_speed_meter down_speed_meter_;//从本地的下载速度
			rough_speed_meter task_speed_meter_;//分配任务速度
			rough_speed_meter push_to_local_speed_meter_;//向本地push的速率

			timestamp_t			last_buffermap_exchange_time_;
			timestamp_t			last_neighbor_exchange_time_;
			timestamp_t			last_media_confirm_time_;
			std::vector<seqno_t> seqno_download_from_local_;

			timed_keeper_set<seqno_t> task_keeper_;//local向本peer请求的数据
			double task_wnd_;
			double ssthresh_;

			struct substream_info
			{
				timestamp_t m_last_unsubscription_time;
				timestamp_t m_last_subscription_time;
				timed_keeper_set<seqno_t> m_ignore_seqnos;
				boost::optional<int> m_push_to_remote_delay;
				int m_push_to_remote_delay_var;
			};
			std::vector<substream_info> substream_info_;
			int subscription_count_;
			timestamp_t last_unsubscription_time_;
			timestamp_t last_subscription_time_;
			boost::optional<seqno_t> last_push_to_local_seqno_;

			timestamp_t current_playing_timestamp_;
			boost::optional<timestamp_t> last_known_current_playing_timestamp_time_;

			std::set<message_socket*, std::less<message_socket*>, 
				allocator<message_socket*> > hold_sockets_;
		};
		boost::scoped_ptr<neighbor_info> neighbor_info_;
#ifdef DEBUG_SCOPE_OPENED
		std::set<seqno_t, wrappable_less<seqno_t> > buffermap_;
	public:
		void on_recvd_buffermap(seqno_t seqno)
		{
			buffermap_.insert(seqno);
			while(buffermap_.size())
			{
				if (buffermap_.size()>2000
					||seqno_less((*buffermap_.begin()+2000), (*buffermap_.rbegin()))
					)
				{
					buffermap_.erase(buffermap_.begin());
				}
				else
				{
					break;
				}
			}
		}
		double buffmap_health()
		{
			if (buffermap_.empty())
				return 0.0;
			return double(buffermap_.size())
				/(double)(seqno_minus(*buffermap_.rbegin(), *buffermap_.begin())+1.0);
		}
#endif

	};
}

#endif//peer_peer_h__
