//
// seed_connection.h
// ~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2008 GuangZhu Wu (GuangZhuWu@gmail.com)
//
// All rights reserved. 
//
#ifndef server_seed_connection_h__
#define server_seed_connection_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "server/config.h"
#include <p2engine/push_warning_option.hpp>
#include <vector>
#include <p2engine/pop_warning_option.hpp>

namespace p2server{

	class seed_connection
		:public message_socket
	{
		typedef seed_connection this_type;
		SHARED_ACCESS_DECLARE;
	public:
		enum seed_type{SEED, SUPER_SEED};

		struct seed_connection_score_less 
		{
			bool operator()(const shared_ptr&s1, const shared_ptr&s2)const
			{
				if (s1&&s2)
				{
					if (s1->m_score!=s2->m_score)
						return s1->m_score<s2->m_score;
				}
				return s1<s2;
			}
		};
	public:
		seed_connection(io_service& ios, bool realTimeUsage, bool isPassive);
		virtual ~seed_connection();

	public:
		bool is_super_seed()const
		{
			return m_type==SUPER_SEED;
		}
		bool is_normal_seed()const
		{
			return m_type==SEED;
		}
		double score()const
		{
			return m_score;
		}
		void score(double n)
		{ 
			m_score=n;
		}
		const ptime& be_seed_timestamp()const
		{return m_be_seed_timestamp;};
		const ptime& be_super_seed_timestamp()const
		{return m_be_super_seed_timestamp;};

		void set_seed(const ptime& now, const time_duration& pingInterval);
		void set_super_seed(const ptime& now, const time_duration& pingInterval);

		void piece_notified(const ptime& now)
		{
			m_last_piece_notify_time=now;
		}
		void piece_notify(const safe_buffer& sndbuf, const ptime& now);

		void piece_confirmed(const ptime& now)
		{
			m_last_media_confirm_time=now;
		}
		void piece_confirm(const ptime& now);

		void media_have_sent(seqno_t seqno, const ptime& now)
		{
			m_media_have_sent.push_back(seqno);
			if (m_media_have_sent.size()>4)
				piece_confirm(now);
		}
		void send_media_packet(const safe_buffer& buf, seqno_t seqno, const ptime& now);

	private:
		ptime m_be_seed_timestamp;
		ptime m_be_super_seed_timestamp;
		ptime m_last_piece_notify_time;
		ptime m_last_media_confirm_time;
		double m_score;
		std::vector<seqno_t>m_media_have_sent;
		seed_type m_type;
		seqno_t m_cur_seqno;
	};

	RDP_DECLARE(seed_connection, seed_acceptor);
}

#endif//server_seed_connection_h__