#include "server/seed_connection.h"
#include <p2engine/push_warning_option.hpp>
#include <fstream>
#include <p2engine/pop_warning_option.hpp>

using namespace p2server;

namespace
{
	const  time_duration SEED_PIECE_NOTIFY_INTERVAL=milliseconds(200);
	const  time_duration SUPER_SEED_PIECE_NOTIFY_INTERVAL=milliseconds(100);
	const  time_duration MEDIA_CONFIRM_INTERVAL=milliseconds(100);
}

seed_connection::seed_connection(io_service& ios, bool realTimeUsage, bool isPassive)
	:message_socket(ios, realTimeUsage, isPassive)
{
	//刚接到一个seed节点时候，默认其score<0，意思是不会主动推送
	//一直到seed主动发送score>0的信息时，才会推送。
	m_score=-1.0;
	m_media_have_sent.reserve(8);
	m_last_piece_notify_time=min_time();
	m_last_media_confirm_time=min_time();
}
seed_connection::~seed_connection()
{
}

void seed_connection::set_seed(const ptime& now, const time_duration& pingInterval)
{
	safe_buffer buf;
	async_send_reliable(buf, server_peer_msg::be_seed);
	ping_interval(pingInterval);
	m_be_seed_timestamp=now;
	m_type=SEED;
}

void seed_connection::set_super_seed(const ptime& now, const time_duration& pingInterval)
{
	safe_buffer buf;
	async_send_reliable(buf, server_peer_msg::be_super_seed);
	ping_interval(pingInterval);
	m_be_super_seed_timestamp=now;
	m_type=SUPER_SEED;
}

void seed_connection::piece_notify(const safe_buffer& sndbuf, const ptime& now)
{
	if (!is_connected())
	{
		return;
	}
	const time_duration& durationTime=(
		m_type==SEED?SEED_PIECE_NOTIFY_INTERVAL:SUPER_SEED_PIECE_NOTIFY_INTERVAL
		);
	if (m_last_piece_notify_time==min_time()||
		m_last_piece_notify_time+durationTime<now)
	{
		async_send_unreliable(sndbuf, server_peer_msg::piece_notify);

		//加入一定的随机时间，目的是使得s2p_piece_notify消息的发送尽量分散
		//（否则将是每次轮询时候要么不发送要么所有节点发送，造成cpu和带宽占用峰值过高）
		m_last_piece_notify_time=now+milliseconds(random(0, 80));
	}
}

void seed_connection::piece_confirm(const ptime& now)
{
	if (!is_connected())
	{
		m_media_have_sent.clear();
		return;
	}
	if(!m_media_have_sent.empty())
	{
		if (m_last_media_confirm_time==min_time()
			||m_media_have_sent.size()>8
			||m_last_media_confirm_time+MEDIA_CONFIRM_INTERVAL<now
			)
		{
			media_sent_confirm_msg msg;
			BOOST_FOREACH(seqno_t seq, m_media_have_sent)
			{
				msg.add_seqno(seq);
			}
			safe_buffer buf=serialize(msg);
			async_send_unreliable(buf, global_msg::media_sent_confirm);
			m_media_have_sent.clear();
			m_last_media_confirm_time=now;
		}
	}
	else
	{
		m_last_media_confirm_time=now;
	}
}

void seed_connection::send_media_packet(const safe_buffer& buf, 
	seqno_t seqno, const ptime& now)
{
	async_send_unreliable(buf, global_msg::media);
	m_last_piece_notify_time=now;
	media_have_sent(seqno, now);
}
