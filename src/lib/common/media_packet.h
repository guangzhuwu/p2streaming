//
// const_define.h
// ~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2008 GuangZhu Wu (GuangZhuWu@gmail.com)
//
// All rights reserved. 
//

#ifndef common_media_packet_h__
#define common_media_packet_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "common/config.h"
#include "common/const_define.h"
#include "common/message_type.h"
#include "common/typedef.h"

namespace p2common{

	enum frame_priority{
		P_FAME=1, 
		B_FAME=2, 
		I_FRAME=3
	};

	//mediapacket流量很大，为避免过多内存拷贝，没有使用googleprotobuf
	namespace media_packet_header
	{
		P2ENGINE_PACKET_FORMAT_DEF_BEGIN(def, 0, {})
			P2ENGINE_PACKET_FIELD_DEF(int32_t, src_id32)//源节点的id32（并非uuid，这个id32主要用在interactive应用中）
			P2ENGINE_PACKET_FIELD_DEF(timestamp_t, time_stamp)//包的分发时间
			P2ENGINE_PACKET_FIELD_DEF(seqno_t, seqno)
			P2ENGINE_PACKET_BIT_FIELD_DEF_INIT(uint16_t, packet_rate, 9, 0)
			P2ENGINE_PACKET_BIT_FIELD_DEF_INIT (uint16_t, hop, 7, 0)
			P2ENGINE_PACKET_FIELD_DEF (uint8_t, session_id)
			P2ENGINE_PACKET_BIT_FIELD_DEF_INIT (uint8_t, level, 1, USER_LEVEL)
			P2ENGINE_PACKET_BIT_FIELD_DEF_INIT (uint8_t, channel_id, 4, 0)
			P2ENGINE_PACKET_BIT_FIELD_DEF_INIT (uint8_t, priority, 2, 0)
			P2ENGINE_PACKET_BIT_FIELD_DEF_INIT (uint8_t, is_push, 1, 0)
			P2ENGINE_PACKET_FIELD_DEF(uint64_t, recent_seqno_map)//8字节，可以存放多个seqno
			P2ENGINE_PACKET_FIELD_DEF(int64_t, anti_pollution_signature)//signature//防范污染攻击用
			P2ENGINE_PACKET_FORMAT_DEF_END
	}
	typedef p2engine::packet<media_packet_header::def> media_packet;
}

#endif//common_media_packet_h__