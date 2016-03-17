#include "client/stream/heuristic_scheduling_strategy.h"
#include "client/stream/stream_scheduling.h"
#include "client/stream/stream_monitor.h"
#include "client/stream/stream_seed.h"
#include "client/client_service.h"
#include "common/const_define.h"

NAMESPACE_BEGIN(p2client);

#if !defined(_SCHEDULING_DBG) && defined(NDEBUG)
#	define SCHEDULING_DBG(x) 
#else 
#	define SCHEDULING_DBG(x) x
#endif

#define GUARD_TOPOLOGY(returnValue) \
	BOOST_AUTO(topology, scheduling_->get_topology());\
	if (!topology) {return returnValue;}

#define GUARD_CLIENT_SERVICE(returnValue)\
	GUARD_TOPOLOGY(returnValue)\
	BOOST_AUTO(clientService, topology->get_client_service());\
	if (!clientService) {return returnValue;}

#define GUARD_CLIENT_SERVICE_LOGIC(returnValue)\
	GUARD_CLIENT_SERVICE(returnValue)\
	BOOST_AUTO(svcLogic, clientService->get_client_service_logic());\
	if (!svcLogic) {return returnValue;}


#ifdef POOR_CPU
enum{ kSelectLoopCount = 1 };
enum{ kOnlyUrgentInterval = 2 };
#else
enum{ kSelectLoopCount = 2 };
enum{ kOnlyUrgentInterval = 4 };
#endif

void heuristic_scheduling_strategy::set_urgent_degree::operator()(
	mutable_select_param&mprm, boost::optional<seqno_t>&smallestNotUrgent,
	const stream_monitor& streamMonitor, timestamp_t now, seqno_t seqno,
	int urgentTime)
{
	if (calced_)
		return;
	calced_ = true;
	if (smallestNotUrgent)
	{
		if (seqno_greater_equal(seqno, *smallestNotUrgent))
		{
			mprm.pieceState->urgentDegree = 0;
		}
		else
		{
			mprm.pieceState->urgentDegree = streamMonitor.urgent_degree(now, seqno, urgentTime);
			if (mprm.pieceState->urgentDegree <= 0 && seqno_less(seqno, *smallestNotUrgent))
				smallestNotUrgent = seqno;
		}
	}
	else
	{
		mprm.pieceState->urgentDegree = streamMonitor.urgent_degree(now, seqno, urgentTime);
		if (mprm.pieceState->urgentDegree <= 0)
			smallestNotUrgent = seqno;
	}
}

heuristic_scheduling_strategy::heuristic_scheduling_strategy(stream_scheduling& scheduling)
	: scheduling_(&scheduling)
	, select_piece_state_cache_(1024 * 4)
	, select_substream_state_cache_(SUBSTREAM_CNT)
	, select_because_of_elapse_meter_(seconds(2))
	, select_because_of_urgent_meter_(seconds(2))
	, schedule_packet_speed_(seconds(2))
	, min_request_cnt_(-1)
	, pull_real_started_(false)
	, loop_(0)
{
	temp_task_map_.resize(STREAM_NEIGHTBOR_PEER_CNT);

	typedef boost::make_unsigned<seqno_t>::type u_seqno_t;
	BOOST_ASSERT((~u_seqno_t(0) + (int64_t)1) % select_piece_state_cache_.size() == 0);
}

heuristic_scheduling_strategy::~heuristic_scheduling_strategy()
{
}

void heuristic_scheduling_strategy::stop()
{
	pull_real_started_ = false;
}

void heuristic_scheduling_strategy::start()
{
	loop_ = 0;
	pull_real_started_ = false;//初始状态是false
}

const scheduling_task_map& heuristic_scheduling_strategy::get_task_map()
{
	loop_++;
	task_map_.clear();
	if (!m_smallest_seqno_i_care)
		return task_map_;
	bool onlyUrgent = m_b_play_start && 0 == (loop_%kOnlyUrgentInterval);
	min_request_cnt_ = get_min_pull_size(onlyUrgent);
	//候选片段，候选时，要候选比min_request_cnt_更多的片片段
	select_candidate_piece(onlyUrgent);
	//printf("---------select_candidate_piece:%d, %d, %d\n", time_minus(timestamp_now(), m_now), min_request_cnt_, g_check_count);///
	SCHEDULING_DBG(
		bool print_real_scheduling_size = false;
	if (in_probability(0.03) || m_buffer_health < 0.95&&in_probability(0.035))
	{
		GUARD_CLIENT_SERVICE(task_map_);
		if (in_probability(0.75))
		{
			print_real_scheduling_size = true;
			if (m_smallest_seqno_i_care)
				std::cout << "&^^&^&^&^&^&^&^&^&^&^&^&^^&^^^&^&^&^&^&^&^&^&^&^&^("
				<< *m_smallest_seqno_i_care << ", " << *m_bigest_seqno_i_know << ")\n";
			else
				std::cout << "&^^&^&^&^&^&^&^&^&^&&^&^&^&^&^^&^&^&^&^&^&^&^&^&^&^\n";

			std::cout
				<< "onlineClinetCount=" << clientService->total_online_peer_cnt()
				<< " (" << clientService->peers().size() << ")"
				<< "\nneighborCount=" << topology->neighbor_count()
				<< "\nuploadSpeed=" << scheduling_->get_stream_monitor().get_outgoing_speed()
				//<<"\npushUploadSpeed="<<scheduling_->get_stream_monitor().get_push_outgoing_speed()
				<< "\npushedRate=" << scheduling_->get_stream_monitor().get_push_rate()
				<< "\nplayingQuality=" << scheduling_->get_stream_monitor().get_playing_quality()
				<< "\nbufferHealth=" << m_buffer_health
				<< "\nabsentCandidateCnt=" << scheduling_->get_absent_packet_list().size()
				<< "\ncandidates_size=" << candidates_buf_.size()
				<< "\nmin_request_cnt=" << min_request_cnt_
				<< "\ndelay_guarantee=" << m_buffer_duration
				<< "\nduplicate=" << scheduling_->get_stream_monitor().get_duplicate_rate()
				<< "\npull_pull_duplicate=" << scheduling_->get_stream_monitor().get_pull_pull_duplicate_rate()
				<< "\ngolobal_remote_to_local_lost_rate:" << global_remote_to_local_lost_rate()
				<< ", " << global_remote_to_local_speed()
				<< "\ngolobal_local_to_remote_lost_rate:" << global_local_to_remote_lost_rate()
				<< ", " << global_local_to_remote_speed();
			if (m_server_connection&&m_server_connection->is_connected())
				std::cout << "\nserver_to_local_lost_rate:" << m_server_connection->remote_to_local_lost_rate()
				<< ", local_to_server_lost_rate:" << m_server_connection->local_to_remote_lost_rate()
				<< ", server_rtt:" << m_server_connection->get_peer()->rtt()
				<< ", server_rtt_var:" << m_server_connection->get_peer()->rtt_var()
				<< ", type=" << m_server_connection->connection_category();
			std::cout << "\n";
		}
		else
		{
			topology->print_neighbors();
		}
	}
	);

	if (candidates_buf_.size() == 0)
		return task_map_;

	if (!pull_real_started_)
	{
		last_max_select_seqno_ = *m_smallest_seqno_i_care;
		last_check_requested_time_ = m_now;
		pull_real_started_ = true;
	}

	//确定最佳调度media片号及确定片号对应的主机（服务器or客户端）
	select_best_scheduling();
	int requestCnt = 0;
	BOOST_FOREACH(const piece& pce, best_scheduling_)
	{
		if (!pce.peer_in_charge&&!pce.b_disk_cached)
			break;

		BOOST_ASSERT(scheduling_->get_packet_info(pce.seqno) &&
			time_greater_equal(m_now, scheduling_->get_packet_info(pce.seqno)->m_pull_outtime)
			);
		BOOST_ASSERT(seqno_greater_equal(pce.seqno, *m_smallest_seqno_i_care));
		std::vector<seqno_t>& pieceVec = task_map_[pce.peer_in_charge];
		if (pieceVec.capacity() == 0)
			pieceVec.reserve(16);
		//当调度量不足min_request_cnt_时，就不管是不是向server请求。一旦调度量超过
		//min_request_cnt_时，就不再向server请求。
		if (requestCnt <= min_request_cnt_
			|| pce.peer_in_charge != m_server_connection.get()
			|| !m_b_play_start&&m_global_remote_to_local_lostrate < 0.25
			)
		{
			pieceVec.push_back(pce.seqno);
			++requestCnt;
			schedule_packet_speed_ += 1;
		}
	}

	SCHEDULING_DBG(;
	if (print_real_scheduling_size)
	{
		if (m_server_connection)
		{
			peer_sptr p = m_server_connection->get_peer();
			int residualCapacity = p->residual_tast_count();
			std::cout << "server_connection_residual=" << residualCapacity
				<< "\nserver_connection_taskcount=" << p->tast_count()
				<< "\n";
		}

		std::cout << "real_scheduling_size=" << requestCnt
			<< "\nselected_count=" << best_scheduling_.size()
			<< "\ncandidates_buf_size=" << candidates_buf_.size()
			<< "\nmedia_pkt_to_player_cache_size=" << m_buffer_size
			<< "\n";
	}
	else
	{
		//if (requestCnt<4)
		//{
		//	std::cout<<"************WHY? ***************** "
		//		<<requestCnt<<", "<<best_scheduling_.size()<<" , "<<min_request_cnt_
		//		<<std::endl;;
		//}
	}
	);

	return task_map_;
}

int heuristic_scheduling_strategy::get_min_pull_size(bool onlyUrgent)
{
	if (onlyUrgent)
		return m_src_packet_rate / 4;

	BOOST_ASSERT(m_src_packet_rate > 0);
	if (average_absent_media_packets_cnt_ < 0)
		average_absent_media_packets_cnt_ = scheduling_->get_absent_packet_list().size();
	else
		average_absent_media_packets_cnt_ = (scheduling_->get_absent_packet_list().size() + average_absent_media_packets_cnt_) / 2;

	//随着loop次数增多，milti减少.这里的multi是一个经验值，后面的代码在通过一些实时参数对此进行调整
	static const double MIN_MULTI = 2.5;
	static const double MAX_MULTI = MIN_MULTI * 2;
	double multi = bound(MIN_MULTI, (MAX_MULTI - 0.05*loop_), MAX_MULTI);

	//最低调度量
	double minRequestRate = multi*m_src_packet_rate - m_incoming_packet_rate - schedule_packet_speed_.bytes_per_second();

	//调度速度太低，很多片段处于absent状态
	if (average_absent_media_packets_cnt_>3 * m_src_packet_rate)
		minRequestRate *= (1.5 - m_global_remote_to_local_lostrate*m_global_remote_to_local_lostrate);
	if (m_buffer_size < 3 * average_absent_media_packets_cnt_)
		minRequestRate *= (1.5 - m_global_remote_to_local_lostrate*m_global_remote_to_local_lostrate);

	//buffer越健康，越是少调度
	minRequestRate *= (1.0 + 1.5*(1.0 - m_buffer_health));
	//playing_quality越健康，越是少调度
	if (m_playing_quality != 1.0)
		minRequestRate *= (1.0 + 1.5*(1.0 - m_playing_quality));
	//duplicate越高，越少调度
	if (m_duplicate_rate != 0.0)
		minRequestRate *= (1.0 - 0.5*m_duplicate_rate);

	//根据丢包率调整
	if (m_global_remote_to_local_lostrate < 0.1)//丢包率比较低，适当提升
		minRequestRate *= (1.5 - m_global_remote_to_local_lostrate);
	else if (m_global_remote_to_local_lostrate < 0.25)//丢包率高越多调度(因为丢包越高收到概率越低)
		minRequestRate /= (1.0 - m_global_remote_to_local_lostrate*m_global_remote_to_local_lostrate + FLT_MIN);
	else//丢包率太高，降低
		minRequestRate *= (1.0 - m_global_remote_to_local_lostrate*m_global_remote_to_local_lostrate + FLT_MIN);

	minRequestRate = bound<double>(m_src_packet_rate * 3 / 4, minRequestRate, multi*m_src_packet_rate);

	//和历史调度量平滑下
	int minRequestCnt = (int)ceil(minRequestRate*stream_scheduling::PULL_TIMER_INTERVAL / 1000.0);
	if (min_request_cnt_>0)
		minRequestCnt = (3 * min_request_cnt_ + minRequestCnt) / 4;
	if (!pull_real_started_)
		minRequestCnt = bound(8, minRequestCnt, 24);//第一次调度虽然要压制其他软件的带宽，但也不可盲目使用非常大的调度量

	//对于VoD，根据已经下载的缓存大小决定下载速度。
	if (!m_b_live)
	{
		if (m_buffer_size
			&&seqno_less_equal(m_smallest_seqno_in_buffer, *m_smallest_seqno_i_care)
			)
		{
			if (m_buffer_size > 2048)
			{
				minRequestCnt = 0;
			}
			if (m_buffer_size > 1024)
			{
				minRequestCnt = min_request_cnt_ * 1500 / m_buffer_size;
				minRequestCnt /= 2;
			}
		}
	}

	min_request_cnt_ = minRequestCnt;
	return minRequestCnt;
}

void heuristic_scheduling_strategy::set_substream_state(const stable_select_param& sprm,
	mutable_select_param& mprm)
{
	enum{ calc_substream_state_interval = 0 };//间隔，节省cpu
	BOOST_ASSERT(mprm.subStreamState);
	substream_state_cache& subStat = *mprm.subStreamState;
	BOOST_ASSERT(loop_ >= subStat.lastSetLoop);
	if ((loop_ - subStat.lastSetLoop) <= calc_substream_state_interval)
		return;

	stream_monitor& streamMonitor = scheduling_->get_stream_monitor();
	peer_connection* subConn = NULL;
	if (m_b_super_seed || (subConn = scheduling_->get_incoming_substream_conn(mprm.subStreamID)))
		subStat.dupRate = streamMonitor.get_push_pull_duplicate_rate(mprm.subStreamID);
	else
		subStat.dupRate = streamMonitor.get_pull_pull_duplicate_rate();
	subStat.averageDelay = streamMonitor.get_average_push_delay(mprm.subStreamID);
	subStat.varDelay = streamMonitor.get_variance_push_delay(mprm.subStreamID);
	subStat.dupeThresh = streamMonitor.get_dupe_thresh(mprm.subStreamID);
	subStat.delayThresh = streamMonitor.get_delay_thresh(mprm.subStreamID);
	subStat.pushRate = streamMonitor.get_push_rate(mprm.subStreamID);
	subStat.lastSetLoop = loop_;
	subStat.conn = subConn;

	if (m_b_super_seed || subConn)
	{
		if (m_b_super_seed)
		{
			subStat.lostRate = m_server_to_local_lostrate;
			subStat.aliveProbability = 1.0;
		}
		else
		{
			subStat.lostRate = subConn->remote_to_local_lost_rate();
			subStat.aliveProbability = subConn->alive_probability();
		}
		double alf = (1.0 - subStat.lostRate + subStat.dupRate + subStat.pushRate);
		double addVar = bound(1.0, alf, 2.0)*(subStat.delayThresh + subStat.varDelay);
		subStat.elapseThresh = subStat.averageDelay + addVar;
	}
	else
	{
		subStat.lostRate = 0;
		subStat.aliveProbability = 0;
		subStat.elapseThresh = 0;
	}
}

void heuristic_scheduling_strategy::set_stable_select_param(stable_select_param&s)
{
	stream_monitor& monitor = scheduling_->get_stream_monitor();
	int absentDuration = scheduling_->get_absent_packet_list().size() * 1000 / (m_src_packet_rate + 1);
	s.randSelectServerProb = 1.0 / ((m_neighbor_cnt ^ 4) - 10 + FLT_MIN);
	s.serverToLocalLostRate = (m_b_seed ? m_server_to_local_lostrate : 1.0);
	s.magnifyingPower = double(monitor.get_outgoing_speed()) / (monitor.get_incoming_speed() + FLT_MIN);
	s.urgentTime = m_b_play_start ?
		(scheduling_->urgent_time() + std::max(m_buffer_duration, absentDuration) / 4)
		: (scheduling_->urgent_time() + std::max(m_buffer_duration, 3 * absentDuration / 2) / 2);//播放器启动之前，倾向于认为片段都处于紧急状态
}

void heuristic_scheduling_strategy::set_mutable_select_param(mutable_select_param& m,
	seqno_t seqno)
{
	m.seqno = seqno;
	m.subStreamID = seqno%SUBSTREAM_CNT;
	m.pieceState = &get_slot(select_piece_state_cache_, seqno);
	m.pktInfo = scheduling_->get_packet_info(seqno);
	m.subStreamState = &select_substream_state_cache_[m.subStreamID];
	BOOST_ASSERT(m.pktInfo);
}

double heuristic_scheduling_strategy::select_score(const stable_select_param&sprm,
	mutable_select_param&mprm, int sellectLoopCnt)
{
	BOOST_ASSERT(kSelectLoopCount - 1 > 0);
	BOOST_ASSERT(ALIVE_GOOD_PROBABILITY > ALIVE_DROP_PROBABILITY);
	BOOST_ASSERT(((ALIVE_GOOD_PROBABILITY - ALIVE_DROP_PROBABILITY) / std::max(1.0, kSelectLoopCount - 1.0)) >= 0.0);

	BOOST_ASSERT(mprm.pktInfo);
	const absent_packet_info* pktInfo = mprm.pktInfo;
	const piece_state_cache& pieceState = *mprm.pieceState;
	const substream_state_cache& subStreamState = *mprm.subStreamState;
	const bool subscribing = (mprm.subStreamState->aliveProbability > 0);
	const double randSelectServerProb = sprm.randSelectServerProb;
	const double magnifyingPower = sprm.magnifyingPower;
	const seqno_t seqno = mprm.seqno;

	//如果平均到达hop越大，说明越是处于分发树的底层，则越少的向server请求
	bool isInteractive = is_interactive_category(scheduling_->get_client_param_sptr()->type);
	int hopCnt = isInteractive ? 10 : (int)bound(50.0, (scheduling_->average_hop(mprm.subStreamID) + 1) * 50, 300.0);
	bool notSelectServer = seqno_minus(*m_bigest_seqno_i_know, seqno) < hopCnt;

	//double alf = (double)(STREAM_NEIGHTBOR_PEER_CNT) / (m_neighbor_cnt + FLT_MIN)
	//	*(selectBecauseOfUrgentSpeed / (selectBecauseOfElapseSpeed + FLT_MIN));
	//alf = bound(5.0, alf, 20.0);

	double necessityScore = -1;
	if (subscribing
		&&mprm.subStreamState->conn
		&&!mprm.subStreamState->conn->get_peer()->is_ignored_subscription_seqno(seqno, mprm.subStreamID)
		)//处于订阅状态
	{
		double selectBecauseOfElapseSpeed = select_because_of_elapse_meter_.bytes_per_second();
		double selectBecauseOfUrgentSpeed = select_because_of_urgent_meter_.bytes_per_second();

		BOOST_ASSERT(mprm.pieceState->elapse == -0xfffffff || abs(mprm.pieceState->elapse) < 100 * 1000);
		bool lowSpeed = (m_incoming_packet_rate + selectBecauseOfUrgentSpeed + selectBecauseOfElapseSpeed) < m_src_packet_rate;

		//超过预期到达时间时
		bool bool1 = (
			(pieceState.elapse > subStreamState.elapseThresh
			|| lowSpeed&&pieceState.elapse > (subStreamState.elapseThresh - 2000)
			)
			&& (isInteractive
			|| pieceState.neighborScore > 0
			|| m_b_super_seed&&seqno_minus(*m_bigest_seqno_i_know, seqno) > (isInteractive ? 10 : 2 * m_src_packet_rate)//super推送只是经过server delay
			|| in_probability(std::min(0.1, average_absent_media_packets_cnt_ / 200.0))
			)
			&& subStreamState.dupRate <= std::min(2 * subStreamState.dupeThresh, 0.08)
			);

		//紧急时
		bool bool2 = (bool1 ||
			(pieceState.urgentDegree > stream_scheduling::DEFINIT_URGENT_DEGREE)
			);

		//判断为mustpull时
		bool bool3 = (bool2 ||
			(pktInfo->m_must_pull && time_less(pktInfo->m_pull_outtime, m_now))
			);

		//第一次调度或者播放器未启动
		bool bool4 = (bool3 ||
			(!pull_real_started_&&m_total_online_peer_cnt < 10
			|| false == m_b_play_start
			)
			);

		//父节点很可能已经掉线时
		static const double ka = (ALIVE_GOOD_PROBABILITY - ALIVE_DROP_PROBABILITY) / double(kSelectLoopCount - 1);
		bool bool5 = (bool4 ||
			subStreamState.aliveProbability < (ALIVE_DROP_PROBABILITY - ka + sellectLoopCnt*(ka / kSelectLoopCount))
			);

		bool isUrgent = (pieceState.urgentDegree > stream_scheduling::DEFINIT_URGENT_DEGREE);
		if (bool5)
		{
			if (bool1&&!lowSpeed)
				select_because_of_elapse_meter_ += 1;
			else if (bool2 &&!bool1)
				select_because_of_urgent_meter_ += 1;

			necessityScore = (isUrgent ? 1000 : 100.0*(m_b_super_seed ? 0.01 : pow(0.5, subStreamState.aliveProbability)));
			BOOST_ASSERT(necessityScore > 0);
		}

		SCHEDULING_DBG(
			static std::vector<int> debug_int(6, 0);
		if (bool1)debug_int[0]++;
		else if (bool2)debug_int[1]++;
		else if (bool3)debug_int[2]++;
		else if (bool4)debug_int[3]++;
		else if (bool5)debug_int[4]++;
		if (bool1&&in_probability(0.01))
		{
			std::cout << ".elapse=" << mprm.pieceState->elapse
				<< ", .elapseThresh=" << mprm.subStreamState->elapseThresh
				<< ", .averageDelay=" << mprm.subStreamState->averageDelay
				<< ", .dupRate=" << subStreamState.dupRate
				<< ", -------" << mprm.subStreamID
				<< "\n";
		}
		if (in_probability(0.001))
		{
			for (size_t i = 0; i < debug_int.size(); ++i)
				std::cout << " ----- " << debug_int[i];
			std::cout << " -----\n";
		}
		);
	}
	else
	{
		//有合作节点持有本片段，或者放大率较大，或者健康度较低，或者虽然只有server有这片段但待调度包实在多时候
		bool bool1 = (pieceState.neighborScore>0
			|| in_probability(magnifyingPower) || in_probability((1.0 - 0.2*m_buffer_health*m_buffer_health))
			|| !notSelectServer&&in_probability(std::min(0.5, average_absent_media_packets_cnt_ / 200.0))
			|| in_probability(std::min(0.2, average_absent_media_packets_cnt_ / 200.0))
			);

		//紧急时
		bool bool2 = (bool1 ||
			pieceState.urgentDegree > stream_scheduling::DEFINIT_URGENT_DEGREE
			|| pieceState.urgentDegree > 0 && in_probability(pieceState.urgentDegree)
			);//urgent

		//判断为must_pull
		bool bool3 = (bool2 ||
			(pktInfo->m_must_pull
			&&time_less(pktInfo->m_pull_outtime, m_now)
			)
			);

		//第一次调度或者播放器未启动
		bool bool4 = (bool3 ||
			!pull_real_started_
			|| !m_b_play_start
			);

		//is_in_subscription_state_为false时
		bool bool5 = (bool4 ||
			!m_b_inscription_state && (m_neighbor_cnt < 3 && in_probability(0.25))
			);

		bool isUrgent = pieceState.urgentDegree > stream_scheduling::DEFINIT_URGENT_DEGREE;
		if (bool5)
		{
			necessityScore = (isUrgent ? 1000 : (pieceState.neighborScore <= 0 ? 10 : 100));
			BOOST_ASSERT(necessityScore > 0);
		}

		SCHEDULING_DBG(
			static std::vector<int> debug_int(6, 0);
		if (bool1) debug_int[0]++;
		else if (bool2)debug_int[1]++;
		else if (bool3)debug_int[2]++;
		else if (bool4)debug_int[3]++;
		else if (bool5)debug_int[4]++;
		if (in_probability(0.001))
		{
			for (size_t i = 0; i < debug_int.size(); ++i)
				std::cout << " ~~~~~~ " << debug_int[i];
			std::cout << " ~~~~~~\n";
		}
		)
	}

	mprm.pieceState->necessityScore = necessityScore;

	return necessityScore;
}

double heuristic_scheduling_strategy::scheduling_score(const peer& p, const task_peer_param& param,
	double alf, bool isServer, piece& pic)
{
	enum{ GIVE_SCORE = 1000 };

	double rTT = p.rtt(), rttVar = p.rtt_var();
	//这是片段可到达的概率
	double getProbability = param.aliveProbability*(
		(1.0 - param.remoteToLocalLostRate / std::max(1, param.residualTastCount))//剩余能力越大这个包丢的概率越小
		+ std::pow(1.0 - param.remoteToLocalLostRate, param.taskCnt)//任务越多，这个包丢的概率越大
		);
	double score = getProbability
		+ (rTT + 100.0) / (rttVar + 100.0)//抖动越大越不倾向于调度
		+ 100.0 / (rTT + 100.0)//延迟越大，越不倾向于调度
		+ ((1.0 - param.remoteToLocalLostRate)*p.upload_to_local_speed()) / 1000000.0
		;
	score /= 3;
	score *= score;
	score *= 10000 * alf;
	if (rTT + rttVar > 1000)
		score = (score * 1000) / (rTT + rttVar);

	BOOST_ASSERT(score > 0);

	if (pic.b_urgent)
	{
		pic.shceduling_priority = pic.select_priority + score + GIVE_SCORE;
	}
	else
	{
		pic.shceduling_priority = pic.select_priority + score;
		if (isServer&&is_time_passed(15000, m_scheduling_start_time, m_now))
		{
			pic.shceduling_priority = pic.shceduling_priority
				*(STREAM_NEIGHTBOR_PEER_CNT - m_neighbor_cnt + 1) / STREAM_NEIGHTBOR_PEER_CNT;//尽量不选择server，每次选择到，如果不是紧急就不加分
		}

	}
	return pic.shceduling_priority;
}

void heuristic_scheduling_strategy::check_requested(const stable_select_param&s,
	seqno_t maxCheckSeqno,
	int maxCheckCnt)
{
	enum{ check_requested_interval = 200 };
	if (loop_ > 100 && !is_time_passed(check_requested_interval, last_check_requested_time_, m_now))
		return;
	last_check_requested_time_ = m_now;

	int cnt = 0;
	int fastRequest = 4;
	boost::optional<seqno_t> smallestNotUrgent;
	absent_packet_list& absentList = scheduling_->get_absent_packet_list();
	BOOST_AUTO(itrEnd, absentList.end(true));
	for (BOOST_AUTO(itr, absentList.begin(true)); itr != itrEnd;)
	{
		const seqno_t seqno = *itr;

		if (seqno_greater(seqno, maxCheckSeqno)
			&& ++cnt > maxCheckCnt
			)
		{
			break;
		}

		mutable_select_param mprm;
		set_mutable_select_param(mprm, seqno);//设定这个片段的param
		BOOST_ASSERT(mprm.pktInfo);

		BOOST_ASSERT(time_less_equal(mprm.pktInfo->m_pull_time, m_now));//确实请求了而非订阅了

		//这个包是严重过时的
		if (seqno_greater(*m_smallest_seqno_i_care, seqno)
			|| !mprm.pktInfo->is_this(seqno, m_now)
			)
		{
			SCHEDULING_DBG(;
			std::cout << "-requesting- the packet of " << seqno << " lost."
				<< " smallest_seqno_i_care=" << *m_smallest_seqno_i_care
				<< " " << seqno_greater(*m_smallest_seqno_i_care, seqno)
				<< std::endl
				<< "m_pull_cnt=" << mprm.pktInfo->m_pull_cnt
				<< " m_pull_accumulator: mean=" << boost::accumulators::mean(absent_packet_info::s_pull_accumulator)
				<< " ";
			BOOST_FOREACH(BOOST_TYPEOF(mprm.pktInfo->m_pull_edps)::const_reference edpInfo, mprm.pktInfo->m_pull_edps)
			{
				std::cout << edpInfo.edp << " " << edpInfo.t << "|";
			}
			std::cout << std::endl;
			);

			absentList.erase(itr++, true);
			continue;
		}

		//刚检查过，跳过
		if (!is_time_passed(check_requested_interval / 2, mprm.pktInfo->m_last_check_peer_incharge_time, m_now))
		{
			++itr;
			continue;
		}
		mprm.pktInfo->m_last_check_peer_incharge_time = m_now;

		//检查是不是请求失败
		peer_connection_sptr peerIncharge;
		bool requestFailed = false;
		if (!mprm.pktInfo->m_dskcached//diskcached的没有connection，只检查超时就好
			&& (peerIncharge = mprm.pktInfo->m_peer_incharge.lock())
			&& peerIncharge->is_connected()
			)
		{
			int currRto = peerIncharge->get_peer()->rto();
			int diff = currRto - mprm.pktInfo->m_last_rto;
			diff = bound(-1000, diff, 1000);
			diff /= 5;
			//if (diff<0||time_minus(mprm.pktInfo->m_pull_outtime+diff, mprm.pktInfo->m_pull_time)<std::min(currRto, 5000))
			{
				mprm.pktInfo->m_pull_outtime += diff;
			}
			mprm.pktInfo->m_last_rto = currRto;
			if (time_greater_equal(m_now, mprm.pktInfo->m_pull_outtime))
			{
				requestFailed = true;
			}
			else
			{
				set_urgent_degree()(mprm, smallestNotUrgent, scheduling_->get_stream_monitor(), m_now, seqno, s.urgentTime);
				if (mprm.pieceState->urgentDegree > 0)
				{
					double degree = std::max<double>(0.5, mprm.pieceState->urgentDegree);
					if (is_time_passed(currRto*(1.5 - degree), mprm.pktInfo->m_pull_time, m_now)
						&& m_duplicate_rate < degree*0.1
						&&--fastRequest >= 0
						)
					{
						//马上就要播放，而且请求了较长时间了。宁愿下载重复，也要设置为timeout
						requestFailed = true;
					}
				}
			}
		}
		else if (!mprm.pktInfo->m_dskcached)
		{
			BOOST_ASSERT((!peerIncharge || !peerIncharge->is_connected()));
			requestFailed = true;
		}

		BOOST_ASSERT(
			time_minus(mprm.pktInfo->m_pull_outtime, mprm.pktInfo->m_pull_time) < 10000
			);

		if (requestFailed == false
			&& time_greater_equal(m_now, mprm.pktInfo->m_pull_outtime)
			)
		{
			requestFailed = true;
		}

		if (requestFailed)
			absentList.request_failed(itr++, m_now, -1);
		else
			++itr;
	}
}

int heuristic_scheduling_strategy::select_candidate_piece(bool onlyUrgent)
{
	/*
	优先权 priority计算为：稀有性得分+紧急性得分+请求必要性得分+随机抖动
	*/
	enum{
		GIVE_MARK = 100, //送分，保证score为非负
		UNKNOWN_ELAPSE = -0xfffffff
	};
	BOOST_STATIC_ASSERT(UNKNOWN_ELAPSE < 0);

	candidates_buf_.clear();

	if (scheduling_->get_absent_packet_list().size(false) == 0)
		return 0;

	if (!m_smallest_seqno_i_care || !m_bigest_seqno_i_know)
		return 0;

	int maxCnt = (min_request_cnt_ >= 8 && m_global_remote_to_local_lostrate < stream_scheduling::LOST_RATE_THRESH)
		? (min_request_cnt_ * 3 / 2) : (min_request_cnt_ * 2)
		;

	//控制片段选择范围
	std::pair<seqno_t, seqno_t> candidateSeqPair = bigest_select_seqno();
	seqno_t maxCandidateSeq = candidateSeqPair.first;
	//seqno_t maxCandidateSeqThresh = candidateSeqPair.second;
	const int addCnt = 4 * stream_scheduling::PULL_TIMER_INTERVAL / 10;
	const int maxCandidateAdd = (m_b_play_start ? 100 : (addCnt + addCnt*(1.0 - m_buffer_health)));//(justStart?20:std::min((int)media_pkt_to_player_cache_.size(), 100));
	peer_connection_sptr peerIncharge;
	boost::optional<seqno_t> smallestNotUrgent;
	stable_select_param sprm;
	set_stable_select_param(sprm);
	check_requested(sprm, maxCandidateSeq + maxCandidateAdd, std::max(maxCnt, 100));
	seqno_t firstSeqno = *scheduling_->get_absent_packet_list().begin(false);
	seqno_t stateCacheSeqno = firstSeqno - 1;
	int i = 0;
	DEBUG_SCOPE(std::set<seqno_t> seqno_set_for_test);
	for (; i < kSelectLoopCount; ++i)//这层循环的目的是逐渐放宽选择阈值，直到选择出足够的片段
	{
		int continueNotSelectCnt = 0;
		int checkCnt = 0;
		int maxAdd = maxCandidateAdd;
		BOOST_AUTO(itrEnd, scheduling_->get_absent_packet_list().end(false));
		for (BOOST_AUTO(itr, scheduling_->get_absent_packet_list().begin(false));
			itr != itrEnd;
			)
		{
			set_urgent_degree do_set_urgent_degree;
			const seqno_t seqno = *itr;
			checkCnt++;

			//选择范围过大，超出select_piece_state_cache_的大小会造成片段状态覆盖
			if (seqno_minus(seqno, firstSeqno) >= (int)select_piece_state_cache_.size())
				break;

#ifdef POOR_CPU
			WARNING("POOR_CPU defined, check max 100 pieces");
			if (m_b_inscription_state&&checkCnt > 100 && get_sys_cpu_usage(m_now) > 95)
				break;
#endif

			//检查是不是不用重复计算select_param
			BOOST_AUTO(&pieceState, get_slot(select_piece_state_cache_, seqno));
			if (seqno_less(stateCacheSeqno, seqno) || pieceState.seqno != seqno)
			{
				pieceState.selectFlag = piece_state_cache::NOT_SURE;//是否选择的状态还不确定
				pieceState.seqno = seqno;
				stateCacheSeqno = seqno;
			}
			else if (pieceState.selectFlag)//是否已经选择有定论了
			{
				BOOST_ASSERT(seqno_less_equal(seqno, stateCacheSeqno));
				++itr;
				continue;
			}

			mutable_select_param mprm;
			set_mutable_select_param(mprm, seqno);//设定这个片段的param
			set_substream_state(sprm, mprm);
			BOOST_ASSERT(&pieceState == mprm.pieceState);
			//这个包是严重过时的
			if (seqno_greater(*m_smallest_seqno_i_care, seqno)
				|| !mprm.pktInfo->is_this(seqno, m_now)
				)
			{
				SCHEDULING_DBG(
					std::cout << "the packet of " << seqno << " lost." << *m_smallest_seqno_i_care
					<< " " << seqno_greater(*m_smallest_seqno_i_care, seqno) << std::endl;
				);
				scheduling_->get_absent_packet_list().erase(itr++, false);
				continue;
			}
			else
			{
				++itr;
			}

			//剩余时间过大，临时不需要下载
			if (seqno_greater(seqno, maxCandidateSeq))
			{
				if ((int)candidates_buf_.size() < maxCnt / 2 && maxAdd >= 0)
				{
					maxCandidateSeq += 2;
					maxAdd -= 2;
				}
				else if (checkCnt > 100)
				{
					SCHEDULING_DBG(
						std::cout << "XXXXX select break, because of seqno_greater(seqno, maxCandidateSeq)\n";
					);
					break;
				}
			}
			BOOST_ASSERT(piece_state_cache::NOT_SURE == mprm.pieceState->selectFlag);
			if ((!m_b_seed) && mprm.pktInfo->m_owners.empty()//非seed节点不能向服务器请求
				|| time_less(m_now, mprm.pktInfo->m_pull_outtime)//请求过，不到超时
				)
			{
				mprm.pieceState->selectFlag = piece_state_cache::SURE_NOT_SELECT;
				continue;
			}

			do_set_urgent_degree(mprm, smallestNotUrgent, scheduling_->get_stream_monitor(),
				m_now, seqno, sprm.urgentTime);
			//订阅态下，elapse才有意义
			if (!m_b_inscription_state
				|| !scheduling_->get_stream_monitor().piece_elapse(m_now, seqno, mprm.pieceState->elapse)
				)
			{
				mprm.pieceState->elapse = UNKNOWN_ELAPSE;
			}

			int ownerCnt = (int)mprm.pktInfo->m_owners.size();
			mprm.pieceState->neighborScore = ownerCnt;
			if (1 == ownerCnt)//1个合作节点时候，要检查这个节点是不是还活着
			{
				peer_connection_sptr conn = mprm.pktInfo->m_owners.random_select();
				if (!conn || !conn->is_connected())
				{
					//反正就一个，清理掉
					mprm.pktInfo->m_owners.reset();
					mprm.pieceState->neighborScore = 0;
				}
				else
				{
					double remoteToLocalLostRate = conn->remote_to_local_lost_rate();
					double aliveProbability = conn->alive_probability();
					int residualTastCount = conn->get_peer()->residual_tast_count();
					//不超过节点的剩余上行能力, 或者实在选不出
					if (residualTastCount > 0
						&& aliveProbability >= ALIVE_GOOD_PROBABILITY
						&&remoteToLocalLostRate <= stream_scheduling::LOST_RATE_THRESH*0.75
						)
					{
						mprm.pieceState->neighborScore = 1;
					}
					else
					{
						mprm.pieceState->neighborScore = 0;
					}
				}
			}

			BOOST_ASSERT(mprm.pieceState->selectFlag == piece_state_cache::NOT_SURE);
			BOOST_ASSERT(time_greater_equal(m_now, mprm.pktInfo->m_pull_outtime));

			if (onlyUrgent&&mprm.pieceState->urgentDegree <= 0)
			{
				maxCnt = candidates_buf_.size();
				break;
			}

			double necessityScore = select_score(sprm, mprm, i);
			bool selectIt = (necessityScore > 0);
			if (selectIt)
			{
				continueNotSelectCnt = 0;

				BOOST_ASSERT(scheduling_->get_packet_info(seqno) &&
					time_greater_equal(m_now, scheduling_->get_packet_info(seqno)->m_pull_outtime));

				//randomScore是足够小的波动
				double randomScore = (1e-2) - (1e-4)*(random(0, m_b_play_start ? 50 : 5) + seqno_minus(seqno, *m_smallest_seqno_i_care));

				//rareScore满分100
				double rareScore = 100;
				//播放前，倾向使用random调度
				if (m_b_play_start)
				{
					rareScore = (mprm.pktInfo->m_owners.size() < 1) ?
						100.0 / (STREAM_NEIGHTBOR_PEER_CNT) : 100.0 / mprm.pktInfo->m_owners.size();
				}

				//对urgent片段赋予较大的值
				bool isUrgent = (mprm.pieceState->urgentDegree > stream_scheduling::DEFINIT_URGENT_DEGREE);
				double urgentScore = (isUrgent ? 10 * GIVE_MARK : 100)*mprm.pieceState->urgentDegree;
				BOOST_ASSERT(urgentScore >= 0);

				piece candidatePiece;
				candidatePiece.seqno = seqno;
				candidatePiece.b_urgent = isUrgent;
				candidatePiece.select_priority = candidatePiece.shceduling_priority =
					randomScore - (1e-2)
					+ (rareScore + urgentScore + necessityScore + GIVE_MARK*(1.0 - mprm.subStreamState->dupRate)) / 4;//非urgent满分100
				BOOST_ASSERT(isUrgent || candidatePiece.select_priority / 100.0 < 1.1);

				mprm.pieceState->selectFlag = piece_state_cache::SURE_SELECT;
				BOOST_ASSERT(pieceState.selectFlag == mprm.pieceState->selectFlag);

				//如果是因为neighborScore大选择到这个片段，则不倾向于使用server
				if (mprm.pieceState->neighborScore>0 && mprm.pieceState->urgentDegree <= 0.0
					&&pull_real_started_&&m_b_play_start&&m_buffer_health > 0.90
					&&m_playing_quality >= 1.0
					&&sprm.magnifyingPower < 0.5
					&& (int)scheduling_->get_absent_packet_list().size() * 5 < m_buffer_size
					)
				{
					candidatePiece.b_not_necessary_to_use_server = true;
				}
				else
				{
					candidatePiece.b_not_necessary_to_use_server = false;
				}

				candidates_buf_.push_back(candidatePiece);
				DEBUG_SCOPE(
					seqno_set_for_test.insert(candidatePiece.seqno);
				BOOST_ASSERT(candidates_buf_.size() == seqno_set_for_test.size());
				);

				if (candidates_buf_.size() >= scheduling_->get_absent_packet_list().size()
					|| (int)candidates_buf_.size() >= maxCnt)
					break;
			}
			else
			{
				//candidatePiece.select_priority=candidatePiece.shceduling_priority=0;
				if (continueNotSelectCnt++ > SUBSTREAM_CNT)
				{
					break;
				}
			}
		}

		if (candidates_buf_.size() >= scheduling_->get_absent_packet_list().size()
			|| (int)candidates_buf_.size() >= maxCnt
			)
		{
			break;
		}
	}

	if (!pull_real_started_&&candidates_buf_.empty()
		&& scheduling_->get_absent_packet_list().size())
	{
		BOOST_AUTO(itrEnd, scheduling_->get_absent_packet_list().end(false));
		for (BOOST_AUTO(itr, scheduling_->get_absent_packet_list().begin(false));
			itr != itrEnd && (int)candidates_buf_.size() < min_request_cnt_;
			++itr)
		{
			const seqno_t seqno = *itr;
			mutable_select_param mprm;
			set_mutable_select_param(mprm, seqno);//设定这个片段的param

			BOOST_ASSERT(seqno_greater_equal(seqno, *m_smallest_seqno_i_care));
			//randomScore是足够小的波动
			double randomScore = (1e-2) - (1e-4)*(random(0, m_b_play_start ? 50 : 5) + seqno_minus(seqno, *m_smallest_seqno_i_care));
			//rareScore满分100
			double rareScore = 100;
			//播放前，倾向使用random调度
			if (m_b_play_start)
			{
				int ownerSize = mprm.pktInfo->m_owners.size();
				rareScore = ((ownerSize < 1) ? 1.0 / (STREAM_NEIGHTBOR_PEER_CNT / 2) : 1.0 / ownerSize);
				//以rarest fires 调度算法为主, 满分100
				rareScore *= (100 * STREAM_NEIGHTBOR_PEER_CNT);
			}
			piece candidatePiece;
			candidatePiece.seqno = seqno;
			candidatePiece.b_urgent = false;
			candidatePiece.select_priority = candidatePiece.shceduling_priority
				= randomScore + rareScore + GIVE_MARK;
			candidates_buf_.push_back(candidatePiece);
		}
	}
	std::sort(candidates_buf_.begin(), candidates_buf_.end(), piece_prior());
	return i;
}

void heuristic_scheduling_strategy::select_best_scheduling()
{
	enum{ GIVE_SCORE = 1000 };

	best_scheduling_.clear();
	bool localIsSeed = m_b_seed;
	//double globalRemoteToLocalLostratePow2=m_global_remote_to_local_lostrate*m_global_remote_to_local_lostrate+FLT_MIN;
	//整体丢包率过高，则认为是和其他软件竞争带宽，此时，要aggressive
	bool aggressive = (m_global_remote_to_local_lostrate > stream_scheduling::AGGRESSIVE_THREASH);
	double best_total_priority = 0;
	int average_pkt_size = scheduling_->get_stream_monitor().get_average_packet_size();
	int selected_count = 0;
	double lost_rate_thresh = (m_global_remote_to_local_lostrate + stream_scheduling::LOST_RATE_THRESH) / 2.0;
	peer *p = NULL, *serverPeer = NULL;
	timestamp_t scheduling_start_time = m_scheduling_start_time;
	peer_connection* lastSelected = NULL;
	task_peer_param serverParam;
	if (localIsSeed)
	{
		serverPeer = m_server_connection->get_peer().get();
		serverParam.remoteToLocalLostRate = m_server_connection->remote_to_local_lost_rate();
		serverParam.localToRemoteLostRate = m_server_connection->local_to_remote_lost_rate();
		serverParam.aliveProbability = m_server_connection->alive_probability();
		serverParam.residualTastCount = serverPeer->residual_tast_count();
		serverParam.inited = true;

		//检查udp/tcp通信限制
		if (serverParam.localToRemoteLostRate > 0.45
			&&serverParam.remoteToLocalLostRate > 0.45
			//&&m_server_connection->connection_category()==peer_connection::UDP
			)
		{
			localIsSeed = false;
			serverPeer->set_udp_restricte(!serverPeer->is_udp_restricte());
			scheduling_->get_io_service().post(
				boost::bind(&peer_connection::close, m_server_connection, true)
				);
		}
	}

	//重置init标识
	BOOST_FOREACH(task_peer_param& tskParam, temp_task_map_)
	{
		tskParam.inited = false;
	}

	//随机多次
#ifdef POOR_CPU
	enum{ MAX_TRY = 2 };
#else
	enum{ MAX_TRY = 2 };
#endif
	double moreAgressive = bound(-0.1, double(m_src_packet_rate - m_incoming_packet_rate) / m_src_packet_rate, 0.3);
	int selectedCnt = 0;
	for (int c = 0; c <= MAX_TRY; c++)
	{
		bool lastChance = c == MAX_TRY;
		if (lastChance&&selected_count >= min_request_cnt_)
		{
			break;
		}

		int selectedCnt = 0;
		double temp_total_priority = 0;
		size_t i = 0;
		if (lastChance)
		{ //复用前几次的最好选择结果
			candidates_buf_.swap(best_scheduling_);
			BOOST_FOREACH(piece& pic, candidates_buf_)
			{
				if (!pic.peer_in_charge)
				{
					break;
				}
				else
				{
					++selectedCnt;
					temp_total_priority += pic.shceduling_priority;
					++i;
				}
			}
		}
		else
		{
			//重置分配给某节点的上行任务字节数
			BOOST_FOREACH(task_peer_param& tskParam, temp_task_map_)
			{
				tskParam.taskCnt = 0;
			}
			serverParam.taskCnt = 0;
		}

		for (; i < candidates_buf_.size(); ++i)
		{
			piece& pic = candidates_buf_[i];
			pic.peer_in_charge = NULL;
			pic.b_disk_cached = false;
			absent_packet_info* pktInfo = scheduling_->get_packet_info(pic.seqno);
			BOOST_ASSERT(pktInfo);
			//有DiskCache的，直接选择。
			if (pktInfo->m_dskcached)
			{
				BOOST_ASSERT(!m_b_live);
				pic.b_disk_cached = true;
				if (pic.b_urgent)
					pic.shceduling_priority = pic.select_priority + GIVE_SCORE;

				temp_total_priority += pic.shceduling_priority;

				if (++selectedCnt > 3 * min_request_cnt_ / 2)
					break;
				continue;
			}
			BOOST_ASSERT(time_greater_equal(m_now, pktInfo->m_pull_outtime));

			struct
			{
				void operator ()(std::vector<task_peer_param>& taskMap,
					const peer_connection_sptr& conn)const
				{
					task_peer_param& param = get_slot(taskMap, (size_t)conn->local_id());
					if (!param.inited)
					{
						peer* p = conn->get_peer().get();
						param.remoteToLocalLostRate = conn->remote_to_local_lost_rate();
						param.localToRemoteLostRate = conn->local_to_remote_lost_rate();
						param.remoteToLocalLostRatePow2 = param.remoteToLocalLostRate*param.remoteToLocalLostRate;
						if (param.remoteToLocalLostRate < 0.1)
							param.maxAppendTaskCnt = std::min(5.0, 0.1 / (param.remoteToLocalLostRate + FLT_MIN));
						else
							param.maxAppendTaskCnt = -std::min(5.0, (param.remoteToLocalLostRate + FLT_MIN) / 0.2);
						param.aliveProbability = conn->alive_probability();
						param.residualTastCount = p->residual_tast_count();

						param.inited = true;
					}
				}
			}task_peer_param_initor;

			//从具有该片段的节点中随机选择一个有剩余上行能力的节点
			bool flag = false;
			size_t try_times = pktInfo->m_owners.size() + 2;
			const double kTryMulti = (pktInfo->m_owners.size()) / (double)(try_times);
			size_t which = pktInfo->m_owners.size() ? (p2engine::random() % pktInfo->m_owners.size()) : 0;
			peer_connection_sptr conn;
			peer_connection_sptr bestConn;
			double bestScore = -10000;
			bool bestSelected = false;
			for (size_t k = 0; k < try_times; ++k)
			{
				if (pktInfo->m_owners.empty())
					break;
				if (k == try_times - 1)
				{
					bestSelected = true;
					conn = bestConn;//直接使用最好的conn
				}
				else if (k > 0)
				{
					size_t j = (which++) % pktInfo->m_owners.size();
					conn = pktInfo->m_owners.select(j);
				}
				else
				{
					conn = pktInfo->m_owners.random_select();
				}

				if (!conn || !conn->is_connected() || conn.get() == scheduling_->get_prepare_erase_connection())
				{
					if (conn)
					{
						pktInfo->m_owners.erase(conn);
						conn.reset();
					}
					continue;
				}
				DEBUG_SCOPE(
					error_code ec;
				BOOST_ASSERT(conn&&conn->is_connected());
				BOOST_ASSERT(conn->remote_endpoint(ec).port());
				);

				if (lastSelected == conn.get() && k<pktInfo->m_owners.size() / 2)
				{
					//不倾向选同一个节点，避免请求集中到一个目标节点，一旦目标节点掉线，就可能带来不顺畅。
					task_peer_param& param = get_slot(temp_task_map_, (size_t)conn->local_id());
					if (param.inited&&param.remoteToLocalLostRate>0.15)
					{
						--k;
						lastSelected = NULL;
						continue;
					}
				}

				BOOST_ASSERT(conn&&conn->is_connected());
				if (conn != m_server_connection
					&& (p = conn->get_peer().get())
					&& (!p->smallest_cached_seqno() || seqno_greater(pic.seqno, p->smallest_cached_seqno().get()))
					)
				{
					BOOST_ASSERT(conn->local_id() < (int)temp_task_map_.size());
					task_peer_param_initor(temp_task_map_, conn);
					task_peer_param& param = get_slot(temp_task_map_, (size_t)conn->local_id());
					double alf = (1.25 - param.remoteToLocalLostRatePow2 / (m_global_remote_to_local_lostrate + FLT_MIN));
					alf = bound(0.80, alf, 1.25) + std::min(moreAgressive, 0.3);
					double residualCapacity = alf*param.residualTastCount - param.taskCnt - 1;
					double avaliveProbabilityiliThresh = ALIVE_GOOD_PROBABILITY;
					double lostRateThresh = lost_rate_thresh / (try_times - k); //逐步容许较大的丢包率, 否则param.remoteToLocalLostRate<=lostRateThresh一次不通过则tryTimes都不通过
					int rtoThresh = 3000;

					if (pic.b_urgent)
					{
						avaliveProbabilityiliThresh = ALIVE_VERY_GOOD_PROBABILITY;
						lostRateThresh = lost_rate_thresh*0.75;
						rtoThresh = 2000;
					}
					else if (k >= pktInfo->m_owners.size() / 2/*&&c>=MAX_TRY*/)
					{
						avaliveProbabilityiliThresh = ALIVE_GOOD_PROBABILITY - 0.2;
						double lostAlf = (1.0 / kTryMulti)*std::min(kTryMulti, ((double)k / pktInfo->m_owners.size()));;
						lostRateThresh = std::max(lost_rate_thresh*lostAlf, localIsSeed ? serverParam.remoteToLocalLostRate : 0);
						rtoThresh = 6000;
						int times = (int)(bool)(aggressive)+(int)(bool)(c == MAX_TRY&&selected_count < min_request_cnt_);
						double threshValue = (1.0 + m_global_remote_to_local_lostrate*0.5 + 0.9 - m_buffer_health);
						for (int j = 0; j<times; j++)
						{
							lostRateThresh *= threshValue;
							residualCapacity += 1;
						}
						if (bestSelected
							&& (!localIsSeed || serverParam.remoteToLocalLostRate>stream_scheduling::LOST_RATE_THRESH*0.9)//server丢包率太高
							&& scheduling_->get_absent_packet_list().size() > 300
							)
						{
							lostRateThresh = std::max(lost_rate_thresh, lostRateThresh);
						}
					}

					residualCapacity += param.maxAppendTaskCnt;
					if (residualCapacity >= 0
						&& param.aliveProbability >= avaliveProbabilityiliThresh
						&& (
						param.remoteToLocalLostRate <= lostRateThresh
						|| param.remoteToLocalLostRate <= serverParam.remoteToLocalLostRate
						|| param.remoteToLocalLostRate <= m_global_remote_to_local_lostrate
						|| in_probability(1.0 - m_global_remote_to_local_lostrate*(try_times - k))
						)
						&& (
						lastChance
						|| k >= pktInfo->m_owners.size() / 2
						|| (!p->is_rtt_accurate() || p->rto() <= rtoThresh)
						)
						)
					{//不超过节点的剩余上行能力, 或者实在选不出						
						lastSelected = conn.get();
						pic.peer_in_charge = conn.get();
						param.taskCnt += 1;
						temp_total_priority += scheduling_score(*p, param, alf, false, pic);
						flag = true;
						break;
					}
					else
					{
						//顺便找出最好的conn
						double tempScore = (1.0 - param.remoteToLocalLostRate)
							*(param.residualTastCount + param.maxAppendTaskCnt) - param.taskCnt;
						if (tempScore > bestScore
							&&param.remoteToLocalLostRate <= lost_rate_thresh
							&&param.aliveProbability >= ALIVE_GOOD_PROBABILITY - 0.2
							)
						{
							bestConn = conn;
							bestScore = tempScore;
						}

						SCHEDULING_DBG(;
						//error_code ec;
						//std::cout<<"----------{{\n";
						//std::cout<<conn->remote_endpoint(ec)
						//	<<", residual:"<<residualCapacity
						//	<<", tastCount:"<<p->tast_count()
						//	<<", aliveProbability:"<<param.aliveProbability<<", "<<avaliveProbabilityiliThresh
						//	<<", lostRate:"<<param.remoteToLocalLostRate
						//	<<"\n";
						//std::cout<<"--------------}}\n";
						);
					}
				}
				else
				{
					pktInfo->m_owners.erase(conn);
				}
			}
			DEBUG_SCOPE(;
			if (false == flag&&c == 0 && pktInfo->m_owners.size() && in_probability(0.001))
			{
				std::cout << "\n\n----------------NO-NEIGHBOR-SELECT[[[[[[[[[[[[[[[[[[[[\n"
					<< "lost_rate_thresh:" << lost_rate_thresh << ", is_urgent:" << pic.b_urgent << "\n";
				pktInfo->m_owners.dump();
				std::cout << "----------------NO-NEIGHBOR-SELECT]]]]]]]]]]]]]]]]]]]]\n\n";
			}
			);
			//如果未能从邻居节点那里选择到，但本节点是seed，则选择服务器
			if (false == flag
				&& localIsSeed
				&& (p = serverPeer)
				&& !pic.b_not_necessary_to_use_server
				&& pktInfo->m_server_request_deadline > 0
				&& (pic.b_urgent
				|| lastChance
				|| !m_b_play_start && (!scheduling_->get_bigest_sqno_i_know() || seqno_less(pic.seqno + 128, *scheduling_->get_bigest_sqno_i_know()))
				|| m_buffer_health < 0.9
				|| i <= std::max<size_t>(1, candidates_buf_.size() * 4 / 5) && pktInfo->m_owners.empty()
				|| pktInfo->m_pull_cnt >= 5 && pktInfo->m_owners.size() <= 3
				)
				)
			{
				double alf = (1.25 - serverParam.remoteToLocalLostRatePow2 / (m_global_remote_to_local_lostrate + FLT_MIN))
					+ std::min(0.3, moreAgressive);
				alf = bound(0.90, alf, 1.25);
				double residualCapacity = alf*serverParam.residualTastCount - serverParam.taskCnt - 1;
				double lostRateThresh = stream_scheduling::LOST_RATE_THRESH;
				if ((serverParam.remoteToLocalLostRate < lostRateThresh
					|| pktInfo->m_owners.empty() && m_buffer_duration < std::max(m_delay_gurantee, 5000)
					|| in_probability(1.0 - serverParam.remoteToLocalLostRate)
					)
					&& (residualCapacity > 0
					|| pic.b_urgent
					|| lastChance
					|| (!g_b_fast_push_to_player&&m_delay_gurantee > 10000)//shunt
					)
					)
				{
					serverParam.taskCnt += 1;
					pic.peer_in_charge = m_server_connection.get();
					temp_total_priority += scheduling_score(*p, serverParam, alf, true, pic);
					flag = true;
				}
				else
				{
				}
			}
			else
			{
				SCHEDULING_DBG(
					if (!flag&&lastChance)
						std::cout << "-----!!!------Not Real Select! ?"
						<< ", is_urgent=" << pic.b_urgent
						<< ", b_not_necessary_to_use_server?" << (pic.b_not_necessary_to_use_server)
						<< ", m_server_request_deadline>0?" << (pktInfo->m_server_request_deadline > 0)
						<< "\n";
				);
			}

			if (!flag)
			{
				pic.peer_in_charge = NULL;
				//没有从邻居那请求，也没向服务器请求，如果pktInfo->m_server_request_deadline=0且没有节点有这个piece, 这个piece将永远不再请求
				if (!pktInfo->m_server_request_deadline&&!m_b_live)
					pktInfo->m_server_request_deadline = 1;
			}
			else
			{
				BOOST_ASSERT(pic.peer_in_charge);
				++selectedCnt;
				if (selectedCnt > 2 * min_request_cnt_ || lastChance&&selected_count >= min_request_cnt_)
					break;
			}
		}

		if (lastChance)
		{
			best_scheduling_.swap(candidates_buf_);
			selected_count = selectedCnt;
		}
		else if (temp_total_priority > best_total_priority&&selectedCnt > 0)
		{
			best_scheduling_ = candidates_buf_;
			best_total_priority = temp_total_priority;
			selected_count = selectedCnt;
		}
		moreAgressive = std::max(moreAgressive, double(min_request_cnt_ - selected_count) / min_request_cnt_);
	}
	std::sort(best_scheduling_.begin(), best_scheduling_.end(), piece_prior());
	if (selectedCnt >= 3 * min_request_cnt_ / 2)
		best_scheduling_.resize(3 * min_request_cnt_ / 2);
}

std::pair<seqno_t, seqno_t> heuristic_scheduling_strategy::bigest_select_seqno()
{
	BOOST_ASSERT(m_smallest_seqno_i_care);
	seqno_t smallestSeqnoIcare = *m_smallest_seqno_i_care;
	double inPktSpeed = scheduling_->get_stream_monitor().get_incoming_packet_rate();
	bool slowStart = !m_b_play_start || !m_recvd_first_packet_time || 0 == m_buffer_size;
	int bufferDuration = m_buffer_duration;
	int delayGuarantee = std::max(2500, m_delay_gurantee);
	int continuousBufferSize = (int)(m_buffer_health*m_buffer_size);
	seqno_t rst;
	if (slowStart || bufferDuration < delayGuarantee)
	{
		int rangeThresh = m_src_packet_rate*(delayGuarantee + 1000) / 1000;
		int window = continuousBufferSize * 3 / 2;
		int inc = std::max(window, rangeThresh);
		if (inPktSpeed>8 * m_src_packet_rate)
			inc -= (inc / 4)*std::min(1.5, inPktSpeed / (8 * m_src_packet_rate + 1.0));
		else if (inPktSpeed < 2 * m_src_packet_rate)
			inc += (inc / 4)*std::min(1.5, (2.0*m_src_packet_rate / (inPktSpeed + 1.0)));
		rst = smallestSeqnoIcare + std::max(inc, rangeThresh);

		if (m_b_live&&!m_b_play_start&&m_buffer_size)
			rst = std::max(rst, m_bigest_seqno_in_buffer, seqno_less);
	}
	else
	{
		int elapse = time_minus(m_now, m_recvd_first_packet_time.get());
		rst = smallestSeqnoIcare + int((1.0 + std::max(0.5, m_buffer_health))*m_buffer_size);
		if (m_neighbor_cnt == 0
			&& bufferDuration>3 * delayGuarantee
			&&inPktSpeed > 1.4*m_src_packet_rate
			)
		{
			//rst+=0;
		}
		else
		{
			rst += (int)(
				bound(1, 3 * delayGuarantee / elapse, 3)
				*((1.4*m_src_packet_rate) / (inPktSpeed + 1.0))
				*m_src_packet_rate*(4 * stream_scheduling::PULL_TIMER_INTERVAL + 150) / 1000
				);
		}

		if (elapse > 3 * delayGuarantee&&inPktSpeed > 2.0*m_src_packet_rate)
		{
			rst -= (int)(
				std::min((inPktSpeed) / (2 * m_src_packet_rate + 1.0), 6.0)
				*stream_scheduling::PULL_TIMER_INTERVAL / 1000
				);
		}
		else if (inPktSpeed < 1.55*m_src_packet_rate)
		{
			rst += (int)(
				std::min(1.55*m_src_packet_rate / (inPktSpeed + 1.0), 6.0)
				*stream_scheduling::PULL_TIMER_INTERVAL / 1000
				);
		}
	}

	if (seqno_less(rst, last_max_select_seqno_))
		rst = last_max_select_seqno_;
	else
		last_max_select_seqno_ = rst;

	seqno_t bigest = *m_bigest_seqno_i_know;
	if (!scheduling_->get_absent_packet_list().empty())
	{
		seqno_t big = scheduling_->get_absent_packet_list().min_seqno() + m_max_memory_cach_size - 1;
		if (seqno_greater(big, rst))
			rst = big;
		if (seqno_greater(big, bigest))
			bigest = big;
	}
	return std::make_pair(rst, bigest);
}

NAMESPACE_END(p2client);
