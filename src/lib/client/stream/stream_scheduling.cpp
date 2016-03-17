#include "client/peer_connection.h"
#include "client/client_service.h"
#include "client/client_service_logic.h"
#include "client/tracker_manager.h"
#include "client/cache/cache_service.h"
#include "client/stream/stream_scheduling.h"
#include "client/stream/stream_topology.h"
#include "client/stream/stream_monitor.h"

#include "client/stream/vod_media_dispatcher.h"
#include "client/stream/live_media_dispatcher.h"

NAMESPACE_BEGIN(p2client);

#if !defined(_SCHEDULING_DBG) && defined(NDEBUG)
#	define SCHEDULING_DBG(x)
#else 
#	define SCHEDULING_DBG(x) x
#endif

#define ONLY_PULL 0
#define DISABLE_SERVER_PUSH 0
#ifndef POOR_CPU
#	define SMOOTH_REQUEST  1
#	define SMOOTH_RESPONSE 1
#else
#	define SMOOTH_REQUEST 0
#	define SMOOTH_RESPONSE 0
#endif

#define GUARD_TOPOLOGY(returnValue) \
	BOOST_AUTO(topology, topology_.lock());\
	if (!topology) {return returnValue;}

#define GUARD_CLIENT_SERVICE(returnValue)\
	GUARD_TOPOLOGY(returnValue)\
	BOOST_AUTO(clientService, topology->get_client_service());\
	if (!clientService) {return returnValue;}

#define GUARD_CLIENT_SERVICE_LOGIC(returnValue)\
	GUARD_CLIENT_SERVICE(returnValue)\
	BOOST_AUTO(svcLogic, clientService->get_client_service_logic());\
	if (!svcLogic) {return returnValue;}

namespace{
	static const int MIN_PACKET_RATE = 50;
	static const int MAX_PACKET_RATE = 300;

	static const int OVERSTOCKED_SIZE = 1024 * 1024;

	static const int QUALITY_REPORT_TIMER_INTERVAL = 50 * 1000;
	static const int INFO_REPORT_TIMER_INTERVAL = 5 * 1000;

	static double time_adjust_coefficient = 1.0;//时间校对系数（有些机器的时钟走不准，6这很难相信，但的确是事实）

#ifdef POOR_CPU
	static const int MEDIA_CONFIRM_INTERVAL=400;
#else
	static const int MEDIA_CONFIRM_INTERVAL = 200;
#endif
	enum { MAX_EXCHANGE_TIME = 80 * 1000 };

	inline void dummy_func_1(const error_code& ec)
	{
		SCHEDULING_DBG(
			if (ec)
				std::cout << "????????????dummy_func_1 error: " << ec.message();
		);
	}
	inline void dummy_func_2(size_t, const error_code&ec)
	{
		SCHEDULING_DBG(
			if (ec)std::cout << "????????????dummy_func_2 error: " << ec.message();
		);
	}
}

const double stream_scheduling::URGENT_TIME = 0.5;
const double stream_scheduling::DEFINIT_URGENT_DEGREE = 0.35;
const double stream_scheduling::LOST_RATE_THRESH = 0.45;//remote to local 超过这个丢包率就不倾向于请求流了
const double stream_scheduling::AGGRESSIVE_THREASH = 0.10;//丢包超过这个就认为需要和别人抢带宽
#ifdef POOR_CPU
const int stream_scheduling::PULL_TIMER_INTERVAL=150;//msec
#else
const int stream_scheduling::PULL_TIMER_INTERVAL = 100;//msec
#endif

struct is_subscriptable{
	/*
	* -------------GroupID------------------------>
	* |
	* | [0]  4  8   12  16  20  24  28
	* S  1   5 [9]  13  17  21  25  29
	* u  2   6  10  14 [18] 22  26  30
	* b  3   7  11  15  19  23 [27] 31
	* |
	* V
	*/
	//子流分割，每行是一个子流，每一列是一个group。
	//用[]标注的是子流订阅标识seqno。
	//is_subscriptable用来确定一个seqno是不是订阅标识。
	is_subscriptable(int SubstreamCnt, int GroupCnt)
		:SUBSTREAM_CNT_(SubstreamCnt)
		, GROUP_CNT_(GroupCnt)
		, len_(SubstreamCnt*GroupCnt)
		, subscriptable_id_(SubstreamCnt)
	{
		typedef boost::make_unsigned<seqno_t>::type unsigned_seqno_t;
		//如果seqno_t不是足够大，则
		//订阅组长度必须满足整除条件，否则在从 ~seqno_t(0)回卷到0时候会造成问题
		BOOST_ASSERT(
			sizeof(seqno_t) >= 8
			||
			(static_cast<boost::uint64_t>(~unsigned_seqno_t(0)) + 1) % (SubstreamCnt*GroupCnt) == 0
			);
		double r = (double)GroupCnt / (double)SubstreamCnt;
		int maxA = -1;
		for (int i = 0; i < SubstreamCnt; ++i)
		{
			int a = static_cast<int>(i*r);
			subscriptable_id_[i] = (i + (a%GroupCnt)*SubstreamCnt);
		}
	}

	bool operator()(seqno_t id)const
	{
		seqno_t subID = id%SUBSTREAM_CNT_;
		return subscriptable_id_[subID] == id%len_;
	}
	int SUBSTREAM_CNT_, GROUP_CNT_, len_;
	std::vector<int>subscriptable_id_;
};

void stream_scheduling::register_message_handler(peer_connection* conn)
{
#define REGISTER_HANDLER(msgType, handler)\
	conn->register_message_handler(msgType, bind(&this_type::handler, this, conn, _1));

	if (!conn)
		return;
	REGISTER_HANDLER(global_msg::media, on_recvd_media_packet);
	REGISTER_HANDLER(global_msg::pulled_media, on_recvd_pulled_media_packet);
	REGISTER_HANDLER(global_msg::pushed_media, on_recvd_pushed_media_packet);
	REGISTER_HANDLER(global_msg::media_sent_confirm, on_recvd_media_confirm);
	REGISTER_HANDLER(global_msg::no_piece, on_recvd_no_piece);
	REGISTER_HANDLER(global_msg::media_request, on_recvd_media_request);
	REGISTER_HANDLER(server_peer_msg::be_seed, on_recvd_be_seed);
	REGISTER_HANDLER(server_peer_msg::be_super_seed, on_recvd_be_super_seed);
	REGISTER_HANDLER(server_peer_msg::piece_notify, on_recvd_piece_notify);
	REGISTER_HANDLER(server_peer_msg::recommend_seed, on_recvd_recommend_seed);
	REGISTER_HANDLER(peer_peer_msg::buffermap_exchange, on_recvd_buffermap_exchange);
	REGISTER_HANDLER(peer_peer_msg::buffermap_request, on_recved_buffermap_request);
	REGISTER_HANDLER(peer_peer_msg::media_subscription, on_recvd_media_subscription);
	REGISTER_HANDLER(peer_peer_msg::media_unsubscription, on_recvd_media_unsubscription);
#undef REGISTER_HANDLER
}

stream_scheduling::stream_scheduling(topology_sptr tplgy, bool justBeProvider)
	:scheduling_base(tplgy->get_io_service(), tplgy->get_client_param_sptr())
	, just_be_provider_(justBeProvider)
	, topology_(tplgy)
	, smoother_(milliseconds(600), milliseconds(350), milliseconds(SMOOTH_TIME),
	milliseconds(SMOOTH_TIME), milliseconds(SMOOTH_TIME), 64 * 1024 / 8,
	get_io_service()
	)
	, in_substream_(SUBSTREAM_CNT)
	, be_super_seed_(false)
	, is_in_subscription_state_(false)
	, in_subscription_state_confirm_(0)
	, global_local_to_remote_speed_(-1)
	, src_packet_rate_(60)
	, out_substream_(SUBSTREAM_CNT)
	, hop_(-1)
	, substream_hop_(SUBSTREAM_CNT, -1)
{
	GUARD_CLIENT_SERVICE(;);
	timestamp_t now = timestamp_now();
	scheduling_start_time_ = now;
	last_subscrib_time_ = now - 5000;
	last_average_subscrib_rtt_calc_time_ = now - 1000;
	last_get_global_local_to_remote_speed_time_ = now - 5000;
	last_media_confirm_time_ = now;
	last_exchange_buffermap_time_ = now;
	delay_guarantee_ = bound(MIN_DELAY_GUARANTEE,
		get_client_param_sptr()->delay_guarantee, MAX_DELAY_GUARANTEE);
	is_in_subscription_state_ = is_interactive_category(get_client_param_sptr()->type)
		|| get_client_param_sptr()->back_fetch_duration <= 500;
	seqnomap_buffer_.reserve(1024);

	int64_t filmLenth = -1;
	if (is_vod())
	{
		BOOST_AUTO(vodInfo, clientService->get_vod_channel_info());
		BOOST_ASSERT(vodInfo);
		filmLenth = vodInfo->film_length();
		std::pair<seqno_t, seqno_t> seqRange =
			get_seqno_range(get_client_param_sptr()->offset, filmLenth);
		min_seqno_ = seqRange.first;
		max_seqno_ = seqRange.second;
		average_packet_rate_ = (max_seqno_ + 1) * 1000 / vodInfo->film_duration();

		if (*average_packet_rate_ < 1.0)
			average_packet_rate_ = 80;

		SCHEDULING_DBG(
			std::cout << "average_packet_rate_=" << *average_packet_rate_ << std::endl;
		);
		media_dispatcher_ = vod_media_dispatcher::create(*this);
	}

	buffer_manager_.reset(new buffer_manager(get_io_service(), get_client_param_sptr(), filmLenth));
	stream_seed_ = stream_seed::create(*this);
	scheduling_strategy_ = heuristic_scheduling_strategy::create(*this);
	stream_monitor_.reset(new stream_monitor(*this));

	if (!is_vod())
	{
		media_dispatcher_ = live_media_dispatcher::create(*this);

		tracker_manager_sptr tracker = clientService->get_tracker_handler();
		if (tracker)
		{
			BOOST_AUTO(liveInfo, clientService->get_live_channel_info());
			if (liveInfo)
			{
				const peer_info& local_info = get_client_param_sptr()->local_info;
				long timeDiff = time_minus((timestamp_t)local_info.join_time(), liveInfo->server_time());
				if (timeDiff >= 0 && timeDiff <= get_client_param_sptr()->back_fetch_duration / 5)
				{
					src_packet_rate_ = bound<double>(MIN_PACKET_RATE,
						liveInfo->server_packet_rate(), MAX_PACKET_RATE);
					__init_seqno_i_care(liveInfo->server_seqno() + src_packet_rate_*timeDiff / 1000);
					SCHEDULING_DBG(
						std::cout << "-----stream_scheduling---constructor __init_seqno_i_care used --\n" << std::endl;
					);
				}
			}
		}
	}
}

stream_scheduling::~stream_scheduling()
{
	__stop();
}
void stream_scheduling::require_update_server_info()
{
	GUARD_CLIENT_SERVICE(;);
	clientService->update_server_info();
}
void stream_scheduling::restart()
{
	GUARD_CLIENT_SERVICE(;);
	get_io_service().post(boost::bind(&client_service::restart, clientService));
	topology_.reset();
}

void stream_scheduling::request_perhaps_fail(seqno_t seqno, peer_connection* inchargePeer)
{
	absent_packet_info* pktInfo = get_packet_info(seqno);
	if (!pktInfo)
		return;
	timestamp_t now = timestamp_now();
	timestamp_t outTime = now + 50;
	if (time_minus(now, pktInfo->m_pull_time) > 1000
		&& time_greater(pktInfo->m_pull_outtime, outTime)
		&& pktInfo->m_peer_incharge.lock().get() == inchargePeer
		)
	{
		pktInfo->m_pull_outtime = outTime;
	}
}

void stream_scheduling::reset_scheduling(uint8_t* serverSession)
{
	timestamp_t now = timestamp_now();

	if (serverSession)
		server_session_id_ = *serverSession;
	else
		server_session_id_.reset();

	if (is_pull_start())
		stream_monitor_.reset(new stream_monitor(*this));

	if (media_dispatcher_)
		media_dispatcher_->reset();

	if (stream_seed_)
		stream_seed_->reset();

	if (buffer_manager_)
		buffer_manager_->reset();

	scheduling_start_time_ = now;
	recvd_first_packet_time_.reset();
	is_in_subscription_state_ = is_interactive_category(get_client_param_sptr()->type)
		|| get_client_param_sptr()->back_fetch_duration <= 500;
	in_subscription_state_confirm_ = 0;
	be_super_seed_ = false;

	smoother_.reset();
	for (size_t i = 0; i < out_substream_.size(); ++i)
		out_substream_[i].clear();
}

void stream_scheduling::start()
{
	GUARD_CLIENT_SERVICE(;);

	scheduling_start_time_ = timestamp_now();
	last_pull_time_ = scheduling_start_time_ + random(0, 50);
	last_quality_report_time_ = scheduling_start_time_ + random(100, 200);
	last_info_report_time_ = scheduling_start_time_ + random(100, 200);
	//启动各定时器
	if (!timer_)
	{
		timer_ = timer::create(get_io_service());
		timer_->set_obj_desc("p2client::stream_scheduling::timer_");
		timer_->register_time_handler(boost::bind(&this_type::on_timer, this));
	}
	else
	{
		timer_->cancel();
	}
	timer_->async_keep_waiting(millisec(3), milliseconds(20));

	set_play_offset(get_client_param_sptr()->offset);
}

void stream_scheduling::set_play_offset(int64_t offset)
{
	//计算偏移
	if (is_vod())
	{
		get_client_param_sptr()->offset = offset;
		set_play_seqno((seqno_t)((offset) / PIECE_SIZE));
	}
}

bool stream_scheduling::is_in_outgoing_substream(const peer_connection* conn,
	int substreamID)const
{
	if (!out_substream_[substreamID].empty())
	{
		for (BOOST_AUTO(itr, out_substream_[substreamID].begin());
			itr != out_substream_[substreamID].end();
			++itr)
		{
			if (itr->lock().get() == conn)
				return true;
		}
	}
	return false;
}

bool stream_scheduling::is_in_outgoing_substream(const peer_connection* conn)const
{
	for (int substreamID = 0; substreamID < SUBSTREAM_CNT; ++substreamID)
	{
		if (is_in_outgoing_substream(conn, substreamID))
		{
			return true;
		}
	}
	return false;
}

void stream_scheduling::set_play_seqno(seqno_t seqno)
{
	BOOST_ASSERT(is_vod());
	reset_scheduling();
	__init_seqno_i_care(seqno);
}

void stream_scheduling::stop(bool flush)
{
	__stop(flush);
}

void stream_scheduling::__stop(bool flush)
{
	if (media_dispatcher_)
	{
		media_dispatcher_->set_flush(flush);
		media_dispatcher_->stop();
	}
	if (stream_seed_)
		stream_seed_->stop();
	if (scheduling_strategy_)
		scheduling_strategy_->stop();
	if (timer_)
		timer_->cancel();

	SCHEDULING_DBG(
		std::cout << "************************channel closed" << std::endl;
	);
}

int stream_scheduling::average_subscribe_rtt(timestamp_t now)
{
	if (!is_time_passed(1000, last_average_subscrib_rtt_calc_time_, now))//500ms
		return average_subscrib_rtt_;
	last_average_subscrib_rtt_calc_time_ = now;

	int rttSum = 0;
	int n = 0;
	for (size_t i = 0; i < in_substream_.size(); ++i)
	{
		peer_connection_sptr conn(in_substream_[i].lock());
		if (conn&&conn->is_connected())
		{
			peer_sptr p(conn->get_peer());
			if (p)
			{
				int t = p->rtt();
				if (t < 2000 && t>0)
				{
					++n;
					rttSum += t;
				}
			}
		}
	}
	enum{ DEFAULT_RTT = 500 };
	if (0 == n)
		average_subscrib_rtt_ = DEFAULT_RTT;
	else
		average_subscrib_rtt_ = rttSum / n;

	return average_subscrib_rtt_;
}

boost::optional<seqno_t> stream_scheduling::remote_urgent_seqno(const peer_sptr& p, timestamp_t now)
{
	boost::optional<seqno_t> urgent_seqno;
	if (get_smallest_seqno_i_care())
	{
		enum{ urgent_time = 2500 };//millisec
		timestamp_t timestamp;
		int pktRate = src_packet_rate_;
		seqno_t minSeqnoLocalCare = *get_smallest_seqno_i_care();
		if (p->known_current_playing_timestamp()
			&& guess_timestamp(now, minSeqnoLocalCare, timestamp, &pktRate)
			)
		{
			long timeDiff = time_minus(p->current_playing_timestamp(now), timestamp);
			urgent_seqno = minSeqnoLocalCare + pktRate*(timeDiff + urgent_time) / 1000;
		}
		else
		{
			urgent_seqno = minSeqnoLocalCare + pktRate*urgent_time / 1000;
		}
	}
	return urgent_seqno;
}

void stream_scheduling::send_media_packet(peer_connection_sptr conn, seqno_t seq, bool push,
	int smoothDelay)
{
	if (!conn->is_connected())
		return;

	timestamp_t now = timestamp_now();
	int cpuUsage = get_sys_cpu_usage(now);

	peer_sptr p = conn->get_peer();
	boost::optional<seqno_t> urgent_seqno;
	bool confirmImediatelly = false;

	if (global_local_to_remote_speed_ < 0
		|| is_time_passed(500, last_get_global_local_to_remote_speed_time_, now)
		)
	{
		global_local_to_remote_speed_ = (int)global_local_to_remote_speed();
		last_get_global_local_to_remote_speed_time_ = now;
	}

	media_packet pkt;
	if ((cpuUsage < 85 && global_local_to_remote_speed_ < MAX_CLINET_UPLOAD_SPEED
		|| cpuUsage < 90 && in_probability(0.4)
		|| cpuUsage < 95 && (urgent_seqno = remote_urgent_seqno(p, now)) && seqno_less(seq, *urgent_seqno)
		)
		&& get_memory_packet_cache().get(pkt, seq)
		)
	{
		//最初设计没有考虑周全：media_packet中的safe_buffer是被shared，发送media消息前
		//虽然设置is_push状态正确，但可能因为发送前发给其他节点的同样一个media_packet修改
		//is_push状态而造成错误。
		//所以，这里直接使用message_type来表达清楚到底是pushed还是pulled。
		message_type msgType = global_msg::media;
		if (p->get_peer_info().has_version() && p->get_peer_info().version() >= 1.0)
		{
			msgType = (push ? global_msg::pushed_media : global_msg::pulled_media);
		}
		modify_media_packet_header_before_send(pkt, conn.get(), push);
		conn->async_send_unreliable(pkt.buffer(), msgType);

		stream_monitor_->outgoing_media_packet(pkt.buffer().length(), push);
		p->on_download_from_local(pkt.buffer().length(), seq);
	}
	else
	{
		//p->last_media_confirm_time()=now-1000;
	}
	p->media_download_from_local().push_back(seq);

	/*
	for (int i=0;i<msg.seqno_size();++i)
	{
	media_pkt_seq_t seqno((media_pkt_seq_t)msg.seqno(i));
	//以一定概率响应，丢包率越高，响应概率越低
	if (LocalToRemoteLostRate>LOST_RATE_THRESH
	&&in_probability(0.9+LocalToRemoteLostRate)
	)
	{
	SCHEDULING_DBG(
	std::cout<<"global_local_to_remote_lost_rate()="
	<<global_local_to_remote_lost_rate()
	<<", conn->local_to_remote_lost_rate()="
	<<LocalToRemoteLostRate
	<<std::endl;
	);
	p->media_have_sent().push_back(seqno);
	confirmImediatelly=true;
	continue;
	}
	if (recvd_media_packets_.get(pkt, seqno))
	{
	pkt.set_is_push(0);
	modify_media_packet_header_before_send(pkt, conn, false);
	conn->async_send_unreliable(pkt.buffer(), global_msg::media);
	stream_monitor_->outgoing_media_packet(pkt.buffer().length());
	}
	else
	{
	p->media_have_sent().push_back(seqno);
	confirmImediatelly=true;
	BOOST_ASSERT(0&&"buffermap, bugggggggggggggggggggggggggggggggggggg");
	}
	}
	if(confirmImediatelly)
	{
	timestamp_t now=timestamp_now();
	p->last_media_confirm_time()=now-milliseconds(1000);//使得confirm检查时候以为已经1s没有发送confirm了，以至于马上进行confirm
	}
	*/
}

void stream_scheduling::do_subscription_push(media_packet&pkt, timestamp_t now)
{
	seqno_t seqno = pkt.get_seqno();
	int subStreamID = seqno%out_substream_.size();
	timestamp_t pkt_timeStamp = get_timestamp_adjusted(pkt);
	//如果是直接推送或是延迟较小或是seed节点，则需要发送给子流订阅者
	int sendCnt = 0;
	if (!out_substream_[subStreamID].empty())
	{
		int backfetchMsec = backfetch_msec();
		int defaultDelay = std::max(backfetchMsec / 3, 1000 + get_client_param_sptr()->max_push_delay);
		const absent_packet_info* pktInfo = get_packet_info(seqno);
		pkt.set_is_push(1);
		BOOST_AUTO(itr, out_substream_[subStreamID].begin());
		for (; itr != out_substream_[subStreamID].end();)
		{
			peer_connection_sptr childPeerConn = itr->lock();
			peer *p = NULL;
			if (childPeerConn&&childPeerConn->is_connected() && (p = childPeerConn->get_peer().get()))
			{
#ifdef SMOOTH_RESPONSE
				int cnt = smoother_.size();
#else
				int cnt=sendCnt;
#endif
				double localToRemoteLostRate = (childPeerConn->local_to_remote_lost_rate());
				const boost::optional<int>&averageDelay = p->average_push_to_remote_delay(subStreamID);
				const int maxDelay = p->max_push_to_remote_delay(subStreamID);
				bool knownPlayingTimestamp = p->known_current_playing_timestamp();
				int preTime = knownPlayingTimestamp ? time_minus(pkt_timeStamp, p->current_playing_timestamp(now)) : defaultDelay;
				int delayToHim = knownPlayingTimestamp ? (backfetchMsec - preTime) : defaultDelay;
				//BOOST_ASSERT(!knownPlayingTimestamp||abs(preTime)<100*1000);
				if (
					(!knownPlayingTimestamp
					|| preTime > std::min(urgent_time() + 1000, 2 * backfetchMsec / 3)
					|| (!averageDelay || maxDelay<0 || delayToHim < maxDelay)//(*averageDelay + get_client_param_sptr()->max_push_delay)) //&& preTime>std::min(urgent_time() - 1000, 2 * backfetchMsec / 3)
					)
					&& (localToRemoteLostRate < LOST_RATE_THRESH || in_probability(1.0 - localToRemoteLostRate))
					&& !p->is_ignored_subscription_seqno(seqno, subStreamID)
					&& (!pktInfo || !pktInfo->is_this(seqno, now) || !pktInfo->m_owners.find(childPeerConn))//对方的确没有这个片段
#ifdef POOR_CPU
					&&(cnt<2||cnt<5&&in_probability(0.5))
#elif !defined(WIN32)
					&&(cnt<10||cnt<15&&in_probability(0.5))
#endif
					&&get_sys_cpu_usage(now) < 95
					)
				{
					p->on_push_to_remote(delayToHim, subStreamID);
#ifdef SMOOTH_RESPONSE
					smoother_.push(childPeerConn->local_id(),
						boost::bind(&this_type::send_media_packet, this,
						childPeerConn, seqno, true, _1),
						1500
						);
#else
					send_media_packet(childPeerConn, seqno, true, 0);
					sendCnt++;
#endif
				}
				else
				{
					p->media_download_from_local().push_back(seqno);

					SCHEDULING_DBG(
						static timestamp_t last_print_time = timestamp_now();
					if (!p->is_ignored_subscription_seqno(seqno, subStreamID))
					{
						last_print_time = now;
						std::cout << "???????????------not-push-to-him?"
							<< " PRETIME_THRESH=" << std::min(urgent_time() + 1000, 2 * backfetchMsec / 3)
							<< ", preTime=" << preTime
							<< ", delayToHim=" << delayToHim
							<< ", maxSubDelay=" << (int)(maxDelay)
							<< ", averageSubDelay=" << (int)(averageDelay ? (*averageDelay) : -1111111)
							<< ", lostRate=" << localToRemoteLostRate
							<< ", ignored=" << p->is_ignored_subscription_seqno(seqno, subStreamID)
							<< ", find?=" << (pktInfo&&pktInfo->m_owners.find(childPeerConn) != NULL)
							<< "\n";
					}
					);
				}
				++itr;
			}
			else
			{
				out_substream_[subStreamID].erase(itr++);
			}
		}
		/*SCHEDULING_DBG(
		std::count<<"&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&:"<<smoother().size()<<"\n";
		)*/
	}

}

void stream_scheduling::on_recved_buffermap_request(peer_connection* conn,
	safe_buffer buf)
{
	p2p_buffermap_request_msg msg;
	if (!parser(buf, msg))
	{
		return;
	}

	seqno_t minSeq = msg.min_seqno();
	seqno_t maxSeq = msg.max_seqno();
	int size = seqno_minus(maxSeq, minSeq);

	buffermap_exchange_msg_.Clear();
	buffermap_info* mutableBufferMap = buffermap_exchange_msg_.mutable_buffermap();
	buffer_manager_->get_memory_packet_cache_buffermap(mutableBufferMap, size);
	if (is_vod())
	{
		buffer_manager_->get_disk_packet_cache_buffermap(mutableBufferMap,
			get_client_param_sptr()->channel_uuid);
	}

	conn->async_send_unreliable(serialize(buffermap_exchange_msg_),
		peer_peer_msg::buffermap_exchange);
}

void stream_scheduling::on_recvd_buffermap_exchange(peer_connection* conn,
	safe_buffer buf)
{
	if (!get_smallest_seqno_i_care())
		return;

	buffermap_exchange_msg msg;
	if (!parser(buf, msg))
		return;

	timestamp_t now = timestamp_now();

	if (msg.has_current_playing_timestamp())
	{
		peer* p = conn->get_peer().get();
		//早期版本有个bug, 链接初期没有发送buffermap，这里只好猜测写入
		if (!conn->is_passive()//主动连接会发送handshake, 其中携带了buffermap
			&& !p->known_current_playing_timestamp()
			&& msg.buffermap().bitset().size() < 20)
		{
			p2p_buffermap_request_msg requestMsg;
			requestMsg.set_min_seqno(*get_smallest_seqno_i_care() + 32);
			requestMsg.set_max_seqno(*get_smallest_seqno_i_care() + 32 + 768);
			conn->async_send_semireliable(serialize(requestMsg), peer_peer_msg::buffermap_request);
		}
		p->set_current_playing_timestamp(msg.current_playing_timestamp(), now);
	}

	//只有通过buffermap_exchange获知的piece才加入owner
	const buffermap_info& mapInfo = msg.buffermap();
	buffer_manager_->process_recvd_buffermap(mapInfo, in_substream_, conn,
		*get_smallest_seqno_i_care(), now, true);

	if (mapInfo.has_erased_seq_begin() && mapInfo.has_erased_seq_end())
	{
		buffer_manager_->process_erased_buffermap(conn, mapInfo.erased_seq_begin(),
			mapInfo.erased_seq_end(), *get_smallest_seqno_i_care(), now);
	}
}

void stream_scheduling::on_recvd_media_confirm(peer_connection* conn, safe_buffer buf)
{
	media_sent_confirm_msg msg;
	if (!parser(buf, msg))
		return;

	if (!get_smallest_seqno_i_care())
		return;

	peer* p = conn->get_peer().get();
	if (!p)return;

	timestamp_t now = timestamp_now();
	int minTime = std::max(250, p->rtt() - p->rtt_var());//int minTime=p->rtt()+p->rtt_var();
	const seqno_t smallest_seqno_i_care = *get_smallest_seqno_i_care();
	seqno_t processedSeqno = smallest_seqno_i_care;
	for (int i = 0; i < msg.seqno_size(); ++i)
	{
		const seqno_t seqno = msg.seqno(i);
		if (seqno < smallest_seqno_i_care || seqno == processedSeqno)
		{
			continue;
		}
		processedSeqno = seqno;

		bool inCache = get_memory_packet_cache().has(seqno, now) != packet_buffer::NOT_HAS;
		if (inCache)
			continue;;

		enum{ TIME = 50 };
		absent_packet_info* pktInfo = get_packet_info(seqno);
		if (!pktInfo)
		{
			get_absent_packet_list().just_known(seqno, smallest_seqno_i_care, now);
			pktInfo = get_packet_info(seqno);
			if (pktInfo)
				get_absent_packet_list().request_failed(seqno, now, TIME);
		}
		else if (pktInfo
			&&!pktInfo->m_must_pull
			&&pktInfo->is_this(seqno, now)
			&& (time_greater(pktInfo->m_pull_time, now) && (is_in_incoming_substream(conn, seqno%SUBSTREAM_CNT) || p->is_server() && be_super_seed_)
			|| (time_minus(now, pktInfo->m_pull_time) / (double)minTime) > 0.5 && (pktInfo->m_peer_incharge.lock().get() == conn)
			)//推送流或者是较长时间前的请求流
			)
		{
			get_absent_packet_list().request_failed(seqno, now, TIME);
		}
		else
		{
			get_absent_packet_list().just_known(seqno, smallest_seqno_i_care, now);

			SCHEDULING_DBG(
				if (pktInfo&&!pktInfo->m_must_pull)
				{
				std::cout << "-------??------recvd_media_confirm, but useless---"
					<< " seqno=" << seqno
					<< ", t=" << time_minus(now, pktInfo->m_pull_time)
					<< ", rtt=" << minTime
					<< ", t/rtt=" << time_minus(now, pktInfo->m_pull_time) / double(minTime)
					<< ", is_this:" << pktInfo->is_this(seqno, now)
					<< ", has:" << get_memory_packet_cache().has(seqno)
					<< ", is_incharge:" << (pktInfo->m_peer_incharge.lock().get() == conn)
					<< ", is_substream:" << is_in_incoming_substream(conn, seqno%SUBSTREAM_CNT)
					<< std::endl;
				}
			);
		}
	}
}

void stream_scheduling::on_recvd_be_seed(peer_connection* conn, safe_buffer buf)
{
	if (stream_seed_->get_connection().get() == conn)
	{
		conn->ping_interval(get_client_param_sptr()->server_seed_ping_interval);
		be_super_seed_ = false;
	}
}

void stream_scheduling::on_recvd_be_super_seed(peer_connection* conn, safe_buffer buf)
{
	if (stream_seed_->get_connection().get() == conn)
	{
		conn->ping_interval(SERVER_SUPER_SEED_PING_INTERVAL);
		be_super_seed_ = true;
		unsubscription();
	}
}

void stream_scheduling::on_recvd_piece_notify(peer_connection* conn, safe_buffer buf)
{
	s2p_piece_notify msg;
	if (!parser(buf, msg))
		return;

	process_recvd_buffermap(msg.buffermap(), conn);
}

void stream_scheduling::on_recvd_recommend_seed(peer_connection* conn,
	safe_buffer buf)
{
	BOOST_ASSERT(0 && "recommend_seed功能还未实现");
}

void stream_scheduling::on_recvd_no_piece(peer_connection* conn, safe_buffer buf)
{
	no_piece_msg msg;
	if (!parser(buf, msg))
	{
		BOOST_ASSERT(0);
		return;
	}

	if (conn == stream_seed_->get_connection().get())
	{
		if (msg.has_wait_time())
		{
			uint32_t waitTime = std::min<uint32_t>(msg.wait_time(), 3 * 1000);
			SCHEDULING_DBG(std::cout << "----reset_scheduling----line:" << __LINE__ << std::endl;);
			if (get_bigest_sqno_i_know())
			{
				if (msg.has_min_seqno() && msg.has_max_seqno())
				{
					seqno_t maxSeq = msg.max_seqno();
					seqno_t minSeq = msg.min_seqno();
					int count = seqno_minus(maxSeq, minSeq);
					if (count > std::max(1000, backfetch_cnt_ / 3))
					{
						backfetch_cnt_ = count;
						src_packet_rate_ = bound<double>(MIN_PACKET_RATE, src_packet_rate_*count / backfetch_cnt_, MAX_PACKET_RATE);
						reset_scheduling();
						__init_seqno_i_care(maxSeq + waitTime*src_packet_rate_ / 1000);
					}
				}
			}
		}
	}
	else
	{
		seqno_t seqno = msg.seqno();
		absent_packet_info* pktInfo = get_packet_info(seqno);
		if (pktInfo)
		{
			timestamp_t now = timestamp_now();
			peer_sptr p = conn->get_peer();
			int minTime = p->rtt();

			pktInfo->m_owners.erase(conn->shared_obj_from_this<peer_connection>());
			if (!pktInfo->m_must_pull
				&&pktInfo->is_this(seqno, now)
				&& time_greater(pktInfo->m_pull_outtime, now)
				&& (time_minus(pktInfo->m_pull_time, now) > 0 || is_time_passed(minTime, pktInfo->m_pull_time, now))//推送流或者是较长时间前的请求流
				&& pktInfo->m_peer_incharge.lock().get() == conn
				)
			{
				int remainTime = time_minus(pktInfo->m_pull_outtime, now);
				get_absent_packet_list().request_failed(seqno, now, remainTime);
			}
		}
	}
}

void stream_scheduling::on_recvd_media_request(peer_connection* conn, safe_buffer buf)
{
	//解析
	media_request_msg msg;
	if (!parser(buf, msg))
		return;

#ifdef POOR_CPU
	const double DENY_LOST_THRESH=LOST_RATE_THRESH*0.5;
#else
	const double DENY_LOST_THRESH = LOST_RATE_THRESH*0.8;
#endif

	boost::optional<seqno_t> urgentSeqno;
	timestamp_t now = timestamp_now();
	bool confirmImediatelly = false;
	double localToRemoteLostRate = conn->local_to_remote_lost_rate();
	peer_connection_sptr sock = conn->shared_obj_from_this<peer_connection>();
	peer_sptr p = sock->get_peer();

	if (msg.has_buffermap())
		process_recvd_buffermap(msg.buffermap(), conn);
	if (msg.has_current_playing_timestamp())
	{
		timestamp_t t = msg.current_playing_timestamp() + bound(50, (int)conn->rtt().total_milliseconds(), 2000);
		p->set_current_playing_timestamp(t, now);
	}

	int sendCnt = 0;
	for (int i = 0; i<msg.seqno_size(); ++i)
	{
#ifdef SMOOTH_RESPONSE
		int cnt = smoother_.size();
#else
		int cnt=sendCnt;
#endif
		seqno_t seqno = (seqno_t)msg.seqno(i);
		//以一定概率响应，丢包率越高，响应概率越低
		if (
			(
			localToRemoteLostRate>DENY_LOST_THRESH&&in_probability(localToRemoteLostRate)
			&& (global_local_to_remote_speed_ > MIN_CLINET_UPLOAD_SPEED || global_local_to_remote_speed_ < MIN_CLINET_UPLOAD_SPEED / 3)
#ifdef POOR_CPU
			||(cnt>2||cnt>6&&in_probability(0.5))
#elif !defined(WIN32)
			||(cnt>20||cnt>15&&in_probability(0.5))
#endif
			|| get_sys_cpu_usage(now)>95
			)
			&& (
			!(urgentSeqno || (urgentSeqno = remote_urgent_seqno(p, now)))
			|| seqno_greater(seqno, *urgentSeqno)
			)//对对方重要的片段，尽量响应
			)
		{
			p->media_download_from_local().push_back(seqno);
			confirmImediatelly = true;
		}
		else
		{
#ifdef SMOOTH_RESPONSE
			//平滑响应
			smoother_.push(sock->local_id(),
				boost::bind(&this_type::send_media_packet, this, sock, seqno, false, _1),
				1500
				);
#else
			send_media_packet(sock, seqno, false, 0);
			sendCnt++;
#endif
		}
	}
	if (confirmImediatelly)
	{
		//使得confirm检查时候以为已经1s没有发送confirm了，从而马上进行confirm
		p->last_media_confirm_time() = now - 1000;
	}
}

void stream_scheduling::on_recvd_media_subscription(peer_connection* conn, safe_buffer buf)
{
	media_subscription_msg msg;
	if (!parser(buf, msg))
		return;

	process_recvd_buffermap(msg.buffermap(), conn);

	peer_sptr p = conn->get_peer();
	if (!p)return;
	if (msg.has_current_playing_timestamp()
		&& (!is_player_started()
		|| abs(time_minus(msg.current_playing_timestamp(), get_current_playing_timestamp(timestamp_now()))) < 30000
		)
		)
	{
		timestamp_t t = msg.current_playing_timestamp() + bound(50, (int)conn->rtt().total_milliseconds(), 2000);
		p->set_current_playing_timestamp(t);
	}
	peer_connection_sptr connSptr = conn->shared_obj_from_this<peer_connection>();
	for (int i = 0; i < msg.substream_id_size(); i++)
	{
		int subStreamID = msg.substream_id(i);
		if (subStreamID >= 0 && subStreamID < SUBSTREAM_CNT)
		{
			typedef std::list<boost::weak_ptr<peer_connection> > peer_connection_list;
			bool find = false;
			peer_connection_list& lst = out_substream_[subStreamID];
			peer_connection_list::iterator itr = lst.begin();
			for (; itr != lst.end();)
			{
				peer_connection_sptr c = itr->lock();
				if (!c || !c->is_connected()
					|| c->alive_probability() < ALIVE_DROP_PROBABILITY / 2
					)
				{
					lst.erase(itr++);
				}
				else
				{
					if (c.get() == conn)
					{
						find = true;
						break;
					}
					++itr;
				}
			}
			if (!find)
			{
				BOOST_ASSERT(p);
				lst.push_back(connSptr);
				for (int j = 0; j < msg.ignore_seqno_list_size(); ++j)
				{
					p->ignore_subscription_seqno(msg.ignore_seqno_list(j), subStreamID);
				}
			}
		}
	}
}

void stream_scheduling::on_recvd_media_unsubscription(peer_connection* conn, safe_buffer buf)
{
	media_subscription_msg msg;
	if (!parser(buf, msg))
		return;

	process_recvd_buffermap(msg.buffermap(), conn);

	for (int i = 0; i < msg.substream_id_size(); i++)
	{
		int subStreamID = msg.substream_id(i);
		if (subStreamID < SUBSTREAM_CNT)
		{
			typedef std::list<boost::weak_ptr<peer_connection> > peer_connection_list;

			peer_connection_list& lst = out_substream_[subStreamID];
			peer_connection_list::iterator itr = lst.begin();
			for (; itr != lst.end();)
			{
				peer_connection_sptr c = itr->lock();
				if (!c || !c->is_connected()
					|| c->alive_probability() < ALIVE_DROP_PROBABILITY / 2
					)
				{
					itr = lst.erase(itr);
				}
				else if (c.get() == conn)
				{
					conn->get_peer()->average_push_to_remote_delay(subStreamID).reset();
					itr = lst.erase(itr);
					break;
				}
				else
				{
					++itr;
				}
			}
		}
	}
}


void stream_scheduling::on_recvd_media_packet(peer_connection* conn, safe_buffer buf)
{
	media_packet pkt(buf);
	seqno_t seqno = pkt.get_seqno();
	int diff = get_smallest_seqno_i_care() ? seqno_minus(seqno, *get_smallest_seqno_i_care()) : 0;

	//vod只在乎还没播放的包
	if (is_vod() && (seqno_less(seqno, *get_smallest_seqno_i_care()) || seqno_greater(seqno, max_seqno_))
		|| diff<0 || diff>std::min((int)get_absent_packet_list().capacity(), get_memory_packet_cache().max_size())
		)
	{
		return;
	}

	timestamp_t now = timestamp_now();
	if (!recvd_first_packet_time_)
		recvd_first_packet_time_ = now;

	int64_t sig = security_policy::signature_mediapacket(pkt, get_client_param_sptr()->channel_uuid);
	if (sig != (int64_t)pkt.get_anti_pollution_signature())
	{
		GUARD_TOPOLOGY(;);
		peer_id_t id(conn->get_peer()->get_peer_info().peer_id());
		conn->close(false);
		topology->erase_low_capacity_peer(id);
		return;
	}

	process_recvd_media_packet(pkt, conn, now);
}

void stream_scheduling::process_media_packet_from_nonsocket(media_packet& pkt,
	timestamp_t now)
{
	BOOST_STATIC_ASSERT((boost::is_unsigned<seqno_t>::value));

	//pkt.set_hop(pkt.get_hop()+1);
	seqno_t seqno = pkt.get_seqno();
	const bool isPush = (pkt.get_is_push() != 0);
	int mediaChannel = pkt.get_channel_id();
	timestamp_t pkt_timeStamp = get_timestamp_adjusted(pkt);
	int pkt_packetRate = get_packet_rate_adjusted(pkt);

	//调整统计参数
	bool timeout = is_player_started() && seqno_less(seqno, *get_smallest_seqno_i_care());
	stream_monitor::dupe_state dupState = stream_monitor::NOT_DUPE;
	int has = get_memory_packet_cache().has(seqno, now);

	if (!isPush)
	{
		stream_monitor_->incoming_pulled_media_packet(pkt.buffer().length(),
			seqno, pkt_packetRate, pkt_timeStamp, now, dupState, timeout);
	}
	else
	{
		stream_monitor_->incoming_pushed_media_packet(pkt.buffer().length(),
			seqno, pkt_packetRate, pkt_timeStamp, now, dupState, timeout);
	}

	if (has != packet_buffer::NOT_HAS)
		return;//重复包

	if (!timeout)
	{

		//写入缓存
		get_memory_packet_cache().insert(pkt, pkt_packetRate, now, get_smallest_seqno_i_care());

		media_dispatcher_->do_process_recvd_media_packet(pkt);
	}
	else
	{//超时包
		SCHEDULING_DBG(
			std::cout << "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXOUT, seqno="
			<< seqno << " , smallest_sqno_i_care=" << *get_smallest_seqno_i_care() << "\n";
		);
	}

	//已经收到这个片段
	get_absent_packet_list().recvd(seqno, pkt, now);
}

void stream_scheduling::read_media_packet_from_nosocket(const safe_buffer&buf,
	const error_code& ec, seqno_t seqno)
{
	media_packet pkt(buf);
	int diff = get_smallest_seqno_i_care() ? seqno_minus(seqno, *get_smallest_seqno_i_care()) : 0;

	//vod只在乎还没播放的包
	if (is_vod() &&
		(seqno_less(seqno, min_seqno_)
		|| seqno_greater(seqno, max_seqno_)
		|| diff<0 || diff>std::min((int)get_absent_packet_list().capacity(), get_memory_packet_cache().max_size())
		)
		)
	{
		return;
	}

	timestamp_t now = timestamp_now();
	if (ec || buffer_size(buf) <= media_packet::format_size()
		|| security_policy::signature_mediapacket(pkt, get_client_param_sptr()->channel_uuid) != pkt.get_anti_pollution_signature()
		)
	{
		SCHEDULING_DBG(std::cout << "****!!!!**************************** read from cache failed, seqno="
			<< seqno << "\n";
		);

		absent_packet_info* pktInfo = get_packet_info(seqno);
		if (pktInfo&&pktInfo->is_this(seqno, now))
		{
			pktInfo->m_dskcached = false;
			pktInfo->m_peer_incharge.reset();
			pktInfo->m_pull_outtime = now - 1000;
		}
		return;
	}

	if (!recvd_first_packet_time_)
		recvd_first_packet_time_ = now;
	process_media_packet_from_nonsocket(pkt, now);
}

void stream_scheduling::process_recvd_media_packet(media_packet&pkt, peer_connection* conn, timestamp_t now)
{
	BOOST_STATIC_ASSERT(boost::is_unsigned<seqno_t>::value);

	GUARD_CLIENT_SERVICE(;);

	int hop = pkt.get_hop();
	seqno_t seqno = pkt.get_seqno();
	const int subStreamID = seqno%SUBSTREAM_CNT;
	const bool isPush = (pkt.get_is_push() != 0);
	int mediaChannel = pkt.get_channel_id();
	peer_sptr p = conn->get_peer();
	timestamp_t pkt_timeStamp = get_timestamp_adjusted(pkt);
	int pkt_packetRate = bound(16, get_packet_rate_adjusted(pkt), 200);
	uint8_t serverSession = pkt.get_session_id();

	pkt.set_hop(hop + 1);

	if (hop_ < 0)
		hop_ = hop + 1;
	else
		hop_ = hop_*0.8 + hop*0.2;
	if (substream_hop_[subStreamID] < 0)
		substream_hop_[subStreamID] = hop + 1;
	else
		substream_hop_[subStreamID] = substream_hop_[subStreamID] * 0.8 + hop*0.2;

	//std::cout<<hop_<<std::endl;

	//检查server_session（server重启时会重置session_id）
	if (!server_session_id_)
		server_session_id_ = serverSession;
	else if (is_live() && server_session_id_.get() != serverSession)//session重置了
	{
		SCHEDULING_DBG(std::cout << "----reset_scheduling----line:" << __LINE__ << std::endl;);
		reset_scheduling(&serverSession);
		SCHEDULING_DBG(std::cout << "--------line:" << __LINE__ << std::endl;);
		__init_seqno_i_care(seqno);
		return;
	}

	if (!get_smallest_seqno_i_care())
	{
		BOOST_ASSERT(!get_bigest_sqno_i_know());
		SCHEDULING_DBG(std::cout << "--------line:" << __LINE__ << std::endl;);
		__init_seqno_i_care(seqno);
	}

	//获得源的包速率
	if (src_packet_rate_ < 0)
	{
		src_packet_rate_ = pkt_packetRate;
		BOOST_ASSERT(src_packet_rate_ >= 0 && "packet_rate overflow!!");
	}
	else
	{
		src_packet_rate_ = (src_packet_rate_ * 15 + pkt_packetRate) / 16;
		BOOST_ASSERT(src_packet_rate_ >= 0 && "packet_rate overflow!!");
	}

	p->on_upload_to_local(pkt.buffer().length(), isPush);

	//处理携带的bitmap
	//媒体包可能来自server和合作节点, 来自合作节点时不能将seqnomap_buffer_中piece加入owner。
	//因为磁盘缓存片段所携带的seqno并不能反映真实的最近收到的片段情况。
	//合作节点有哪个piece通过buffermap_exchange消息告知。
	if (is_live())
	{
		seqnomap_buffer_.clear();
		seqnomap_buffer_.push_back(seqno);
		uint64_t bm = pkt.get_recent_seqno_map();
		const char* pbm = (const char*)&bm;
		for (size_t i = 0; i < sizeof(uint64_t) / sizeof(seqno_t); ++i)
		{
			seqno_t seq = read_int_ntoh<seqno_t>(pbm);
			if (seq != seqno)
				seqnomap_buffer_.push_back(seq);
		}
		BOOST_ASSERT(!seqnomap_buffer_.empty());
		std::sort(seqnomap_buffer_.begin(), seqnomap_buffer_.end(), seqno_less);

		bool add_to_owner = (conn != get_server_connection().get());
		buffer_manager_->process_recvd_buffermap(seqnomap_buffer_, in_substream_, conn,
			seqnomap_buffer_.back(), *get_smallest_seqno_i_care(), timestamp_now(),
			add_to_owner, true);
	}

	//timestamp_offset_对于调度算法并非必要，可以直接=0。
	//这里仅仅是为了调试时候能更加直观的看到每个片段的delay方差情况而已。
	//这里的delay仅仅是为了能够检测片段是否已经过了PUSH算法所应该到达的时刻，
	//从而能够估计一个PUSH包是否已经丢包，进而可以确认是否进行PULL操作。
	//delay并无实际的具体意义。
	//只有PUSH算法需要估测预计到达时间，因此，在没有进入is_in_subscription_state_状态前
	//并不设置timestamp_offset_的值，并且，设置delay为0。
	//调整统计参数
	peer_connection_sptr oldInSubstreamConn(in_substream_[subStreamID].lock());
	bool hasSubscription = oldInSubstreamConn&&oldInSubstreamConn->is_connected();
	bool timeout = is_player_started() && seqno_less(seqno, *get_smallest_seqno_i_care());
	absent_packet_info* pktInfo = get_packet_info(seqno);
	stream_monitor::dupe_state dupState = stream_monitor::NOT_DUPE;
	int has = get_memory_packet_cache().has(seqno, now);
	if (has != packet_buffer::NOT_HAS)
	{
		if (isPush || has == packet_buffer::PUSHED_HAS)
			dupState = stream_monitor::PUSH_PULL_DUPE;
		else
			dupState = stream_monitor::PULL_PULL_DUPE;

		SCHEDULING_DBG(
			std::cout << (isPush ? " pushed " : " pulled ");
		error_code ec;
		if (dupState == stream_monitor::PUSH_PULL_DUPE)
		{
			if (pktInfo&&pktInfo->is_this(seqno, now) && pktInfo->m_must_pull)
			{
				std::cout << "DDDDDDDDDm_must_pullDDDDDPUSH_PULL_DUPE="
					<< stream_monitor_->get_duplicate_rate()
					<< ", from: " << conn->remote_endpoint(ec)
					<< " time out:" << timeout
					<< "\n";
			}
			else
			{
				int elapseTime;
				get_piece_elapse(now, seqno, elapseTime, &pkt_timeStamp);
				std::cout << "DDDDDDDDDDDDDDDDDDDDDDDDDPUSH_PULL_DUPE="
					<< stream_monitor_->get_duplicate_rate()
					<< ", " << stream_monitor_->get_push_pull_duplicate_rate(subStreamID)
					<< " from:" << conn->remote_endpoint(ec)
					<< " isPush:" << isPush
					<< " elapse:" << elapseTime
					<< " prePlay:" << time_minus(pkt_timeStamp, get_current_playing_timestamp(now))
					<< " hasSub:" << (bool)(in_substream_[subStreamID].lock())
					<< " isInSub:" << (in_substream_[subStreamID].lock().get() == conn)
					<< "\n";
			}
		}
		else if (dupState == stream_monitor::PULL_PULL_DUPE)
		{
			std::cout << "DDDDDDDDDDDDDDDDDDDDDDDDDDPULL_PULL_DUPE="
				<< stream_monitor_->get_duplicate_rate()
				<< ", from: " << conn->remote_endpoint(ec)
				<< " timeout:" << timeout
				<< " rto=" << p->rto() << " rtt=" << p->rtt() << " rttvar=" << p->rtt_var()
				//<<" pull_cnt="<<pktInfo->m_pull_cnt
				<< "\n";
		}
		else
		{
			std::cout << "DDDDDDDDDDDDDDDDDDDDDDDDDDPOUT, seqno="
				<< seqno << " , smallest_sqno_i_care=" << *get_smallest_seqno_i_care() << "\n";
		}
		);
	}

	if (!isPush)
	{
		p->task_success(seqno, dupState == 0);

		stream_monitor_->incoming_pulled_media_packet(pkt.buffer().length(),
			seqno, pkt_packetRate, pkt_timeStamp, now, dupState, timeout);
	}
	else
	{
		if (oldInSubstreamConn.get() == conn)
		{
			p->last_push_to_local_seqno() = seqno;
			stream_monitor_->incoming_pushed_media_packet(pkt.buffer().length(),
				seqno, pkt_packetRate, pkt_timeStamp, now, dupState, timeout);
		}
		else if (!is_in_subscription_state_)
		{
			unsubscribe(conn, subStreamID);
			SCHEDULING_DBG(
				std::cout << "is_in_subscription_state_ works error!!!" << std::endl;
			);
		}
	}

	//检查是不是可以转到订阅状态
	if (is_live())
	{
		int t = get_buffer_duration(1.0);
		if (!is_in_subscription_state_&&t > 0)
		{
			int elapse = time_minus(now, recvd_first_packet_time_.get());
			int seqDistance = seqno_minus(*get_bigest_sqno_i_know(), seqno);
			if (seqDistance < 800
				&& (t > backfetch_msec() / 2 && get_buffer_health() > 0.80
				|| delay_guarantee_ > 10000 && elapse > delay_guarantee_
				)
				)
			{
				in_subscription_state_confirm_ += 2;
				if (in_subscription_state_confirm_ >= 20)
				{
					is_in_subscription_state_ = true;
					last_subscrib_time_ = now;
					on_info_report_timer(now);
				}
			}
			else if (in_subscription_state_confirm_ > 0)
			{
				in_subscription_state_confirm_--;
			}
		}
		else if (is_in_subscription_state_)
		{
			if (is_player_started()
				&& get_buffer_health() < 0.55
				&& is_time_passed(30 * 1000, last_subscrib_time_, now)
				)
			{
				if (--in_subscription_state_confirm_ <= 0)
				{
					is_in_subscription_state_ = false;
					in_subscription_state_confirm_ = 0;
					on_info_report_timer(now);
					unsubscription();
				}
			}
		}
	}

	static is_subscriptable IS_SUBSCRIPTABLE(SUBSTREAM_CNT, GROUP_CNT);

	int averageSubscribRtt = average_subscribe_rtt(now);
	int globalPushDelay = stream_monitor_->get_average_push_delay();
	BOOST_ASSERT(abs(globalPushDelay) < 2 * std::max(delay_guarantee_, backfetch_msec()));
	//查看是不是可以退订阅这个子流
	//if (isPush/*&&is_in_subscription_state_*/)
	/*
	if (hasSubscription&&oldInSubstreamConn.get() != conn)
	{
	//BOOST_ASSERT(!stream_seed_->get_connection()||conn!=stream_seed_->get_connection().get());
	peer* oldPer = oldInSubstreamConn->get_peer().get();
	BOOST_ASSERT(oldPer);
	int delay = stream_monitor_->get_average_push_delay(subStreamID);
	BOOST_ASSERT(abs(delay) < 2 * std::max(delay_guarantee_, backfetch_msec()));
	if ((
	stream_monitor_->get_push_pull_duplicate_rate(subStreamID) > 0.20//dup太高
	&&is_time_passed(20000, oldPer->last_subscription_time(subStreamID), now)//不能订阅不久就退订
	)
	||
	(
	(stream_monitor_->get_push_rate(subStreamID) < std::max(0.10, stream_monitor_->get_push_rate()/2)//推送率太低
	|| oldInSubstreamConn->remote_to_local_lost_rate() > (LOST_RATE_THRESH + 0.25)//丢包率过高
	|| oldPer->rtt() > int(averageSubscribRtt + get_client_param_sptr()->max_push_delay)//链路延迟过大
	|| (delay - globalPushDelay) > get_client_param_sptr()->max_push_delay + 1500//片段延迟过大
	|| (stream_monitor_->get_bigest_push_delay_substream() == subStreamID && (delay - globalPushDelay) > get_client_param_sptr()->max_push_delay)//片段延迟过大
	)
	&& is_time_passed(5000, oldPer->last_unsubscription_time(), now)//不能频繁退订
	&& is_time_passed(15000, oldPer->last_subscription_time(subStreamID), now)//不能频繁退订
	)
	)
	{
	SCHEDULING_DBG(
	error_code ec;
	std::cout << "WWWWWWWWWWWWWWWWWWWWWWWARRING, isPush, unsubscription(" << subStreamID << ")"
	<< (conn->remote_endpoint(ec)) << "\n"
	<< "push_rate: " << stream_monitor_->get_push_rate(subStreamID)
	<< ", dupe: " << stream_monitor_->get_push_pull_duplicate_rate(subStreamID)
	<< ", lost_rate: " << oldInSubstreamConn->remote_to_local_lost_rate()
	<< ", rtt: " << oldPer->rtt() << ", " << averageSubscribRtt
	<< ", delay-globalDelay: " << (delay - globalPushDelay) << ", " << globalPushDelay
	<< ", bigest_push_delay_substream: " << (stream_monitor_->get_bigest_push_delay_substream() == subStreamID && (delay - globalPushDelay) > (get_client_param_sptr()->max_push_delay - 400))
	<< "\n";
	);
	send_unsubscription_to(oldInSubstreamConn.get(), subStreamID);
	hasSubscription = false;
	}
	}
	*/

	//看看是不是可以订阅或者换订阅节点
	if (!ONLY_PULL
		&& (!timeout)
		&& is_in_subscription_state_
		&& IS_SUBSCRIPTABLE(seqno)//是订阅标识点
		&& p->playing_the_same_channel()
		&& (stream_seed_->get_connection().get() != conn)
		&& (!is_player_started() || seqno_greater_equal(seqno, *get_smallest_seqno_i_care()))//播放器还未启动或者包还没有严重过时
		&& !local_is_super_seed()//super_seed是不订阅的
		&& !is_in_outgoing_substream(conn, seqno%SUBSTREAM_CNT)
		&& seqno_greater(seqno + GROUP_LEN, *get_bigest_sqno_i_know())//避免循环订阅
		)
	{//否则，看看是否可以订阅或者重新订阅这个子流
		if (hasSubscription)
		{//如果已经订阅，看看是不是退订老的，重新从其他节点订阅
			int delay = stream_monitor_->get_average_push_delay(subStreamID);
			if (oldInSubstreamConn.get() != conn
				&& (stream_monitor_->get_push_rate(subStreamID) < std::max(0.10, stream_monitor_->get_push_rate() / 2) && conn->remote_to_local_lost_rate() < LOST_RATE_THRESH
				|| (oldInSubstreamConn->remote_to_local_lost_rate() > (LOST_RATE_THRESH + 0.25) && conn->remote_to_local_lost_rate() < LOST_RATE_THRESH)
				|| (delay - globalPushDelay) > 800//片段延迟过大
				|| (oldInSubstreamConn->get_peer()->rtt() > int(averageSubscribRtt + 500) && (p->rtt() + 50) < oldInSubstreamConn->get_peer()->rtt())
				|| (stream_monitor_->get_bigest_push_delay_substream() == subStreamID && (delay - globalPushDelay) > 600)
				)
				&& is_time_passed(4000, p->last_unsubscription_time(), now)//不能频繁退订
				&& is_time_passed(10000, p->last_subscription_time(subStreamID), now)//不能订阅不久就退订
				&& (
				p->subscription_count() < SUBSTREAM_CNT / ((topology->neighbor_count() / 3) + 1)
				|| is_time_passed(80, p->last_subscription_time(), now)
				)
				)
			{
				SCHEDULING_DBG(
					error_code ec;
				std::cout << "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWARRING, !isPush, unsubscription "
					<< (conn->remote_endpoint(ec))
					<< "\n";
				);
				unsubscribe(oldInSubstreamConn.get(), subStreamID);
				subscribe(conn, subStreamID, seqno);
			}
		}
		else
		{
			//如果没有订阅，订阅
			if (conn->remote_to_local_lost_rate() < (LOST_RATE_THRESH + 0.25)
				&& (p->subscription_count() < SUBSTREAM_CNT / ((topology->neighbor_count() / 4) + 1)
				||is_time_passed(1000, p->last_subscription_time(), now)
				)
				&& is_time_passed(200, p->last_subscription_time(), now)
				)
			{
				SCHEDULING_DBG(
					std::cout << "HHHHHHHHHHHHHHHHHHHHHHHHAHA, send subscription"
					<< "\n";
				);
				subscribe(conn, subStreamID, seqno);
			}
		}
	}

	if (has != packet_buffer::NOT_HAS)
		return;//重复包

	get_memory_packet_cache().insert(pkt, pkt_packetRate, now, get_bigest_sqno_i_know());

	if (!is_live() && clientService->get_vod_channel_info())//timeshift也要写磁盘
	{
		//写入磁盘
		BOOST_ASSERT(!pktInfo || !pktInfo->m_dskcached);
		if ((!pktInfo || !pktInfo->m_dskcached) && get_cache_service(get_io_service()))
		{
			int totalPieceCnt = (clientService->get_vod_channel_info()->film_length() + PIECE_SIZE - 1) / PIECE_SIZE;
			get_cache_manager(get_io_service())->write_piece(
				get_client_param_sptr()->channel_uuid, totalPieceCnt, seqno, pkt.buffer(),
				boost::bind(&dummy_func_2, _1, _2)
				);
		}
	}

	if (!timeout)
	{
		media_dispatcher_->do_process_recvd_media_packet(pkt);

		//处理被订阅的媒体数据
		do_subscription_push(pkt, now);
	}
	else
	{//超时包
		SCHEDULING_DBG(
			std::cout << "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXOUT, seqno="
			<< seqno << " , smallest_sqno_i_care=" << *get_smallest_seqno_i_care() << "\n";
		);
	}

	//已经收到这个片段。这会删除这一seqno的scheduling_packet_info。
	//将这句放到函数最后，以便于其他函数处理过程中这一seqno的scheduling_packet_info尚存。
	get_absent_packet_list().recvd(seqno, pkt, now);
}

void stream_scheduling::process_recvd_buffermap(const buffermap_info& bufmap,
	peer_connection* p)
{
	if (!get_smallest_seqno_i_care())
	{
		struct find_max_seqno_from_bufmap{
			boost::optional<seqno_t> operator()(const buffermap_info& bufmap)const
			{
				if (bufmap.has_bigest_seqno_i_know())
					return bufmap.bigest_seqno_i_know();
				if (bufmap.has_bitset())
				{
					const std::string& str = bufmap.bitset();
					for (int i = (int)str.size(); i >= 0; --i)
					{
						if (str[i])
						{
							int j = 1;
							char chr = str[i];
							while ((chr <<= 1))
							{
								j++;
							}
							return seqno_t(bufmap.first_seqno_in_bitset() + i * 8 + j);
						}
					}
				}
				return boost::optional<seqno_t>();
			}
		};
		boost::optional<seqno_t> maxSeq = find_max_seqno_from_bufmap()(bufmap);
		if (!maxSeq)
			return;
		__init_seqno_i_care(*maxSeq);
	}
	bool add_to_owner = (p != get_server_connection().get());
	buffer_manager_->process_recvd_buffermap(bufmap, in_substream_, p,
		*get_smallest_seqno_i_care(), timestamp_now(), add_to_owner);
}

void stream_scheduling::set_local_status(scheduling_status& stat, timestamp_t now)
{
	GUARD_CLIENT_SERVICE(;);
	stat.m_recvd_first_packet_time = recvd_first_packet_time_;
	stat.m_scheduling_start_time = scheduling_start_time_;
	stat.m_smallest_seqno_i_care = get_smallest_seqno_i_care();
	stat.m_bigest_seqno_i_know = get_bigest_sqno_i_know();
	stat.m_now = now;
	stat.m_playing_quality = stream_monitor_->get_playing_quality();
	stat.m_global_remote_to_local_lostrate = global_remote_to_local_lost_rate();
	stat.m_duplicate_rate = stream_monitor_->get_duplicate_rate();
	stat.m_buffer_health = get_buffer_health();
	stat.m_buffer_size = get_buffer_size();
	stat.m_buffer_duration = get_buffer_duration(stat.m_buffer_health);//stat.m_buffer_health要先计算
	stat.m_incoming_packet_rate = stream_monitor_->get_incoming_packet_rate();
	stat.m_neighbor_cnt = topology->neighbor_count();
	stat.m_total_online_peer_cnt = clientService->total_online_peer_cnt();
	stat.m_delay_gurantee = delay_guarantee_;
	stat.m_backfetch_msec = backfetch_msec();
	stat.m_b_super_seed = local_is_super_seed();
	stat.m_b_seed = local_is_seed();
	stat.m_b_inscription_state = is_in_subscription_state_;
	stat.m_b_play_start = is_player_started();
	stat.m_b_live = is_live();
	stat.m_server_connection = stream_seed_->get_connection();
	stat.m_max_memory_cach_size = get_memory_packet_cache().max_size();

	if (is_live())
	{
		stat.m_src_packet_rate = get_src_packet_rate();
	}
	else
	{
		stat.m_src_packet_rate = get_average_src_packet_rate();
	}

	if (stat.m_buffer_size > 0)
	{
		stat.m_bigest_seqno_in_buffer = get_max_seqno_in_buffer();
		stat.m_smallest_seqno_in_buffer = get_min_seqno_in_buffer();
	}
	if (stat.m_b_seed)
		stat.m_server_to_local_lostrate = get_server_connection()->remote_to_local_lost_rate();

	calc_download_speed_coefficient(stat);
}

void stream_scheduling::on_timer()
{
	int cpuUsage = 0;
	timestamp_t now = timestamp_now();
	//static timestamp_t last_time=now;
	//if(time_minus(now, last_time)>0)
	//	std::cout<<"time elapse: "<<time_minus(now, last_time)<<"\n";
	//last_time = now;
	//return;
	on_unsubscrip_timer(now);

	if (cpuUsage < 100
		&& is_time_passed(PULL_TIMER_INTERVAL + random(0, 10), last_pull_time_, now))
	{
		timestamp_t t = timestamp_now();

		cpuUsage += 80;
		last_pull_time_ = now;
		on_pull_timer(now);

		//在嵌入式系统，on_pull_timer执行往往需要几十毫秒，所以，这里更新下时间
		now = timestamp_now();
		last_pull_time_ = now;

		//printf("---------:%d\n", time_minus(timestamp_now(), t));
	}

	if (cpuUsage < 100
		&& is_time_passed(BUFFERMAP_EXCHANGE_INTERVAL.total_milliseconds() / 3, last_exchange_buffermap_time_, now))
	{
		cpuUsage += on_exchange_buffermap_timer(now);
		last_exchange_buffermap_time_ = now;
	}

	if (cpuUsage < 100
		&& is_time_passed(MEDIA_CONFIRM_INTERVAL, last_media_confirm_time_, now))
	{
		timestamp_t t = timestamp_now();
		cpuUsage += on_media_confirm_timer(now);
		last_media_confirm_time_ = now;
	}

	if (cpuUsage < 100
		&& is_time_passed(QUALITY_REPORT_TIMER_INTERVAL, last_quality_report_time_, now))
	{
		cpuUsage += 5;
		last_quality_report_time_ = now;
		on_quality_report_timer(now);
	}

	if (cpuUsage < 100)
	{
		cpuUsage += stream_seed_->on_timer(now);
	}

	if (!just_be_provider_)
	{
		if (cpuUsage < 100
			&& is_time_passed(INFO_REPORT_TIMER_INTERVAL, last_info_report_time_, now))
		{
			cpuUsage += 5;
			last_info_report_time_ = now;
			on_info_report_timer(now);
		}

		if (cpuUsage < 100)
		{
			cpuUsage += media_dispatcher_->on_timer(now);
		}
	}
}

void stream_scheduling::check_health(const scheduling_status&localStatus)
{
	if (localStatus.m_b_live)
	{
		if (localStatus.m_incoming_packet_rate < localStatus.m_src_packet_rate / 4)
		{
			enum{ MAX_LOWSPEED_TIME = 15000 };
			int currDelayPlayTime = media_dispatcher_->get_delay_play_time();
			if (!low_speed_time_)
			{
				low_speed_time_ = localStatus.m_now;
				low_speed_delay_play_time_ = currDelayPlayTime;
			}
			else if (
				(localStatus.m_buffer_size<1000
				|| abs(*low_speed_delay_play_time_ - currDelayPlayTime)>MAX_LOWSPEED_TIME / 2
				)
				&& is_time_passed(MAX_LOWSPEED_TIME, *low_speed_time_, localStatus.m_now)
				)
			{
				low_speed_time_.reset();
				low_speed_delay_play_time_.reset();
				SCHEDULING_DBG(std::cout << "-------restart----line:" << __LINE__ << std::endl;);
				restart();
			}
		}
		else if (low_speed_time_)
		{
			low_speed_time_.reset();
			low_speed_delay_play_time_.reset();
		}
	}
}

//核心调度算法：pull
void stream_scheduling::on_pull_timer(timestamp_t now)
{
	if (!get_smallest_seqno_i_care())
	{
		if (time_minus(now, scheduling_start_time_) > 10 * 1000)
		{
			SCHEDULING_DBG(std::cout << "----restart----line:" << __LINE__ << std::endl;);
			restart();
			SCHEDULING_DBG(std::cout << "--------line:" << __LINE__ << std::endl;);
		}
		return;
	}

	scheduling_status& localStatus = *scheduling_strategy_;
	set_local_status(localStatus, now);

	//必须在set_local_status已经被调用
	check_health(*scheduling_strategy_);

	const scheduling_task_map& task_map = scheduling_strategy_->get_task_map();
	if (task_map.empty())
		return;

	if (!snd_media_request_msg_)
	{
		snd_media_request_msg_.reset(new media_request_msg);
		snd_media_request_msg_->set_direct_request(true);
		snd_media_request_msg_->set_peer_id(get_client_param_sptr()->local_info.peer_id());
		if (!is_live())
			snd_media_request_msg_->set_channel_id(get_client_param_sptr()->channel_uuid);
	}
	snd_media_request_msg_->clear_buffermap();
	set_buffermap(*snd_media_request_msg_);

	double pullPullDupRate = stream_monitor_->get_pull_pull_duplicate_rate();
	double alf = (1.0 + 30 * pullPullDupRate*pullPullDupRate);
	boost::shared_ptr<asfio::async_dskcache>dskCache = buffer_manager_->get_disk_packet_cache();
	for (BOOST_AUTO(itr, task_map.begin()); itr != task_map.end(); ++itr)
	{
		const std::vector<seqno_t>& seq_lst = itr->second;

		if (NULL == itr->first)//这是VoD有DiskCache
		{
			BOOST_FOREACH(seqno_t seq, seq_lst)
			{
				absent_packet_info* pktInfo = get_packet_info(seq);
				BOOST_ASSERT(pktInfo);
				if (pktInfo)
				{
					BOOST_ASSERT(pktInfo->m_dskcached);
					enum{ EstimateDelayMsec = 100 };
					pktInfo->m_pull_outtime = (timestamp_t)(localStatus.m_now + EstimateDelayMsec + SMOOTH_TIME);
					pktInfo->m_last_rto = 200;
					pktInfo->m_pull_time = localStatus.m_now;
					pktInfo->m_last_check_peer_incharge_time = localStatus.m_now;
					pktInfo->m_pull_cnt++;
					DEBUG_SCOPE(
						pktInfo->m_pull_edps.push_back(absent_packet_info::endpoint_info());
					pktInfo->m_pull_edps.back().t = localStatus.m_now;
					);
					get_absent_packet_list().set_requesting(seq);
				}

				if (dskCache)
				{
					dskCache->read_piece(get_client_param_sptr()->channel_uuid, seq,
						boost::bind(&stream_scheduling::read_media_packet_from_nosocket,
						SHARED_OBJ_FROM_THIS, _1, _2, seq)
						);
				}
			}
			continue;
		}
		else
		{
			snd_media_request_msg_->clear_seqno();
			peer_connection_sptr serverConn = stream_seed_->get_connection();
			peer_connection_sptr conn = itr->first->shared_obj_from_this<peer_connection>();
			peer_sptr p = conn->get_peer();
			double lostRate = conn->remote_to_local_lost_rate();
			int peerRto = std::min(6000, (int)(p->rto()*alf));
			BOOST_FOREACH(seqno_t seq, seq_lst)
			{
				BOOST_ASSERT(!get_memory_packet_cache().has(seq, now));
				absent_packet_info* pktInfo = get_packet_info(seq);
				BOOST_ASSERT(pktInfo);
				if (pktInfo)
				{
					get_absent_packet_list().set_requesting(seq);
					snd_media_request_msg_->add_seqno(seq);
					
					int estimateDelayMsec = p->keep_task(seq, lostRate);
					BOOST_ASSERT(!pktInfo->m_dskcached);
					pktInfo->m_peer_incharge = conn;
					pktInfo->m_pull_outtime = (timestamp_t)(now + estimateDelayMsec + SMOOTH_TIME);
					pktInfo->m_last_rto = peerRto;
					pktInfo->m_pull_time = now;
					pktInfo->m_last_check_peer_incharge_time = now;
					if (conn == serverConn)
					{
						if (pktInfo->m_server_request_deadline>0)
							pktInfo->m_server_request_deadline--;
					}
					else
					{
						BOOST_ASSERT(pktInfo->m_owners.size());
						pktInfo->m_owners.dec_request_deadline(conn);
					}
					pktInfo->m_pull_cnt++;

					DEBUG_SCOPE(
						error_code ec;
					pktInfo->m_pull_edps.push_back(absent_packet_info::endpoint_info());
					pktInfo->m_pull_edps.back().t = localStatus.m_now;
					pktInfo->m_pull_edps.back().edp = conn->remote_endpoint(ec);
					BOOST_ASSERT(pktInfo->m_pull_edps.size() == pktInfo->m_pull_cnt);
					);
				}
			}
#if SMOOTH_REQUEST
			smoother_.push(conn->local_id(),
				boost::bind(&peer_connection::async_send_semireliable, conn,
				serialize(*snd_media_request_msg_), (message_type)global_msg::media_request),
				256
				);
#else
			conn->async_send_semireliable(serialize(*snd_media_request_msg_), 
				global_msg::media_request);//这里使用semi reliab发送
#endif
		}
	}
}

void stream_scheduling::calc_download_speed_coefficient(const scheduling_status&stat)
{
	if (is_vod())
	{
		int incomingPktRate = stream_monitor_->get_incoming_packet_rate()
			*(1 + stat.m_global_remote_to_local_lostrate);
		if (!is_player_started())
		{
			if (incomingPktRate > src_packet_rate_ / 4)
			{
				if ((incomingPktRate<0.9*src_packet_rate_&&stat.m_global_remote_to_local_lostrate>0.1)//下行带宽不足，降低下载速度
					|| incomingPktRate > src_packet_rate_//下行带宽较好，增加下载速度
					)
				{
					src_packet_rate_ = (incomingPktRate + 2 * src_packet_rate_) / 3;
				}
			}
		}
		else
		{
			src_packet_rate_ = (incomingPktRate + 2 * src_packet_rate_
				+ stream_monitor_->get_push_to_player_packet_rate()) / 4;
		}
		src_packet_rate_ = bound<double>(average_packet_rate_.get(),
			src_packet_rate_*(1.0 - 0.2*stat.m_global_remote_to_local_lostrate),
			average_packet_rate_.get() * 3 / 2);
	}
	peer_info& local_info = get_client_param_sptr()->local_info;
	local_info.set_playing_quality(stat.m_playing_quality);
	local_info.set_global_remote_to_local_lost_rate(stat.m_global_remote_to_local_lostrate);
}

int stream_scheduling::on_exchange_buffermap_timer(timestamp_t now)
{
	GUARD_TOPOLOGY(0);
	const neighbor_map& conn_map = topology->get_neighbor_connections();
	if (is_live())
		return exchange_buffermap_live(conn_map, now);
	else if (is_vod())
		return exchange_buffermap_vod(conn_map, now);
	else
	{
		TODO("添加其他类型的buffermap exchange 函数，或者利用现有实现");
		return 0;
	}
}

int stream_scheduling::exchange_buffermap_live(const neighbor_map& conn_map, timestamp_t now)
{
	int cpuUsage = 0;
	BOOST_AUTO(itr, conn_map.begin());
	BOOST_AUTO(end, conn_map.end());
	safe_buffer exchangeBuf;
	int intervalMsec = (int)BUFFERMAP_EXCHANGE_INTERVAL.total_milliseconds();
	for (; itr != end; ++itr)
	{
		peer_sptr per = itr->second->get_peer();
		timestamp_t& lastExchangeTime = per->last_buffermap_exchange_time();
		if (per && is_time_passed(intervalMsec, lastExchangeTime, now))
		{
			BOOST_ASSERT(per->playing_the_same_channel());
			if (exchangeBuf.length() == 0)
			{
				buffermap_exchange_msg msg;
				buffer_manager_->get_buffermap(msg, src_packet_rate_, true);
				if (is_player_started())
				{
					msg.set_current_playing_timestamp(get_current_playing_timestamp(now));
				}
				exchangeBuf = serialize(msg);
			}
			lastExchangeTime = now + random(0, intervalMsec / 2);

			peer_connection* conn = itr->second.get();
			conn->async_send_unreliable(exchangeBuf, peer_peer_msg::buffermap_exchange);
			cpuUsage += 2;
		}
	}
	return cpuUsage;
}

int stream_scheduling::exchange_buffermap_vod(const neighbor_map& conn_map,
	timestamp_t now)
{
	buffer_manager_->inject_absent_seqno(*get_smallest_seqno_i_care(), get_buffer_size(), now);

	int cpuUsage = 0;

	BOOST_AUTO(const&bigest_sqno_i_know, get_bigest_sqno_i_know());
	BOOST_AUTO(const&smallest_sqno_i_care, get_smallest_seqno_i_care());

	int neighbor_peer_cnt = conn_map.size();
	src_packet_rate_ = get_src_packet_rate();
	int intervalTime = std::min(1000, neighbor_peer_cnt * 1000 / 2);
	int maxSeqDiff = MAX_EXCHANGE_TIME * src_packet_rate_ / 1000;
	const int64_t kExchangeInterval = BUFFERMAP_EXCHANGE_INTERVAL.total_milliseconds();
	BOOST_AUTO(itr, conn_map.begin());
	BOOST_AUTO(end, conn_map.end());
	for (; itr != end; ++itr)
	{
		peer_sptr per = itr->second->get_peer();
		const timestamp_t& lastExchangeTime = per->last_buffermap_exchange_time();
		if (per && is_time_passed(kExchangeInterval, lastExchangeTime, now))
		{
			BOOST_ASSERT(!per->playing_the_same_channel());

			//cache交换间隔时间要长一些
			if (is_time_passed(intervalTime / 2, lastExchangeTime, now))
			{
				seqno_t absent_first_seq, absent_last_seq;

				buffer_manager_->get_absent_seqno_range(absent_first_seq, absent_last_seq, per, intervalTime,
					get_smallest_seqno_i_care(), src_packet_rate_);

				//太多了过会再请求
				if (seqno_minus(absent_first_seq, *smallest_sqno_i_care) > maxSeqDiff ||
					seqno_greater(absent_first_seq, *bigest_sqno_i_know + 200) ||
					seqno_greater(absent_first_seq, max_seqno_)
					)
				{
					return cpuUsage;
				}

				//enum{BACK_CNT = 8};
				//if (per->smallest_buffermap_exchange_seqno() && 
				//	seqno_greater_equal(per->smallest_buffermap_exchange_seqno().get() + BACK_CNT, max_seqno_)
				//	)
				//{
				//	return cpuUsage;
				//}
				//else
				//{
				//	per->smallest_buffermap_exchange_seqno() = absent_last_seq - BACK_CNT;//-BACK_CNT是为了防止因为对方发送buffermap时候以8对齐造成漏报
				//}

				p2p_buffermap_request_msg msg;
				msg.set_min_seqno(absent_first_seq);
				msg.set_max_seqno(absent_last_seq);

				peer_connection* conn = itr->second.get();
				conn->async_send_semireliable(serialize(msg), peer_peer_msg::buffermap_request);
				per->last_buffermap_exchange_time() = now;
				cpuUsage += 3;
			}
		}
	}
	return cpuUsage;
}

void stream_scheduling::on_info_report_timer(timestamp_t now)
{
	GUARD_CLIENT_SERVICE(;);
	if (local_is_seed())
	{
		int capacity = get_upload_capacity();//启动上行带宽测量
		if (capacity <= 0)
			capacity = 32 * 1024;//默认是32KB
		//如果在非订阅状态，就设置为0，告诉server不要进行push
#ifdef POOR_CPU
		int upSpeed = is_in_subscription_state_ ? 1 : 0;
#else
		int realUpSpeed = (stream_monitor_->get_outgoing_speed()*global_local_to_remote_lost_rate());
		int upSpeed = is_in_subscription_state_ ? (std::min(capacity / 8, 8 * 1024) + realUpSpeed) : 0;
#endif

#if ONLY_PULL||DISABLE_SERVER_PUSH
		WARNING("*******************************************************************");
		WARNING("*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*");
		WARNING("*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*");
		WARNING("*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*");
		WARNING("*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*");
		WARNING("*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*");
		WARNING("*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*");
		WARNING("*!!!!!!!!!!!!!!! ONLY_PULL or DISABLE_SERVER_PUSH !!!!!!!!!!!!!!!!*");
		WARNING("*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*");
		WARNING("*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*");
		WARNING("*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*");
		WARNING("*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*");
		WARNING("*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*");
		WARNING("*******************************************************************");
		upSpeed=0;
#endif
		p2s_info_report_msg msg;
		msg.set_upload_speed(upSpeed);
		msg.set_lost_rate((float)stream_seed_->get_connection()->remote_to_local_lost_rate());

		stream_seed_->get_connection()->async_send_unreliable(serialize(msg), server_peer_msg::info_report);
	}

	if (in_probability(0.05))
	{
		LogInfo(
			"\n "
			"onlineClintCount:%d (%d); "
			"neighborCount:%d; "
			"uploadCapacity:%d; "
			"uploadSpeed:%d; "
			"playingQuality:%1.5f; "
			"pushedRate:%1.5f; "
			"bufferHealth:%1.5f; "
			, clientService->total_online_peer_cnt()
			, clientService->peers().size()
			, topology->neighbor_count()
			, get_upload_capacity()
			, stream_monitor_->get_outgoing_speed()
			, stream_monitor_->get_playing_quality()
			, stream_monitor_->get_push_rate()
			, get_buffer_health()
			);
	}
}

void stream_scheduling::on_quality_report_timer(timestamp_t now)
{
	GUARD_CLIENT_SERVICE(;);
	peer_connection_sptr seedConn = stream_seed_->get_connection();
	if (!seedConn)
		return;

	p2ts_quality_report_msg msg;
	msg.set_id(0);
	msg.set_ip(get_client_param_sptr()->local_info.external_ip());
	msg.set_playing_quality(stream_monitor_->get_playing_quality());
	msg.set_uplink_lostrate(seedConn->local_to_remote_lost_rate());//上行链路丢包率
	msg.set_downlink_lostrate(seedConn->remote_to_local_lost_rate());//下行链路丢包率
	msg.set_push_rate(stream_monitor_->get_push_rate());//推送率

	msg.set_duplicate_rate(stream_monitor_->get_duplicate_rate());//片段重复率
	double share_rate = (stream_monitor_->get_incoming_speed() - 10e-6) > 0 ?
		(stream_monitor_->get_outgoing_speed() / stream_monitor_->get_incoming_speed()) : 0;
	msg.set_share_rate(share_rate);//上载率(上载速度与下载速度的比率)
	msg.set_buffer_health(get_buffer_health());//buffer健康度
	if (is_player_started())
		msg.set_delay(delay_guarantee_);//启动延迟
#ifndef WINDOWS_OS
	msg.set_cpu(get_sys_cpu_usage(now));//cpu使用，PC上不做统计，只box携带
#endif
	SCHEDULING_DBG(
		std::cout << "xxxxxxxxxxxxxxxxxxxxxxxxxxxxx----quality-----report----------timer-0---\n";
	);
	clientService->report_quality(msg);
}

void stream_scheduling::send_handshake_to(peer_connection* conn)
{
	//construct handshake message
	p2p_handshake_msg msg;
	msg.set_playing_channel_id(get_client_param_sptr()->channel_uuid);
	*(msg.mutable_peer_info()) = get_client_param_sptr()->local_info;
	set_buffermap(msg, true, true, false);

	//add chunk map info
	if (is_vod()
		&& (conn->get_peer()->playing_the_same_channel())
		)
	{
		if (get_cache_service(get_io_service()))
		{
			get_cache_manager(get_io_service())->get_chunk_map(
				get_client_param_sptr()->channel_uuid, *(msg.mutable_chunkmap())
				);
		}
	}

	//send handshake message
	conn->async_send_reliable(serialize(msg), peer_peer_msg::handshake_msg);
	conn->get_peer()->last_buffermap_exchange_time() = timestamp_now();
}

void stream_scheduling::send_buffermap_to(peer_connection* conn)
{
	buffermap_exchange_msg exmsg;
	set_buffermap(exmsg, true, true, false);

	conn->async_send_unreliable(serialize(exmsg), peer_peer_msg::buffermap_exchange);
}

void stream_scheduling::neighbor_erased(const peer_id_t& id)
{
	//不在这里轮询处理了，太费，选择这个节点调度某片段时候再检查
	/*
	timestamp_t now=timestamp_now();
	absent_packet_list::iterator itr=absent_media_packets_.begin();
	for (;itr!=absent_media_packets_.end();++itr)
	{
	seqno_t seqno=*itr;
	media_packet_info& pktInfo=absent_packet_list_.get_packet_info(seqno);
	if (pktInfo->is_this(seqno, now))
	{
	pktInfo->m_owners.erase(id);
	}
	}
	*/
}

void stream_scheduling::subscribe(peer_connection* conn, int substreamID, seqno_t seqno)
{
	peer_sptr p = conn->get_peer();
	if (!p) return;
	BOOST_ASSERT(p->playing_the_same_channel());

	timestamp_t now = timestamp_now();

	media_subscription_msg msg;
	msg.add_substream_id(substreamID);
	if (is_player_started())
		msg.set_current_playing_timestamp(get_current_playing_timestamp(now));
	set_buffermap(msg);
	for (seqno_t i = seqno - 4 * SUBSTREAM_CNT; seqno_less(i, seqno + 4 * SUBSTREAM_CNT); i += SUBSTREAM_CNT)
	{
		absent_packet_info* pktInfo = NULL;
		if (get_memory_packet_cache().has(i)
			|| NULL == (pktInfo = get_packet_info(i)) || time_less(now, pktInfo->m_pull_outtime)
			)
		{
			msg.add_ignore_seqno_list(i);
			p->ignore_subscription_seqno(seqno,substreamID);
		}
	}
	//msg.add_ignore_seqno_list(seqno_t(seqno-SUBSTREAM_CNT));
	conn->async_send_reliable(serialize(msg), peer_peer_msg::media_subscription);
	conn->get_peer()->last_buffermap_exchange_time() = now;

	stream_monitor_->reset_incoming_substeam(substreamID);
	in_substream_[substreamID] = conn->shared_obj_from_this<peer_connection>();
	p->last_subscription_time() = now;
	p->last_subscription_time(substreamID) = now;
	p->subscription_count()++;
}

void stream_scheduling::unsubscribe(peer_connection* conn, int substreamID)
{
	if (!conn || conn == stream_seed_->get_connection().get())
	{
		return;
	}

	peer_sptr p = conn->get_peer();
	if (!p)
	{
		stream_monitor_->reset_incoming_substeam(substreamID);
		return;
	}

	BOOST_ASSERT(p->playing_the_same_channel());

	timestamp_t now = timestamp_now();
	if (!is_time_passed(1000, p->last_unsubscription_time(substreamID), now))
	{
		return;
	}

	BOOST_ASSERT(conn);
	if (conn->is_connected())
	{
		media_subscription_msg msg;
		msg.add_substream_id(substreamID);
		set_buffermap(msg);

		conn->async_send_reliable(serialize(msg), peer_peer_msg::media_unsubscription);
		p->last_buffermap_exchange_time() = now;
		p->last_unsubscription_time() = now;
		p->last_unsubscription_time(substreamID) = now;
		p->subscription_count()--;

		//以一定的延迟重置in_substream_的对应子流的订阅链接，取消链接有延迟
		BOOST_AUTO(connSptr, in_substream_[substreamID].lock());
		if (connSptr&&connSptr.get() == conn)
		{
			stream_monitor_->reset_incoming_substeam(substreamID);
			unsubscrips_.push_back(unsubscript_elm(connSptr, substreamID, now));
		}
	}
	else
	{
		stream_monitor_->reset_incoming_substeam(substreamID);
		BOOST_AUTO(connSptr, in_substream_[substreamID].lock());
		if (connSptr.get() == conn)
			in_substream_[substreamID].reset();
	}
}

void stream_scheduling::on_unsubscrip_timer(timestamp_t now)
{
	while (!unsubscrips_.empty())
	{
		unsubscript_elm& elm = unsubscrips_.front();
		peer_connection* conn = elm.conn.lock().get();
		if (!conn)
		{
			unsubscrips_.pop_front();
			continue;
		}
		else if (is_time_passed(UNSUBCRIP_DELAY, elm.t, now))
		{
			in_substream_[elm.substream_id].reset();
			unsubscrips_.pop_front();
		}
		else
		{
			break;
		}
	}
}

int stream_scheduling::on_media_confirm_timer(timestamp_t now)
{
	int cpuUsage = 0;

	topology_sptr topology = topology_.lock();
	if (!topology)
		return 0;

	BOOST_AUTO(const& conns, topology->get_neighbor_connections());
	BOOST_AUTO(itr, conns.begin());
	BOOST_AUTO(end, conns.end());
	for (; itr != end; ++itr)
	{
		const peer_connection_sptr& conn = itr->second;
		if (!conn)continue;
		peer_sptr p = conn->get_peer();
		if (!p)continue;

		int haveSentSize = static_cast<int>(p->media_download_from_local().size());
		if (haveSentSize > 0)
		{
			if (haveSentSize > 8)
			{
				cpuUsage += haveSentSize / 2;
				media_sent_confirm_msg_.Clear();
				BOOST_FOREACH(seqno_t seq, p->media_download_from_local())
				{
					media_sent_confirm_msg_.add_seqno(seq);
				}
				safe_buffer buf = serialize(media_sent_confirm_msg_);
				conn->async_send_semireliable(buf, global_msg::media_sent_confirm);
				p->media_download_from_local().clear();
				p->last_media_confirm_time() = now;
			}
		}
		else
		{
			p->last_media_confirm_time() = now;
		}
	}
	return cpuUsage;
}

bool stream_scheduling::has_subscription(peer_connection* conn)
{
	BOOST_FOREACH(peer_connection_wptr& weakConn, in_substream_)
	{
		peer_connection_sptr p = weakConn.lock();
		if (p&&p->is_connected() && p.get() == conn)
		{
			return true;
		}
	}
	return is_in_outgoing_substream(conn);
}

void stream_scheduling::modify_media_packet_header_before_send(media_packet&pkt,
	peer_connection* conn, bool isPush)
{
	seqno_t seqno = pkt.get_seqno();

	boost::uint64_t seqnoVec;
	char* pvec = (char*)&seqnoVec;
	BOOST_AUTO(const&infoLst, get_memory_packet_cache().recent_insert());
	BOOST_AUTO(itr, infoLst.begin());
	BOOST_AUTO(end, infoLst.end());
	size_t i = 0;
	for (; i < sizeof(seqnoVec) / sizeof(seqno_t) && itr != end; ++i, ++itr)
	{
		write_int_hton<seqno_t>(itr->m_seqno, pvec);
	}
	for (; i < sizeof(seqnoVec) / sizeof(seqno_t); ++i)
	{
		write_int_hton<seqno_t>(seqno, pvec);
	}
	pkt.set_is_push(isPush);
	pkt.set_recent_seqno_map(seqnoVec);
}

bool stream_scheduling::local_is_major_super_seed()
{
	if (local_is_super_seed())
	{
		double r = (double)stream_seed_->get_connection()->get_peer()->push_to_local_speed()
			/ ((double)stream_monitor_->get_incoming_speed() + FLT_MIN);
		return r <= 1.1&&r > 0.70;
	}
	return false;
}

void stream_scheduling::unsubscription()
{
	for (int i = 0; i < SUBSTREAM_CNT; ++i)
	{
		peer_connection_sptr conn = in_substream_[i].lock();
		if (conn)
		{
			if (conn->is_connected())
				unsubscribe(conn.get(), i);
			else
				in_substream_[i].reset();
		}
	}
}

void stream_scheduling::__init_seqno_i_care(seqno_t bigest)
{
	if (!get_bigest_sqno_i_know() || !get_smallest_seqno_i_care())
	{
		//这里，将数据请求的起始片段限制为当前所知最大片段号偏移最大MAX_BACK_OFFSET片
		//向后偏移MAX_BACK_OFFSET片的目的是使得请求初期可以在MAX_BACK_OFFSET片中进行
		//rarest first的调度。
		if (stream_monitor_)
			stream_monitor_->reset_to_player();

		timestamp_t now = timestamp_now();
		if (is_live())
		{
			enum{ step = 200 };
			int maxBackfetchCnt = std::min(get_memory_packet_cache().max_size() - 64, MAX_BACKFETCH_CNT);
			backfetch_cnt_ = std::min(maxBackfetchCnt, backfetch_msec()*(int)src_packet_rate_ / 1000);
			backfetch_cnt_ = ((backfetch_cnt_ + step - 1) / step)*step;
			if (backfetch_cnt_ > 2048)
				backfetch_cnt_ = 2048;
			BOOST_ASSERT(maxBackfetchCnt > 0);
		}
		else
		{
			backfetch_cnt_ = 0;
		}
		seqno_t smallest = bigest - (seqno_t)backfetch_cnt_;

		//调整smallest到iframe附近
		GUARD_CLIENT_SERVICE(;);
		BOOST_AUTO(trackerSptr, clientService->get_tracker_handler());
		if (is_live() && trackerSptr&&trackerSptr->iframe_list().size() > 0)
		{
			seqno_t preferSeqno = smallest;
			int minDis = 0xffffff;//足够大即可
			BOOST_AUTO(const&iframeList, trackerSptr->iframe_list());
			BOOST_FOREACH(seqno_t iframSeq, iframeList)
			{
				enum{ MAX_DIS = 256 };
				int dis = (int)abs(seqno_minus(iframSeq, smallest) + 50);//+50是倾向于找左侧的iframe
				if (dis < MAX_DIS)
				{
					if (minDis > dis)
					{
						preferSeqno = iframSeq;
						minDis = dis;
					}
				}
				else if (minDis <= MAX_DIS)
				{
					std::cout << "find i frame seqno: " << preferSeqno << std::endl;
					break;
				}
			}
			smallest = preferSeqno - 30;//iframe向后一定量片段，PAT包往往距离iframe在30 piece内
			backfetch_cnt_ = seqno_minus(bigest, smallest);
		}

		get_client_param_sptr()->smallest_seqno_i_care = smallest;
		get_client_param_sptr()->smallest_seqno_absenct = smallest;
		media_dispatcher_->set_smallest_seqno_i_care(smallest);
		buffer_manager_->set_bigest_seqno(bigest, backfetch_cnt_, now);


		SCHEDULING_DBG(
			std::cout << *get_smallest_seqno_i_care() << ", " << bigest << std::endl;
		);

	}
	else
	{
	}
}


NAMESPACE_END(p2client);