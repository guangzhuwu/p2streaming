#include "client/client_service.h"
#include "client/client_service_logic.h"
#include "client/stream/media_dispatcher.h"
#include "client/stream/stream_scheduling.h"
#include "client/stream/stream_monitor.h"
#include "common/tsparse.h"

NAMESPACE_BEGIN(p2client);

#if !defined(_SCHEDULING_DBG) && defined(NDEBUG)
#	define  MEDIA_DISPATCHER_DBG(x)
#else 
#	define  MEDIA_DISPATCHER_DBG(x)/* x*/
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


namespace{
#ifdef POOR_CPU
	enum{FEC_MAX_CNT=2};
#else
	enum{FEC_MAX_CNT=4};
#endif
}

media_dispatcher::media_dispatcher(stream_scheduling& scheduling)
	:basic_stream_scheduling(scheduling)
	, is_player_started_(false)
	, delay_play_time_(0)
	, flush_(false)
{
	cas_string_ = security_policy::generate_shared_key(get_client_param_sptr()->channel_uuid, "");
}

void media_dispatcher::stop()
{
	if (flush_&&is_player_started_)
	{
		GUARD_CLIENT_SERVICE_LOGIC(;);

		timestamp_t now=timestamp_now();
		int fecCnt=0;

		for (BOOST_AUTO(itr, media_pkt_to_player_cache_.begin());
			itr!=media_pkt_to_player_cache_.end();
			)
		{
			const media_packet& orgPkt=itr->second;
			seqno_t theSeqno=itr->first;
			if (seqno_less(*smallest_seqno_i_care_, theSeqno))
			{
				if (seqno_less(theSeqno, *smallest_seqno_i_care_))
				{
					media_pkt_to_player_cache_.erase(itr++);
					continue;
				}
				else// if (seqno_greater(theSeqno, *smallest_seqno_i_care_))
				{
					BOOST_ASSERT(seqno_greater(theSeqno, *smallest_seqno_i_care_));
					if(fecCnt++<FEC_MAX_CNT&&fec_decode(now))
					{
						itr=media_pkt_to_player_cache_.begin();
						if (itr->first==*smallest_seqno_i_care_)
							continue;
					}
				}
			}
			if(seqno_greater_equal(theSeqno, *smallest_seqno_i_care_))
			{
				smallest_seqno_i_care_=theSeqno+1;//care的是下一个序号了
				get_client_param_sptr()->smallest_seqno_i_care=*smallest_seqno_i_care_;
			}
			//last_push_time_ = now;
			//last_delay_time_ = theT;
			dispatch(orgPkt, *clientService, *svcLogic);
			media_pkt_to_player_cache_.erase(itr++);
		}

	}
}

void media_dispatcher::start()
{

}

void media_dispatcher::reset()
{
	media_pkt_to_player_cache_.clear();
	fec_media_pkt_cache_.clear();
	txt_pkt_cache_.clear();
	is_player_started_=false;

	smallest_seqno_i_care_.reset();
	smallest_txt_seqno_i_care_.reset();
}

int media_dispatcher::on_timer(timestamp_t now)
{
	int cpuLoad=on_dispatch_timer(now);
	return cpuLoad;
}

double media_dispatcher::get_buffer_health()const
{
	double bufferHealthy=0.0;
	if (!media_pkt_to_player_cache_.empty())
	{
		int delayGuarantee=get_client_param_sptr()->delay_guarantee;
		seqno_t seq1=media_pkt_to_player_cache_.begin()->first;
		seqno_t seq2=media_pkt_to_player_cache_.rbegin()->first;
		int	t=get_buffer_duration(1.0);
		bufferHealthy=std::min(1.0, (double)(t)/(double)delayGuarantee);
		bufferHealthy*=(double)(media_pkt_to_player_cache_.size())
			/std::min<double>(MAX_PKT_BUFFER_SIZE, abs(seqno_minus(seq2, seq1))+1.0);
	}
	BOOST_ASSERT(bufferHealthy<=1.0);
	return std::max(bufferHealthy, 0.0);
}

int media_dispatcher::get_buffer_duration(double bufferHealth)const
{
	int bufferDuration=0;
	if (!media_pkt_to_player_cache_.empty())
	{
		const media_packet& pkt1=(media_pkt_to_player_cache_.begin()->second);
		const media_packet& pkt2=(media_pkt_to_player_cache_.rbegin()->second);
		timestamp_t t1=scheduling_->get_timestamp_adjusted(pkt1);
		timestamp_t t2=scheduling_->get_timestamp_adjusted(pkt2);
		bufferDuration=bufferHealth*time_minus(t2, t1);
	}
	return bufferDuration;
}

void media_dispatcher::set_smallest_seqno_i_care(seqno_t seq)
{
	smallest_seqno_i_care_ = seq;
	while (!media_pkt_to_player_cache_.empty())
	{
		BOOST_AUTO(itr, media_pkt_to_player_cache_.begin());
		if (seqno_less(itr->first, *smallest_seqno_i_care_))
			media_pkt_to_player_cache_.erase(itr);
		else 
			break;
	}
	//if (seqno_less(*bigest_sqno_i_know_, *smallest_seqno_i_care_))
	//	bigest_sqno_i_know_ = *smallest_seqno_i_care_;
}

void media_dispatcher::do_process_recvd_media_packet(const media_packet& pkt)
{
	BOOST_ASSERT(smallest_seqno_i_care_);

	const seqno_t seqno=pkt.get_seqno();
	const int mediaChannel=pkt.get_channel_id();
	const int mediaLevel=pkt.get_level();

	//无论是什么媒体包，都要存储于media_pkt_to_player_cache_中，以便于计算buffer_health
	if(seqno_greater_equal(seqno, *smallest_seqno_i_care_))
		media_pkt_to_player_cache_.insert(std::make_pair(seqno, pkt));

	//处理USER_LEVEL
	if ((mediaLevel==USER_LEVEL&&mediaChannel==TXT_BROADCAST_CHANNEL)//是不是聊天包
		&&(!smallest_txt_seqno_i_care_||seqno_less(*smallest_txt_seqno_i_care_, seqno))//是不是过期
		)
	{
		txt_pkt_cache_.insert(std::make_pair(seqno, pkt));
		if (!smallest_txt_seqno_i_care_)
			smallest_txt_seqno_i_care_=seqno;
	}
	else if ((mediaLevel==SYSTEM_LEVEL&&mediaChannel==FEC_MEDIA_CHANNEL))//这是FEC数据包
	{//使用了(SYSTEM_LEVEL)是为了不支持FEC的老版本也能工作
		while(!fec_media_pkt_cache_.empty())
		{
			BOOST_AUTO(itr, fec_media_pkt_cache_.begin());
			if (seqno_less(itr->first, *smallest_seqno_i_care_))
				fec_media_pkt_cache_.erase(itr);
			else
				break;
		}
		fec_media_pkt_cache_.insert(std::make_pair(seqno, pkt));
	}
}

int media_dispatcher::on_dispatch_timer(timestamp_t now)
{
	typedef media_packet_map::iterator iterator;

	if (media_pkt_to_player_cache_.empty())
	{
		check_scheduling_health();
		return 0;
	}

	GUARD_CLIENT_SERVICE_LOGIC(0);

	bool first_start=!is_player_started_;
	double bufferHealth=get_buffer_health();

	//文本
	for (iterator itr=txt_pkt_cache_.begin(), end=txt_pkt_cache_.end(); itr!=end; ++itr)
	{
		const media_packet& pkt=itr->second;
		smallest_txt_seqno_i_care_=itr->first;//pkt.get_seqno();
		BOOST_ASSERT(pkt.get_seqno()==itr->first);
		if (buffer_size(pkt.buffer())>=media_packet::format_size())
			dispatch(pkt, *clientService, *svcLogic);
	}
	txt_pkt_cache_.clear();

	//流媒体数据
	if (!is_player_started_&&!(is_player_started_=can_player_start(bufferHealth, now)))
		return 0;

	if (first_start)
		player_start_time_=now;

	int averageSrcPacketRate=scheduling_->get_average_src_packet_rate();
	const int MAX_PUSH=(first_start?100
		:(averageSrcPacketRate*6*time_minus(now, last_send_to_player_timestamp_)/1000)
		);

	int overstockedSize=svcLogic->overstocked_to_player_media_size();
	int fecCnt=0, pushedCnt=0;
	int maxFrontTime=MAX_PRE_PUSH_TIME;//ms

	MEDIA_DISPATCHER_DBG(;boost::optional<timestamp_t> lastTime;);
	for (iterator itr=media_pkt_to_player_cache_.begin();
		itr!=media_pkt_to_player_cache_.end();
		)
	{
		if ((overstockedSize>=OVERSTOCKED_SIZE)&&(pushedCnt>1))
			break;
		const media_packet& orgPkt=itr->second;
		seqno_t theSeqno=itr->first;
		if (!be_about_to_play(orgPkt, bufferHealth, overstockedSize, now))
		{
			check_scheduling_health();
			break;
		}

		if (seqno_less(theSeqno, *smallest_seqno_i_care_))
		{
			media_pkt_to_player_cache_.erase(itr++);
			continue;
		}
		else if (seqno_less(*smallest_seqno_i_care_, theSeqno))
		{
			maxFrontTime=-1;

			BOOST_ASSERT(seqno_greater(theSeqno, *smallest_seqno_i_care_));
			if(fecCnt++<FEC_MAX_CNT&&fec_decode(now))
			{
				itr=media_pkt_to_player_cache_.begin();
				if (itr->first==*smallest_seqno_i_care_)
					continue;
			}

			BOOST_ASSERT(scheduling_->get_packet_info(*smallest_seqno_i_care_));

			MEDIA_DISPATCHER_DBG(;
			seqno_t seqCare=*smallest_seqno_i_care_;
			const absent_packet_info* pktInfo=scheduling_->get_packet_info(seqCare);
			std::cout<<"!!!!!!!!---lost:"<<seqCare;
			if (pktInfo)
			{
				std::cout<<" isThis="<<pktInfo->is_this(seqCare, now)
					<<" inAbsend="<<(scheduling_->get_absent_packet_list().find(seqCare))
					<<" ownerCnt="<<pktInfo->m_owners.size()
					<<" rto="<<time_minus(pktInfo->m_pull_outtime, pktInfo->m_pull_time);
			}

			//<<" pullCnt="<<pktInfo->m_pull_cnt<<" ";
			/*BOOST_FOREACH(const endpoint& edp, pktInfo->m_pull_edps)
			{
			std::cout<<edp<<" ";
			}*/
			std::cout<<scheduling_->local_is_seed()<<"\n";
			BOOST_ASSERT(!pktInfo||pktInfo->is_this(seqCare, now));
			);
		}

		//if (seqno_less(*smallest_seqno_i_care_, theSeqno)
		//	&&delay_play_time_<5*1000
		//	&&time_minus(now, player_start_time_)>5*1000
		//	)
		//{
		//	delay_play_time_+=25;
		//	sento_player_timestamp_offset_-=25;
		//	break;
		//}

		if(seqno_greater_equal(theSeqno, *smallest_seqno_i_care_))
		{
			smallest_seqno_i_care_=theSeqno+1;//care的是下一个序号了
			get_client_param_sptr()->smallest_seqno_i_care=*smallest_seqno_i_care_;

			MEDIA_DISPATCHER_DBG(;
			if (lastTime)
				BOOST_ASSERT(time_less_equal(*lastTime, orgPkt.get_time_stamp()));
			lastTime=orgPkt.get_time_stamp();
			);
		}

		overstockedSize+=orgPkt.buffer().length();
		//last_push_time_ = now;
		//last_delay_time_ = theT;
		dispatch(orgPkt, *clientService, *svcLogic);

		media_pkt_to_player_cache_.erase(itr++);

		if (++pushedCnt>MAX_PUSH)
			break;
	}

	if(pushedCnt > 0)
		last_send_to_player_timestamp_=now;

	return pushedCnt*5;
}

void media_dispatcher::dispatch_system_level_data(safe_buffer data, client_service& svc)
{
	safe_buffer_io io(&data);
	message_t msgType;
	io>>msgType;
	if (tracker_peer_msg::room_info==msgType)
	{
		svc.on_recvd_room_info(data);
	}
	else
	{
		//assert(0&&"临时除room_info还没有其它的SYSTEM_LEVEL_BROADCAST_MSG");
	}
}

void media_dispatcher::dispatch(const media_packet& orgPkt, client_service& svc, 
								client_service_logic_base& svcLogic)
{
	bool notTooLate=true;//is_vod()?true:((theT>-1500)||first_start);
	seqno_t theSeqno=orgPkt.get_seqno();

	scheduling_->get_stream_monitor().push_to_player(theSeqno, notTooLate);

	int channelID=orgPkt.get_channel_id();
	int level=orgPkt.get_level();
	if (TXT_BROADCAST_CHANNEL==channelID&&USER_LEVEL==level)
	{
		//do nothing，因为前面已经通过txt_pkt_cache_处理了
	}
	if (FEC_MEDIA_CHANNEL==channelID&&SYSTEM_LEVEL==level)
	{
		//do nothing
	}
	else
	{
		//last_delay_time_ = theT;

		media_packet pkt(orgPkt.buffer().clone());//科隆一份，用来解cas
		security_policy::cas_mediapacket(pkt, cas_string_);

		level=pkt.get_level();

		if (level==SYSTEM_LEVEL)
		{
			dispatch_system_level_data(pkt.payload(), svc);
		}
		else
		{
			MEDIA_DISPATCHER_DBG(;
			unsigned char* data=buffer_cast<unsigned char*>(pkt.payload());
			ts_t ts;
			if (ts_parse().exist_keyframe(data, pkt.payload().size(), &ts)>0)
			{
				std::cout<<"-----------I Frame--------------------:"<<pkt.get_seqno()<<std::endl;
			}

			);
			dispatch_media_packet(pkt, svcLogic);
		}
	}
}

bool media_dispatcher::fec_decode(timestamp_t now, seqno_t theLostSeqno)
{
	safe_buffer decodeResult; 
	if(packet_fec_decoder_(theLostSeqno, scheduling_->get_memory_packet_cache(), 
		fec_media_pkt_cache_, cas_string_, now, decodeResult)
		)
	{
		//解码成功，插入到队列
		scheduling_->read_media_packet_from_nosocket(decodeResult, error_code(), theLostSeqno);
		{
			MEDIA_DISPATCHER_DBG(;
			if (get_min_seqno_in_buffer()==theLostSeqno)
			{
				static int fec_success=0;
				++fec_success;
				std::cout<<"fec_success:---------------------------------------------------------:"
					<<fec_success<<"\n";
			};
			);
			return true;
		}
	}
	return false;
}


NAMESPACE_END(p2client);

