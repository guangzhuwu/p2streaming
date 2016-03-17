#include "client/stream/live_media_dispatcher.h"
#include "client/stream/stream_scheduling.h"
#include "client/client_service.h"
#include "client/client_service_logic.h"
#include "common/tsparse.h"

NAMESPACE_BEGIN(p2client);

#if !defined(_SCHEDULING_DBG) && defined(NDEBUG)
#	define  MEDIA_DISPATCHER_DBG(x) 
#else 
#	define  MEDIA_DISPATCHER_DBG(x) x
#endif

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

live_media_dispatcher::live_media_dispatcher(stream_scheduling& scheduling)
	:media_dispatcher(scheduling)
	, last_change_delay_play_time_time_(timestamp_now())
{
}

live_media_dispatcher::~live_media_dispatcher()
{
}

void live_media_dispatcher::dispatch_media_packet(media_packet& pkt, 
												  client_service_logic_base& svcLogic)
{

	int mediaChannelID=pkt.get_channel_id();
	int32_t srcID=pkt.get_src_id32();
	peer_id_t srcPeerID(&srcID, sizeof(srcID));

	svcLogic.on_recvd_media(pkt.payload(), srcPeerID, mediaChannelID);
}

bool live_media_dispatcher::be_about_to_play(const media_packet& pkt, 
											 double bufferHealth, 
											 int overstockedToPlayerSize, 
											 timestamp_t now)
{
	BOOST_ASSERT(scheduling_->get_recvd_first_packet_time());

	const boost::optional<seqno_t>& currentServerSeqno=
		scheduling_->get_buffer_manager().get_average_current_server_seqno();
	int maxBitmapCnt=scheduling_->get_buffer_manager().max_bitmap_range_cnt();

	seqno_t theSeqno(pkt.get_seqno());
	timestamp_t pts=scheduling_->get_timestamp_adjusted(pkt);
	int delayGuarantee=get_client_param_sptr()->delay_guarantee;
	int maxDelay=std::max(delayGuarantee, get_client_param_sptr()->back_fetch_duration);
	int cnt=get_buffer_size()+scheduling_->get_absent_packet_list().size();
	int bufferDuration1=get_buffer_duration(1.0);
	int bufferDuration=bufferDuration1*bufferHealth;
	int theT=time_minus(pts, get_current_playing_timestamp(now));
	bool isInteractive=is_interactive_category(get_client_param_sptr()->type);
	if (//直播类按照时戳
		(theT<=0)
		||(theT>maxDelay)//error happend
#if 0
		||
		//当 delayGuarantee 较大时，认为是播放器进行缓存控制
		(get_client_param_sptr()->b_fast_push_to_player
		&&delayGuarantee>10000
		&&seqno_greater_equal(*smallest_seqno_i_care_, theSeqno)//片段连续
		&&time_minus(now, scheduling_->get_recvd_first_packet_time().get())<2*delayGuarantee//只是在开始一段时间内较快的推送到播放器
		&&bufferHealth>0.6
		)
#endif
		)
	{		
		//尝试减速向player传递数据
		if (is_time_passed(50*1000, player_start_time_, now)
			&&bufferDuration1<maxDelay*12/10
			&&bufferDuration<maxDelay*5/10
			&&currentServerSeqno&&seqno_greater(theSeqno+maxBitmapCnt, *currentServerSeqno)
			//&&cnt<(scheduling_->get_src_packet_rate()*maxDelay/1000/2)
			)
		{
			change_delay_play_time(now, false);
			return false;
		}
		return true;
	}

	//尝试加速向player传递数据
	if (overstockedToPlayerSize<OVERSTOCKED_SIZE
		&&
		(
		!isInteractive
		&&(bufferDuration1>maxDelay*10/9||bufferDuration>maxDelay*9/10)
		&&(theSeqno==*smallest_seqno_i_care_||bufferDuration1>maxDelay*12/10)
		||
		isInteractive
		&&bufferDuration>maxDelay*10/9
		//&&cnt>(scheduling_->get_src_packet_rate()*maxDelay/1000)
		)
	)
	{
		change_delay_play_time(now, true);
		return true;
	}
	return false;
}

void live_media_dispatcher::change_delay_play_time(timestamp_t now, bool speedup)
{
	if (last_change_delay_play_time_time_!=now
		||!is_time_passed(5*1000, player_start_time_, now)&&speedup
		)
	{
		last_change_delay_play_time_time_=now;
		int t=(speedup?-2:2);
		delay_play_time_+=t;
		sento_player_timestamp_offset_-=t;
	}
	MEDIA_DISPATCHER_DBG(
		static timestamp_t printTime=now-100000;
	if(is_time_passed(1000, printTime, now))
	{
		double health=get_buffer_health();
		int du=get_buffer_duration(1);
		printTime=now;
		std::cout<<"-----------------------delay_play_time--------------:"
			<<delay_play_time_
			<<", "<<"du1.0: "<<(du)
			<<", "<<"du: "<<(du*health)
			<<", "<<"health: "<<health
			<<std::endl;
	}
	);
}

bool live_media_dispatcher::can_player_start(double bufferHealth, timestamp_t now)
{
	BOOST_ASSERT(!is_player_started_);

	if (!scheduling_->get_recvd_first_packet_time())
		return false;

	if (media_pkt_to_player_cache_.size()<50)
		return false;

	int delayGuarantee=get_client_param_sptr()->delay_guarantee;

	double offset=time_minus(now, scheduling_->get_recvd_first_packet_time().get());
	int  t=get_buffer_duration(1.0);
	double speedRate=(t*bufferHealth)/(offset+1.0);
	double maxDelay=std::min(5000.0, 2.2*delayGuarantee);
	double gRemoteToLocalLostrate=global_remote_to_local_lost_rate();
	//对于shunt，要一直等到有足够的数据
	if (g_b_fast_push_to_player&&false==*g_b_fast_push_to_player)
	{
		int waitTime=std::max(get_client_param_sptr()->delay_guarantee, 
			get_client_param_sptr()->back_fetch_duration);
		if (t*bufferHealth<0.95*waitTime)
		{
			return false;
		}
	}
	if (offset<std::max(2000.0, 0.2*delayGuarantee)
		||t*bufferHealth<std::max(std::min(gRemoteToLocalLostrate*10000.0+1000.0, maxDelay), 0.2*delayGuarantee)
		)
		return false;

	bool bTooLong=(offset>=2*maxDelay);//缓冲了太长时间
	bool bSomewhatLong=(offset>=maxDelay&&speedRate>0.8);//缓冲了较长时间
	bool bGoodSpeed=(speedRate>0.95&&bufferHealth>0.85
		||(bufferHealth>0.90||bufferHealth>0.85&&offset<1.3*delayGuarantee)
		);
	bool canPlay=(bTooLong||bSomewhatLong||bGoodSpeed);
	if (!canPlay)
		return false;

	const media_packet& pkt=get_min_packet_in_buffer();
	timestamp_t timeStamp=scheduling_->get_timestamp_adjusted(pkt);
	seqno_t lastGapSeqno=pkt.get_seqno();
	const media_packet* lastGapPkt=NULL;
	seqno_t lastSeqno=lastGapSeqno;
	int gap=0;
	int fastPushTime1=t;
	for (BOOST_AUTO(itr, media_pkt_to_player_cache_.begin());
		itr!=media_pkt_to_player_cache_.end();
		++itr)
	{
		const media_packet& thePkt=itr->second;
		seqno_t theSeqno=thePkt.get_seqno();
		if (lastSeqno!=theSeqno)
		{
			if (lastGapPkt&&seqno_minus(theSeqno, lastGapSeqno)>10//<10时容易FEC恢复
				)
			{
				timestamp_t theTimestamp=scheduling_->get_timestamp_adjusted(*lastGapPkt);
				fastPushTime1=time_minus(theTimestamp, timeStamp);
				break;
			}
			lastGapPkt=&thePkt;
			lastGapSeqno=theSeqno;
			lastSeqno=theSeqno;
		}
		lastSeqno++;
	}
	fastPushTime1-=500;
	if (speedRate<0.8)
		fastPushTime1-=bound(500, delayGuarantee, 5000);
	double alf=((delayGuarantee>offset)?0.45:0.35);
	int fastPushTime2=bufferHealth*t*alf;
	int fastPushTime=std::min(fastPushTime1, fastPushTime2);
	if (g_b_fast_push_to_player&&false==*g_b_fast_push_to_player)
		fastPushTime=std::min(0, fastPushTime);
	if (is_interactive_category(get_client_param_sptr()->type))
		fastPushTime=0;
	
	is_player_started_=true;
	sento_player_timestamp_offset_= time_minus(timeStamp+fastPushTime , now);
	smallest_seqno_i_care_=pkt.get_seqno();

	MEDIA_DISPATCHER_DBG(
		std::cout<<"---------------fast_push_time--------------------:"
		<<fastPushTime
		<<std::endl;
	);

	return true;
}

void live_media_dispatcher::check_scheduling_health()
{

}

NAMESPACE_END(p2client);
