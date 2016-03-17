//
// packet_buffer.h
// ~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2008 GuangZhu Wu (GuangZhuWu@gmail.com)
//
// All rights reserved. 
//
#ifndef server_packet_buffer_h__
#define server_packet_buffer_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "common/config.h"
#include "common/utility.h"
#include "common/media_packet.h"

namespace p2common{

	class packet_buffer
	{
		class ring_buffermap
		{
		public:
			ring_buffermap(size_t size = 0)
			{
				resize(size);
			}
			void resize(size_t size)
			{
				size = ROUND_UP(size, 8);
				bitsize_ = (int)size;
				bitset_.resize(size/8);
			}
			void set(uint32_t pos, bool v = true)
			{
				set_bit(&bitset_[0], pos%bitsize_, v);
				BOOST_ASSERT(is_bit(&bitset_[0], pos%bitsize_) == v);
			}
			void reset()
			{
				memset(&bitset_[0], 0, bitset_.size());
			}
			void get(seqno_t pos_start, seqno_t pos_end, void * dst, int dstLen)const;
		private:
			std::vector<char> bitset_;
			int bitsize_;
		};

	public:
		packet_buffer(const time_duration& bufTime, bool isVodCategory);
		bool insert(const media_packet& pkt, int packetRate, timestamp_t now = timestamp_now(),
			const boost::optional<seqno_t>& = boost::optional<seqno_t>()
			);
		bool get(media_packet&pkt, seqno_t seqno, timestamp_t now = timestamp_now())const;
		enum { NOT_HAS = 0, PULLED_HAS, PUSHED_HAS };
		int has(seqno_t seqno, timestamp_t now = timestamp_now())const;
		struct recent_packet_info
		{
			seqno_t m_seqno;
			timestamp_t m_out_time;
		};
		typedef std::deque<recent_packet_info> recent_packet_info_list;
		const recent_packet_info_list& recent_insert()const { return recent_insert_; }
		bool get_buffermap(std::vector<char>& out, seqno_t&firstSeqno, int size = 20 * 8)const;
		bool smallest_seqno_in_cache(seqno_t&)const;
		bool bigest_seqno_in_cache(seqno_t&)const;
		void reset();
		int max_size()const{ return (int)media_packets_.size(); }

	protected:
		typedef std::map<seqno_t, timestamp_t, wrappable_greater<seqno_t> > seqno_map;
		seqno_map recent_seq_lst_long_;

		recent_packet_info_list recent_insert_;
		struct  packet_elm
		{
			packet_elm()
			{
				reset(timestamp_now() - (std::numeric_limits<timestamp_t>::max)() / 4);
			}
			void reset(timestamp_t now)
			{
				m_time = now;
				m_packet_buf.reset();
				m_seqno = 0;
				__inited = false;
			}
			void assign(const media_packet&pkt, timestamp_t now)
			{
				m_time = now;
				m_packet_buf = pkt.buffer();
				m_seqno = pkt.get_seqno();
				m_pushed = (0 != pkt.get_is_push());
				__inited = true;
			}
			safe_buffer m_packet_buf;
			timestamp_t m_time;
			seqno_t m_seqno;
			bool    m_pushed : 1;
			bool    __inited : 1;
		};
		std::vector<packet_elm> media_packets_;

		ring_buffermap buffermap_cache_;

		int buf_time_msec_;
		boost::optional<timestamp_t> bigest_time_stamp_;

		bool is_vod_category_;
	};

}

#endif//server_packet_buffer_h__

