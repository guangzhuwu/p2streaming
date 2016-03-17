#include "client/stream/stream_monitor.h"
#include "client/stream/stream_scheduling.h"

using namespace p2client;

namespace
{
	const double DUPE_INIT = 0.001;
	const double DUPE_MAX = 0.015;
}

void timestamp_guess::set(seqno_t seqno, int packetRate, timestamp_t timestamp)
{
	seqno_t slot = (seqno / TIMESTAMP_GUESS_PKT_CNT) % (seqno_t)timestamp_guess_vector_.size();
	timestamp_element& elem = timestamp_guess_vector_[slot];
	if (!elem.m_inited ||
		abs_middle_distance(slot) < abs_middle_distance(elem.m_seqno)//尽量记录中点的值
		)
	{
		elem.m_seqno = seqno;
		elem.m_timestamp = timestamp;
		elem.m_packet_rate = (uint16_t)packetRate;
		elem.m_inited = true;
	}
}

bool timestamp_guess::get(seqno_t seqno, timestamp_t& timeStamp, int*thePacketRate)const
{
	//左右搜索128个片段,找到最近的已经set过的timestamp_element
	const timestamp_element* elem = NULL;
	int seqnoDis;
	for (int i = 0; i < 128 / TIMESTAMP_GUESS_PKT_CNT; ++i)
	{
		if ((elem = &get_slot(timestamp_guess_vector_, ((seqno + i*TIMESTAMP_GUESS_PKT_CNT) / TIMESTAMP_GUESS_PKT_CNT)))
			&& elem->m_inited
			&&abs(seqnoDis = seqno_minus(seqno, (seqno_t)elem->m_seqno)) <= 4 * TIMESTAMP_GUESS_PKT_CNT
			||
			(elem = &get_slot(timestamp_guess_vector_, ((seqno - i*TIMESTAMP_GUESS_PKT_CNT) / TIMESTAMP_GUESS_PKT_CNT)))
			&& elem->m_inited
			&&abs(seqnoDis = seqno_minus(seqno, (seqno_t)elem->m_seqno)) <= 4 * TIMESTAMP_GUESS_PKT_CNT
			)
		{
			timeStamp = elem->m_timestamp + (seqnoDis * 1000 / elem->m_packet_rate);
			if (thePacketRate)
				*thePacketRate = elem->m_packet_rate;
			return true;
		}
	}
	return false;
}

stream_monitor::stream_monitor(stream_scheduling& scheduling)
	:scheduling_(&scheduling)
	//, in_packet_rate_meter_long_(milliseconds(3500))
	//, out_speedmeter_long_(milliseconds(2500))
	//, in_dupe_packet_rate_meter_long_(milliseconds(2500))
	//, in_pull_pull_dupe_packet_rate_meter_long_(milliseconds(2500))
	//, in_push_packet_rate_meter_long_(milliseconds(2500))
	//, in_speedmeter_(milliseconds(1500))
	//, in_packet_rate_meter_(milliseconds(1500))
	//, in_timeout_packet_rate_meter_(milliseconds(1500))
	//, push_to_player_packet_speed_meter_(milliseconds(1500))
	//, in_packet_total_len_(milliseconds(2500))
	//, int_packet_total_count_(milliseconds(2500))

	, in_packet_rate_meter_long_(seconds(5))
	, out_speedmeter_long_(seconds(5))
	, in_dupe_packet_rate_meter_long_(seconds(5))
	, in_pull_pull_dupe_packet_rate_meter_long_(seconds(5))
	, in_push_packet_rate_meter_long_(seconds(5))
	, in_speedmeter_(seconds(2))
	, in_packet_rate_meter_(seconds(2))
	, in_timeout_packet_rate_meter_(seconds(2))
	, push_to_player_packet_speed_meter_(seconds(2))
	, in_packet_total_len_(seconds(3))
	, int_packet_total_count_(seconds(3))

	, default_packet_size_(1350)
	, dupe_thresh_(SUBSTREAM_CNT, DUPE_INIT)
	, playing_quality_(1.0)
	, DELAY_INIT((scheduling.delay_guarantee() + scheduling.backfetch_msec()) > 3000 ? 500 : 50)
	, DELAY_MAX((scheduling.delay_guarantee() + scheduling.backfetch_msec()) > 3000 ? 2500 : 500)
{
	delay_thresh_.resize(SUBSTREAM_CNT, DELAY_INIT);
	pushed_in_substream_delay_var_.resize(SUBSTREAM_CNT, 100);
	pushed_in_substream_delay_.resize(SUBSTREAM_CNT);

	//因为substream的码率是整个stream码率的1/SUBSTREAM_CNT，故使用较长的估计时间
	rough_speed_meter subStreamMeter(seconds(3 * SUBSTREAM_CNT));
	in_substream_push_pull_dupe_packet_rate_meter_.resize(SUBSTREAM_CNT, subStreamMeter);
	in_substream_packet_rate_meter_.resize(SUBSTREAM_CNT, subStreamMeter);
	in_substream_pushed_packet_rate_meter_.resize(SUBSTREAM_CNT, subStreamMeter);
	in_substream_push_pull_dupe_packet_rate_.resize(SUBSTREAM_CNT, 0);
}

void stream_monitor::calc_rtt(boost::optional<msec>& srtt, int& rttVar, int delay)
{
	//仿照tcp的rtt估计算法来估计平均延迟
	//，但是，和tcp rtt不同，这里的rtt可能是负数，因此，rttvar计算是有差别的
	if (!srtt)//not init
	{
		srtt = delay;// srtt = rtt
		rttVar = delay / 4;//初始值
	}
	else
	{
		srtt = (srtt.get() + 3 * delay) / 4;
		rttVar = (3 * rttVar + abs(srtt.get() - delay)) / 4;
	}
}

void stream_monitor::reset_incoming_substeam(int sub_stream_id)
{
	in_substream_push_pull_dupe_packet_rate_[sub_stream_id] = 0;
	in_substream_push_pull_dupe_packet_rate_meter_[sub_stream_id].reset();
	in_substream_packet_rate_meter_[sub_stream_id].reset();
	in_substream_pushed_packet_rate_meter_[sub_stream_id].reset();
	pushed_in_substream_delay_[sub_stream_id].reset();
	pushed_in_substream_delay_var_[sub_stream_id] = 0;

	timestamp_offset_.reset();
}

void stream_monitor::push_to_player(seqno_t seq, bool good)
{
	const int MaxSize = 256;

	push_to_player_packet_speed_meter_ += 1;

	if (!bigest_push_to_playe_seq_ || seqno_minus(*bigest_push_to_playe_seq_, seq) < 0)
		bigest_push_to_playe_seq_ = seq;
	if (!smallest_push_to_playe_seq_)
		smallest_push_to_playe_seq_ = seq;
	if (seqno_minus(*bigest_push_to_playe_seq_, *smallest_push_to_playe_seq_) > MaxSize)
		*smallest_push_to_playe_seq_ = (*bigest_push_to_playe_seq_ - MaxSize);

	if (good)
		push_to_player_seq_.push(seq);

	while (!push_to_player_seq_.empty()
		&& seqno_minus(push_to_player_seq_.front(), *smallest_push_to_playe_seq_) < 0
		)
	{
		push_to_player_seq_.pop();
	}

	BOOST_ASSERT((int)push_to_player_seq_.size() <= MaxSize + 1);
	if (push_to_player_seq_.empty())
	{
		playing_quality_ = 0.0;
	}
	else
	{
		double n = seqno_minus(*bigest_push_to_playe_seq_, *smallest_push_to_playe_seq_) + 1;
		playing_quality_ = push_to_player_seq_.size() / n;
	}
}

void stream_monitor::reset_to_player()
{
	bigest_push_to_playe_seq_.reset();
	smallest_push_to_playe_seq_.reset();
	while (!push_to_player_seq_.empty())
	{
		push_to_player_seq_.pop();
	}
}

double stream_monitor::get_playing_quality()
{
	if (push_to_player_seq_.size() == 0)
		return 0.0;
	else if (push_to_player_seq_.size() <= 256)
	{
		double n = seqno_minus(push_to_player_seq_.back(), push_to_player_seq_.front()) + 1;
		return push_to_player_seq_.size() / n;
	}
	return playing_quality_;
}

stream_monitor::msec stream_monitor::get_variance_push_delay(int sub_stream_id)
{
	if (pushed_in_substream_delay_[sub_stream_id])
		return std::max(100, pushed_in_substream_delay_var_[sub_stream_id]);
	return 500;
}
stream_monitor::msec stream_monitor::get_average_push_delay(int sub_stream_id)
{
	if (pushed_in_substream_delay_[sub_stream_id])
		return pushed_in_substream_delay_[sub_stream_id].get();
	return scheduling_->urgent_time() - 1000;
}
stream_monitor::msec stream_monitor::get_average_push_delay()
{
	int sum = 0;
	int size = 0;
	for (int i = 0; i < SUBSTREAM_CNT; ++i)
	{
		if (pushed_in_substream_delay_[i])
		{
			BOOST_ASSERT(pushed_in_substream_delay_[i].get() < 50 * 1000);
			sum += pushed_in_substream_delay_[i].get();
			size++;
		}
	}
	if (size == 0)
		return 1000;
	BOOST_ASSERT(abs(sum / size) < 50 * 1000);
	return sum / size;
}

size_t stream_monitor::get_average_packet_size()
{
	double total_len = in_packet_total_len_.bytes_per_second();
	double total_count = int_packet_total_count_.bytes_per_second();
	if (total_count >= 1)
	{
		default_packet_size_ = (int)(total_len / total_count);
	}
	BOOST_ASSERT(default_packet_size_ > 0);
	return (size_t)default_packet_size_;
}

int stream_monitor::get_bigest_push_delay_substream()
{
	int which = -1;
	int n = 0;
	int bigest = (std::numeric_limits<int>::min)();
	for (int i = 0; i < SUBSTREAM_CNT; ++i)
	{
		if (pushed_in_substream_delay_[i])
		{
			n++;
			if (bigest < pushed_in_substream_delay_[i].get())
			{
				bigest = pushed_in_substream_delay_[i].get();
				which = (int)i;
			}
		}
	}
	if (n <= std::max(4, SUBSTREAM_CNT / 8))
		return -1;
	return which;
}

double stream_monitor::get_push_pull_duplicate_rate(int sub_stream_id)
{
	double total = in_substream_packet_rate_meter_[sub_stream_id].bytes_per_second();
	double n = in_substream_push_pull_dupe_packet_rate_meter_[sub_stream_id].bytes_per_second();
	double rst = std::min(1.0, n / (total + FLT_MIN));

	double& averageRst = in_substream_push_pull_dupe_packet_rate_[sub_stream_id];
	if (averageRst <= 0.0)
		averageRst = rst;
	else
		averageRst = (averageRst + rst)*0.5;
	return averageRst;
}

double stream_monitor::get_duplicate_rate()
{
	double total = in_packet_rate_meter_long_.bytes_per_second();
	double n = in_dupe_packet_rate_meter_long_.bytes_per_second();
	return std::min(1.0, n / (total + FLT_MIN));
}

double stream_monitor::get_pull_pull_duplicate_rate()
{
	double total = in_packet_rate_meter_long_.bytes_per_second();
	double n = in_pull_pull_dupe_packet_rate_meter_long_.bytes_per_second();
	return std::min(1.0, n / (total + FLT_MIN));
}

double stream_monitor::get_push_rate(int sub_stream_id)
{
	double total_in = in_substream_packet_rate_meter_[sub_stream_id].bytes_per_second();
	double pushed_in = in_substream_pushed_packet_rate_meter_[sub_stream_id].bytes_per_second();
	if (total_in < 3.0)
		return 1.0;
	if (pushed_in <= 0.0)
		return 0.0;
	return std::min(1.0, pushed_in / total_in);
}

double stream_monitor::get_push_rate()
{
	double total = in_packet_rate_meter_long_.bytes_per_second();
	double n = in_push_packet_rate_meter_long_.bytes_per_second();
	if (n <= 0.0)
		return 0.0;
	if (total <= 0.0)
		return 1.0;
	return std::min(1.0, n / total);
}

double stream_monitor::get_timeout_rate()
{
	double total = in_packet_rate_meter_.bytes_per_second();
	double n = in_timeout_packet_rate_meter_.bytes_per_second();
	if (total == 0)
		return 0;
	return std::min(1.0, n / total);
}

void stream_monitor::incoming_media_packet(size_t len, seqno_t seqno, int packetRate,
	timestamp_t timestamp, timestamp_t now, dupe_state dupe, bool timeout, bool isPush)
{
	//用push片段中最新的timestamp来校准timestamp_offset_
	//尽量以最新的片段时戳为准。以越新片段的片段为准，表现出的delay越大。
	int offset = timestamp - now;
	if (!timestamp_offset_)
	{
		timestamp_offset_ = offset;//不使用time_minus，避免跳变
	}
	else
	{
		int diff = timestamp_offset_.get() - offset;
		if (diff < 0)
			*timestamp_offset_ += std::min(20, -diff);//调整量要小
	}
	BOOST_ASSERT(timestamp_offset_);

	DEBUG_SCOPE(
		int delay = INT_MAX;
	piece_elapse(now, seqno, delay, &timestamp);
	BOOST_ASSERT(!scheduling_->is_live() || abs(delay) < 100 * 1000);//不太可能超过100s
	);

	int subStreamID = seqno%SUBSTREAM_CNT;
	in_packet_total_len_ += len;
	int_packet_total_count_ += 1;
	if (isPush)
	{
		int delay = INT_MAX;
		piece_elapse(now, seqno, delay, &timestamp);
		calc_rtt(pushed_in_substream_delay_[subStreamID],
			pushed_in_substream_delay_var_[subStreamID], delay
			);
		in_push_packet_rate_meter_long_ += 1;
		in_substream_pushed_packet_rate_meter_[subStreamID] += 1;
	}
	if (timeout)
		in_timeout_packet_rate_meter_ += 1;
	in_packet_rate_meter_long_ += 1;
	in_speedmeter_ += len;
	in_packet_rate_meter_ += 1;
	in_substream_packet_rate_meter_[subStreamID] += 1;

	if (dupe == PUSH_PULL_DUPE)
	{
		in_substream_push_pull_dupe_packet_rate_meter_[subStreamID] += 1;
		in_dupe_packet_rate_meter_long_ += 1;
	}
	else if (dupe == PULL_PULL_DUPE)
	{
		in_dupe_packet_rate_meter_long_ += 1;
		in_pull_pull_dupe_packet_rate_meter_long_ += 1;
	}
	recode_timestamp(seqno, packetRate, timestamp);
	parameter_adaptive(subStreamID);
}

bool stream_monitor::piece_elapse(timestamp_t now, seqno_t seqno, int& t, const timestamp_t* pStamp)const
{
	if (!timestamp_offset_)
		return false;

	timestamp_t stamp;
	bool gusessRst = true;
	if (pStamp)
		stamp = *pStamp;
	else
		gusessRst = guess_timestamp(seqno, stamp);
	if (gusessRst)
	{
		t = (int)(now - stamp + timestamp_offset_.get());
		BOOST_ASSERT(!scheduling_->is_live() || abs(t) < 100 * 1000);
	}
	return gusessRst;
}

double stream_monitor::urgent_degree(timestamp_t now, seqno_t seqno, int deadlineMsec)const
{
	//恰处于播放点处的seqno紧急度为1.0，恰处于deadlineMsec时间后播放的seqno紧急度是0.0
	//，晚于deadlineMsec时间后播放的紧急度小于0
	BOOST_ASSERT(deadlineMsec > 0);

	BOOST_AUTO(const& minSeqnoLocalCare, scheduling_->get_smallest_seqno_i_care());

	if (!minSeqnoLocalCare || !scheduling_->get_recvd_first_packet_time())
		return 0.0;
	int srcPacketRate = scheduling_->get_src_packet_rate();
	//开始播放前，设置[0, urgent_region]的seq为紧急区域
	if (!scheduling_->is_player_started())
	{
		double urgent_region = std::max(5 * scheduling_->delay_guarantee() / 4, 2500)*srcPacketRate / 1000.0;
		if (seqno_minus(seqno, *minSeqnoLocalCare) < urgent_region)
			return double(urgent_region - seqno_minus(seqno, *minSeqnoLocalCare)) / urgent_region;
		return 0;
	}

	timestamp_t t;
	int packetRate = srcPacketRate;
	if (scheduling_->is_vod()//vod类型只用seqno差来推测urgent，因vod中sento_player_timestamp_offset_已经变得意义不大
		|| !guess_timestamp(seqno, t, &packetRate)
		)
	{
		packetRate = ((int)srcPacketRate + packetRate) / 2;
		double seqnoCnt = deadlineMsec*packetRate / 1000;
		double rst = (seqnoCnt - seqno_minus(seqno, minSeqnoLocalCare.get())) / (seqnoCnt + FLT_MIN);
		return std::max(rst, 0.0);
	}

	int timeT = time_minus(t, scheduling_->get_current_playing_timestamp(now));
	double rst = (double)(deadlineMsec - timeT) / (double)(deadlineMsec + FLT_MIN);
	return std::max(rst, 0.0);
}

void stream_monitor::parameter_adaptive(int subStreamID)
{
	const double inc = 1.2;
	const double dec = 0.99815;//(1.0/1.2)^99==0.99815

	double pushPullDupRate = get_push_pull_duplicate_rate(subStreamID);
	double timeoutRate = get_timeout_rate();
	if (pushPullDupRate > 0.008)
	{
		dupe_thresh_[subStreamID] *= dec;
		delay_thresh_[subStreamID] *= inc;
		if (delay_thresh_[subStreamID] > DELAY_MAX)
			delay_thresh_[subStreamID] = DELAY_MAX;
	}
	else if (pushPullDupRate<0.001)
	{
		dupe_thresh_[subStreamID] *= inc;
		delay_thresh_[subStreamID] *= dec;
		if (dupe_thresh_[subStreamID]>DUPE_MAX)
			dupe_thresh_[subStreamID] = DUPE_MAX;
	}

	if (timeoutRate > 0.012)
	{
		dupe_thresh_[subStreamID] *= inc;
		delay_thresh_[subStreamID] *= dec;
		if (dupe_thresh_[subStreamID] > DUPE_MAX)
			dupe_thresh_[subStreamID] = DUPE_MAX;
	}
	else if (timeoutRate<0.005)
	{
		dupe_thresh_[subStreamID] *= dec;
		delay_thresh_[subStreamID] *= inc;
		if (delay_thresh_[subStreamID]>DELAY_MAX)
			delay_thresh_[subStreamID] = DELAY_MAX;
	}

	//if(timeoutRate>0.015)
	//{
	//	push_to_playe_rate_*=1.00005;
	//	if (push_to_playe_rate_>1.2)
	//		push_to_playe_rate_=1.2;
	//}
	//else if(timeoutRate<0.005)
	//{
	//	push_to_playe_rate_*=0.999995;
	//	if (push_to_playe_rate_<0.999995)
	//		push_to_playe_rate_=0.999995;
	//}
}