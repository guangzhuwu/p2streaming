//
// peer_connection.h
// ~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2008 GuangZhu Wu (GuangZhuWu@gmail.com)
//
// All rights reserved. 
//
#ifndef simple_server_peer_connection_h__
#define simple_server_peer_connection_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "simple_server/config.h"
#include <p2engine/push_warning_option.hpp>
#include <vector>
#include <p2engine/pop_warning_option.hpp>

namespace p2simple{

	class peer_connection
		:public message_socket
	{
		typedef peer_connection this_type;
		SHARED_ACCESS_DECLARE;
	public:
		peer_connection(io_service& ios, bool realTimeUsage, bool isPassive);
		virtual ~peer_connection();

	public:
		const std::string& channel_id()const
		{
			BOOST_ASSERT(!channel_id_.empty());
			return channel_id_;
		};
		void channel_id(const std::string& channelID)
		{
			channel_id_=channelID;
		}
		void piece_confirmed(timestamp_t now)
		{
			m_last_media_confirm_time=now;
		}
		void piece_confirm(timestamp_t now);

		void media_have_sent(seqno_t seqno, timestamp_t now)
		{
			m_media_have_sent.push_back(seqno);
			if (m_media_have_sent.size()>4)
				piece_confirm(now);
		}
		void send_media_packet(const safe_buffer& buf, seqno_t seqno, 
			timestamp_t now);

	private:
		timestamp_t m_last_media_confirm_time;
		std::vector<seqno_t>m_media_have_sent;
		std::string channel_id_;
	};

	RDP_DECLARE(peer_connection, peer_acceptor);
}

#endif//simple_server_peer_connection_h__