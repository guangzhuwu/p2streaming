//
// absent_packet_list.h
// ~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2008 GuangZhu Wu (GuangZhuWu@gmail.com)
//
// All rights reserved. 
//
#ifndef peer_media_packet_info_h__
#define peer_media_packet_info_h__

#include "client/stream/absent_packet_info.h"
#include "asfio/async_dskcache.h"

namespace p2client{

	class absent_packet_list
		: basic_client_object
	{
		typedef std::set<seqno_t, wrappable_less<seqno_t> > absent_packet_set;

	public:
		typedef absent_packet_set::iterator iterator;
		typedef absent_packet_set::iterator const_iterator;
		typedef absent_packet_set::reverse_iterator reverse_iterator;
		typedef absent_packet_set::const_reverse_iterator const_reverse_iterator;

		absent_packet_list(io_service& svc, client_param_sptr param, size_t prefer_size);
		const absent_packet_info* get_packet_info(seqno_t seqno)const
		{
			return __get_packet_info(seqno);
		}
		absent_packet_info* get_packet_info(seqno_t seqno)
		{
			return __get_packet_info(seqno);
		}
		bool just_known(seqno_t seqno, seqno_t smallest_seqno_i_care, timestamp_t now);

		void request_failed(const iterator& itr, timestamp_t now, int reRequestDelay);
		void request_failed(seqno_t seqno, timestamp_t now, int reRequestDelay);
		void set_requesting(seqno_t seqno)
		{
			BOOST_ASSERT(not_requesting_packets_.find(seqno) != not_requesting_packets_.end());
			BOOST_ASSERT(get_packet_info(seqno));
			not_requesting_packets_.erase(seqno);
			requesting_packets_.insert(seqno);
		}

		iterator begin(bool inRequestingState)
		{
			if (inRequestingState)
				return requesting_packets_.begin();
			return not_requesting_packets_.begin();
		}
		iterator end(bool inRequestingState)
		{
			if (inRequestingState)
				return requesting_packets_.end();
			return not_requesting_packets_.end();
		}
		const_iterator begin(bool inRequestingState)const
		{
			if (inRequestingState)
				return requesting_packets_.begin();
			return not_requesting_packets_.begin();
		}
		const_iterator end(bool inRequestingState)const
		{
			if (inRequestingState)
				return requesting_packets_.end();
			return not_requesting_packets_.end();
		}
		reverse_iterator rbegin(bool inRequestingState)
		{
			if (inRequestingState)
				return requesting_packets_.rbegin();
			return not_requesting_packets_.rbegin();
		}
		reverse_iterator rend(bool inRequestingState)
		{
			if (inRequestingState)
				return requesting_packets_.rend();
			return not_requesting_packets_.rend();
		}
		const_reverse_iterator rbegin(bool inRequestingState)const
		{
			if (inRequestingState)
				return requesting_packets_.rbegin();
			return not_requesting_packets_.rbegin();
		}
		const_reverse_iterator rend(bool inRequestingState)const
		{
			if (inRequestingState)
				return requesting_packets_.rend();
			return not_requesting_packets_.rend();
		}
		seqno_t min_seqno()const;
		void erase(reverse_iterator itr, bool inRequestingState)
		{
			return erase((++itr).base(), inRequestingState);
		}
		void erase(const iterator& itr, bool inRequestingState);
		void erase(seqno_t seqno);
		void recvd(seqno_t seqno, const media_packet& pkt, seqno_t now)
		{
			BOOST_AUTO(&infoPtr, get_slot(media_packet_info_vector_, seqno));
			if (infoPtr&&infoPtr->is_this(seqno, now))
				infoPtr->recvd(seqno, pkt, now);
			erase(seqno);
		}
		bool find(seqno_t seqno)const
		{
			return not_requesting_packets_.find(seqno) != not_requesting_packets_.end()
				|| requesting_packets_.find(seqno) != requesting_packets_.end();
		}
		const_iterator find(seqno_t seqno, bool inRequestingState)const
		{
			if (inRequestingState)
				return requesting_packets_.find(seqno);
			return not_requesting_packets_.find(seqno);
		}
		void clear();
		size_t size()const
		{
			return not_requesting_packets_.size() + requesting_packets_.size();
		}
		size_t size(bool inRequestingState)const
		{
			if (inRequestingState)
				return requesting_packets_.size();
			return not_requesting_packets_.size();
		}
		size_t capacity()const
		{
			return media_packet_info_vector_.size();
		}
		bool empty()const
		{
			return not_requesting_packets_.empty() && requesting_packets_.empty();
		}
		void trim(seqno_t smallest_seqno_i_care, int max_range_cnt);

	private:
		absent_packet_info* __get_packet_info(seqno_t seqno, bool justKnown = false)const;
		void __delete(seqno_t seqno);
		void __trim(seqno_t smallest_seqno_i_care, int max_range_cnt, absent_packet_set& lst);

	private:
		absent_packet_set not_requesting_packets_;//当前不处于requesting状态的片段
		absent_packet_set requesting_packets_;//当前处于requesting状态的片段
		mutable std::vector < boost::intrusive_ptr<absent_packet_info> >
			media_packet_info_vector_;//片段信息
		mutable std::vector < boost::intrusive_ptr<absent_packet_info> >
			media_packet_info_pool_;//避免反复new用
		size_t prefer_size_;
		io_service& io_service_;
		boost::shared_ptr<asfio::async_dskcache> async_dskcache_;
	};

}

#endif//peer_packet_info_h__

