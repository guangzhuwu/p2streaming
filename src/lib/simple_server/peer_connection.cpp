#include "simple_server/peer_connection.h"
#include "client/peer.h"

using namespace p2simple;

namespace
{
	const  time_duration MEDIA_CONFIRM_INTERVAL=milliseconds(100);
}

peer_connection::peer_connection(io_service& ios, bool realTimeUsage, bool isPassive)
	:message_socket(ios, realTimeUsage, isPassive)
{
	m_last_media_confirm_time=timestamp_now();
}

peer_connection::~peer_connection()
{
}

void peer_connection::piece_confirm(timestamp_t now)
{
	if (!is_connected())
	{
		return;
	}

	int64_t interval=MEDIA_CONFIRM_INTERVAL.total_milliseconds();
	if(!m_media_have_sent.empty())
	{
		if (m_media_have_sent.size()>8
			||is_time_passed(interval, m_last_media_confirm_time, now)
			)
		{
			media_sent_confirm_msg msg;
			BOOST_FOREACH(seqno_t seq, m_media_have_sent)
			{
				msg.add_seqno(seq);
			}
			async_send_unreliable(serialize(msg), global_msg::media_sent_confirm);
			m_media_have_sent.clear();
			m_last_media_confirm_time=now;
		}
	}
	else
	{
		m_last_media_confirm_time=now;
	}
}

void peer_connection::send_media_packet(const safe_buffer& buf, 
	seqno_t seqno, timestamp_t now)
{
	async_send_unreliable(buf, global_msg::media);
	media_have_sent(seqno, now);
}
