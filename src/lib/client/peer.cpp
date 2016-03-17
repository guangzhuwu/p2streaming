#include "client/peer.h"

NAMESPACE_BEGIN(p2client);

enum
{
	MIN_CAPACITY = 20, 
	MIN_SERVER_CAPACITY = 100, 
	MAX_CAPACITY = 300, 
};
enum
{
	MAX_DELAY_TIME = 6000, 
	MAX_KEEP_TIME = MAX_DELAY_TIME + 2000
};

peer::neighbor_info::neighbor_info(bool isServer)
	: dupe_speed_meter_long_time_(seconds(6))
	, up_speed_meter_long_time_(seconds(6))
	, down_speed_meter_long_time_(seconds(6))
	, up_speed_meter_(seconds(2))
	, down_speed_meter_(seconds(2))
	, task_speed_meter_(seconds(1))
	, push_to_local_speed_meter_(seconds(2))
	, task_wnd_(isServer ? std::max<int>(20, MIN_CAPACITY) : MIN_CAPACITY)
	, ssthresh_(128)
{
	timestamp_t now = timestamp_now();
	timestamp_t ago = now - 10 * 1000;
	last_unsubscription_time_ = ago;
	last_subscription_time_ = ago;
	last_buffermap_exchange_time_ = ago;
	last_neighbor_exchange_time_ = ago;
	last_media_confirm_time_ = now;
	seqno_download_from_local_.reserve(16);

	substream_info defaultInfo;
	defaultInfo.m_last_unsubscription_time = ago;
	defaultInfo.m_last_subscription_time = ago;
	defaultInfo.m_push_to_remote_delay_var = 50;
	substream_info_.resize(SUBSTREAM_CNT, defaultInfo);
}

peer::peer(bool sameChannel, bool isServer)
	: is_server_(isServer)
	, rtt_(-1)
	, old_rtt_(-1)
	, rtt_var_(-1)
	, is_rtt_accurate_(false)
	, the_same_channel_(sameChannel)
	, is_udp_restricte_(false)
	, is_udp_(false)
	, chunk_map_(NULL)
{
	memset(unreachable_mark_cnt_, 0, sizeof(unreachable_mark_cnt_));
	timestamp_t now = timestamp_now();
	for (size_t i = 0; i < MAX_TOPOLOGY_CNT; ++i)
		last_connect_time_[i] = now - 1000 * 1000;
}

peer::~peer()
{
	if (chunk_map_)
	{
		memory_pool::free(chunk_map_);
		chunk_map_ = NULL;
	}
}

void peer::set_connection(const message_socket_sptr& s)
{
	BOOST_ASSERT(s);
	if (!s) 
		return;
	be_neighbor(s);
	if (rtt_ < 0)
	{
		rtt_ = std::max<int>(s->rtt().total_milliseconds(), 200);
		rtt_var_ = rtt_ / 2;
	}
	is_udp_ = s->connection_category() == message_socket::UDP;
}

void peer::on_upload_to_local(int bytes_recvd, bool ispush)
{
	BOOST_ASSERT(neighbor_info_);

	if (!neighbor_info_)
		return;

	//更新测速计
	if (ispush)
		neighbor_info_->push_to_local_speed_meter_ += bytes_recvd;
	neighbor_info_->up_speed_meter_ += bytes_recvd;
	neighbor_info_->up_speed_meter_long_time_ += bytes_recvd;

	//根据下载速度和对方声明的上载速度计算丢包率，并根据丢包率来
	//计算对方对本节点的贡献能力
	//int Bps=(int)neighbor_info_->up_speed_meter_long_time_.bytes_per_second();
}

void peer::on_download_from_local(int bytes_sent, seqno_t seqno)
{
	BOOST_ASSERT(neighbor_info_);
	if (!neighbor_info_)
		return;
	neighbor_info_->down_speed_meter_ += bytes_sent;
	neighbor_info_->down_speed_meter_long_time_ += bytes_sent;
}

void peer::on_push_to_remote(int delay, int subStreamID)
{
	BOOST_ASSERT(neighbor_info_);

	if (!neighbor_info_)
		return;

	neighbor_info::substream_info& substreamInfo = neighbor_info_->substream_info_[subStreamID];
	boost::optional<int>& srtt = substreamInfo.m_push_to_remote_delay;
	int& rttVar = substreamInfo.m_push_to_remote_delay_var;
	if (!srtt)//not init
	{
		srtt = delay;// srtt = rtt
		rttVar = delay/4;//初始值
	}
	else
	{
		srtt = (3 * srtt.get() + delay) / 4;//这里着重看历史情况
		rttVar = (3 * rttVar + abs(srtt.get() - delay)) / 4;
	}
}

int peer::max_task_delay(double lostRate)
{
	BOOST_ASSERT(neighbor_info_);

	if (!neighbor_info_)
		return 0;

	int r = rto();
	int bytesTransfering = (int)(neighbor_info_->task_keeper_.size() * 1500 * (1.0 - lostRate));
	int t =(int)neighbor_info_->task_keeper_.max_remain_time().total_milliseconds();
	int addedTime = (bytesTransfering * 1000) / (is_server_ ? (512 * 1024) : (128 * 1024));//ms
	if (addedTime > 2000) addedTime = 2000;

	return std::min<int>(r + addedTime, MAX_DELAY_TIME);
}

int peer::keep_task(seqno_t seqno, double lostRate)
{
	BOOST_ASSERT(neighbor_info_);

	if (!neighbor_info_)
		return 0;

	int delay = max_task_delay(lostRate);
	int keeptime = std::min<int>(delay, MAX_KEEP_TIME);
	neighbor_info_->task_keeper_.erase(seqno);
	neighbor_info_->task_keeper_.try_keep(seqno, milliseconds(keeptime));
	return delay;
}

int peer::shrink_cwnd(size_t sizeBeforClear, size_t sizeAfterClear)
{
	BOOST_ASSERT(neighbor_info_);

	if (!neighbor_info_)
		return 0;

	if (sizeAfterClear < sizeBeforClear)
	{
		double& task_wnd = neighbor_info_->task_wnd_;
		double& ssthresh = neighbor_info_->ssthresh_;
		{
			task_wnd -= (sizeBeforClear - sizeAfterClear)*(old_rtt_ < rtt_ ? 0.125 : 0.075);
		}
		{
			//task_wnd_*=window_inc_rate(false);
			//if (task_wnd_<1)
			//	task_wnd_=1;
		}

		if (task_wnd < MIN_CAPACITY)
		{
			ssthresh = task_wnd;
		}
		if (is_server_)
		{
			if (task_wnd < MIN_SERVER_CAPACITY)
				task_wnd = MIN_SERVER_CAPACITY;
		}
		else
		{
			if (task_wnd <= MIN_CAPACITY / 2)
				task_wnd = in_probability(0.5) ? MIN_CAPACITY : MIN_CAPACITY / 2;
		}
	}

	//if (in_probability(0.001))
	//{
	//	std::cout<<"%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%:"
	//		<<"task_wnd_="<<task_wnd_
	//		<<" , rtt_="<<rtt_
	//		<<std::endl;
	//}
	return (int)sizeAfterClear;
}

void peer::task_success(seqno_t seqno, bool dupe)
{
	BOOST_ASSERT(neighbor_info_);

	if (!neighbor_info_)
		return;

	double& task_wnd = neighbor_info_->task_wnd_;
	double& ssthresh = neighbor_info_->ssthresh_;
	is_rtt_accurate_ = true;
	size_t sizeBeforClear = neighbor_info_->task_keeper_.size_befor_clear();
	time_duration rtt = neighbor_info_->task_keeper_.expires_time(seqno);
	if (rtt != boost::posix_time::neg_infin)
	{
		typedef timed_keeper_set<seqno_t>::iterator iterator;
		iterator itr = neighbor_info_->task_keeper_.find(seqno);
		if (itr != neighbor_info_->task_keeper_.end())
			neighbor_info_->task_keeper_.clear_timeout_before(itr);
		int t = (int)rtt.total_milliseconds();
		if (rtt_ < 0)
		{
			rtt_ = t;
			rtt_var_ = std::max(t / 2, 200 / 2);
		}
		else
		{
			rtt_var_ = (3 * rtt_var_ + abs(rtt_ - t)) / 4;
			rtt_ = (7 * rtt_ + t) / 8;
		}
		if (old_rtt_ < 0)
			old_rtt_ = rtt_;
		else
			old_rtt_ = (7 * old_rtt_ + rtt_) / 8;
	}

	if (dupe)
	{
		neighbor_info_->dupe_speed_meter_long_time_ += 1;
		if (rtt_ > 0)
		{
			rtt_var_ = std::max(rtt_var_, std::min(rtt_, rtt_var_ + rtt_ / 4));
		}
	}

	double cntRecv = neighbor_info_->up_speed_meter_long_time_.count_per_second();
	cntRecv = cntRecv*rtt_ / 1000;
	size_t sizeAfterClear = neighbor_info_->task_keeper_.size();
	if ((sizeAfterClear + 1 < sizeBeforClear || (!is_udp_&&rtt > milliseconds(2500)))
		&& rtt_ > old_rtt_
		)
	{
		shrink_cwnd(sizeBeforClear, sizeAfterClear);
	}
	else if (cntRecv > task_wnd&&rtt_ < (old_rtt_ * 5 / 4))
	{
		if (task_wnd < neighbor_info_->ssthresh_)
			task_wnd += 1.0;
		else
			task_wnd += 1.0 / task_wnd;
		if (task_wnd<cntRecv / 4)
			task_wnd = cntRecv / 4;
		if (task_wnd>(is_server_ ? MAX_CAPACITY : MAX_CAPACITY / 2))
			task_wnd = (is_server_ ? MAX_CAPACITY : MAX_CAPACITY / 2);
	}
}

void peer::task_fail(seqno_t seqno)
{
	BOOST_ASSERT(neighbor_info_);

	if (!neighbor_info_)
		return;

	size_t sizeBeforClear = neighbor_info_->task_keeper_.size_befor_clear();
	typedef timed_keeper_set<seqno_t>::iterator iterator;
	iterator itr = neighbor_info_->task_keeper_.find(seqno);
	if (itr != neighbor_info_->task_keeper_.end())
		neighbor_info_->task_keeper_.clear_timeout_before(itr);
	size_t sizeAfterClear = neighbor_info_->task_keeper_.size();
	shrink_cwnd(sizeBeforClear, sizeAfterClear);
}

int peer::tast_count()
{
	BOOST_ASSERT(neighbor_info_);

	if (!neighbor_info_)
		return 0;

	size_t sizeBeforClear = neighbor_info_->task_keeper_.size_befor_clear();
	size_t sizeAfterClear = neighbor_info_->task_keeper_.size();
	int n = shrink_cwnd(sizeBeforClear, sizeAfterClear);
	return n;
}

int peer::residual_tast_count()
{
	BOOST_ASSERT(neighbor_info_);
	if (!neighbor_info_)
		return 0;
	int n =(int) (neighbor_info_->task_wnd_ - tast_count());
	return  bound(n * 7 / 8, n * 2500 / rto(), n + n / 8);
}

void peer::tasks(std::vector<seqno_t>& tsks)
{
	BOOST_ASSERT(neighbor_info_);

	if (!neighbor_info_)
		return;

	tsks.reserve(neighbor_info_->task_keeper_.size());
	for (timed_keeper_set<seqno_t>::iterator itr = neighbor_info_->task_keeper_.begin();
		itr != neighbor_info_->task_keeper_.end();
		++itr
		)
	{
		tsks.push_back(*itr);
	}
}

int peer::rto()const
{
	if (rtt_ < 0 || rtt_var_ < 0)
		return is_server() ? 1500 : 2500;

	int rst = 3 * rtt_ / 2 + std::max(250, 4 * rtt_var_);
	if (rst > 5000)
	{
		if (rtt_ < 2500)
			rst = 5000;
		else
			rst = std::min(6000, rst);
	}
	if (neighbor_info_&&neighbor_info_->dupe_speed_meter_long_time_.bytes_per_second()>1)
		rst += std::max(rtt_, rtt_var_);
	if (is_udp_)
		return bound(rtt_ > 150 ? 1500 : 500, rst, 6000);
	else
		return bound(4000, rst, 6000);
}

NAMESPACE_END(p2client);
