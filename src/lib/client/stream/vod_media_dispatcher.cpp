#include "client/stream/vod_media_dispatcher.h"
#include "client/stream/stream_scheduling.h"
#include "client/client_service.h"
#include "client/client_service_logic.h"

NAMESPACE_BEGIN(p2client);

#define GUARD_TOPOLOGY(returnValue) \
	BOOST_AUTO(topology, scheduling_->get_topology());\
	if (!topology) {return returnValue;}

#define GUARD_CLIENT_SERVICE(returnValue)\
	BOOST_AUTO(clientService, scheduling_->get_client_service());\
	if (!clientService) {return returnValue;}

#define GUARD_CLIENT_SERVICE_LOGIC(returnValue)\
	GUARD_CLIENT_SERVICE(returnValue)\
	BOOST_AUTO(svcLogic, clientService->get_client_service_logic());\
	if (!svcLogic) {return returnValue;}


vod_media_dispatcher::vod_media_dispatcher(stream_scheduling& scheduling)
	:media_dispatcher(scheduling)
{
	BOOST_AUTO(clientService, scheduling_->get_client_service());
	BOOST_ASSERT(clientService);
	BOOST_AUTO(vodInfo, clientService->get_vod_channel_info());
	BOOST_ASSERT(vodInfo);
	max_seqno_=(vodInfo->film_length()-1)/PIECE_SIZE;
}

vod_media_dispatcher::~vod_media_dispatcher()
{
}

timestamp_t vod_media_dispatcher::get_timestamp_adjusted(const media_packet& pkt)const
{
	return timestamp_t((pkt.get_seqno()*1000)/(int)scheduling_->get_src_packet_rate());
}

void vod_media_dispatcher::dispatch_media_packet(media_packet& pkt, 
	client_service_logic_base& svcLogic)
{
	safe_buffer data=pkt.payload();
	seqno_t seqno=pkt.get_seqno();

	//如果是第一个片段，一定校准数据偏移，使其符合播放器请求的range范围。
	if (seqno_less(seqno, min_seqno_))
		return;
	else if (seqno==min_seqno_)
	{
		int64_t diff=(get_client_param_sptr()->offset%PIECE_SIZE);
		if (diff>data.length())
			diff=data.length();
		data=data.buffer_ref((int)diff);
	}
	
	int mediaChannelID=pkt.get_channel_id();
	int32_t srcID=pkt.get_src_id32();
	peer_id_t srcPeerID(&srcID, sizeof(srcID));
	svcLogic.on_recvd_media(data, srcPeerID, mediaChannelID);

	//最后一个片段发出后，要通知播放器播放完毕
	if (pkt.get_seqno() == max_seqno_)
	{
		svcLogic.on_media_end(srcPeerID, mediaChannelID);
	}
}

bool vod_media_dispatcher::be_about_to_play(const media_packet& pkt, 
	double bufferHealth, int overstockedToPlayerSize, timestamp_t now)
{
	BOOST_ASSERT(smallest_seqno_i_care_);

	seqno_t theSeqno(pkt.get_seqno());
	if (*smallest_seqno_i_care_==theSeqno//vod 不允许丢包
		&&overstockedToPlayerSize<OVERSTOCKED_SIZE
		&&(media_pkt_to_player_cache_.size()>512||seqno_greater_equal(media_pkt_to_player_cache_.rbegin()->first+512, max_seqno_))
		)
	{
		return true;
	}

	DEBUG_SCOPE(
		if (*smallest_seqno_i_care_!=theSeqno)
		{
			if (in_probability(0.1))
			{
				std::cout<<"*smallest_seqno_i_care_!=theSeqno, "<<*smallest_seqno_i_care_
					<<"!="<<theSeqno<<std::endl;
			}
		}
		
		
		);

	return false;
}


bool vod_media_dispatcher::can_player_start(double bufferHealth, timestamp_t now)
{
	BOOST_ASSERT(!is_player_started_);

	if (!scheduling_->get_recvd_first_packet_time())
		return false;
	//int averageSrcPacketRage=scheduling_->get_average_src_packet_rate();
	//int minCnt=3*averageSrcPacketRage;
	//if (bufferHealth*media_pkt_to_player_cache_.size()<minCnt
	//	&&seqno_minus(max_seqno_, media_pkt_to_player_cache_.rbegin()->first)<minCnt
	//	)
	//	return false;

	is_player_started_=true;
	min_seqno_=*smallest_seqno_i_care_;
	//sento_player_timestamp_offset_= time_minus(timeStamp, now);//no use
	//smallest_seqno_i_care_=min_seqno_;

	return true;//下次才push
}

void vod_media_dispatcher::check_scheduling_health()
{

}

NAMESPACE_END(p2client);
