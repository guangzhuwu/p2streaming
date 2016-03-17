#include "client/cache/cache_service.h"
#include "client/stream/absent_packet_list.h"

NAMESPACE_BEGIN(p2client);

absent_packet_list::absent_packet_list(io_service& svc, client_param_sptr param, size_t prefer_size)
	: basic_client_object(param)
	, io_service_(svc)
	, prefer_size_(prefer_size)
{
	size_t n = (1 << 10);
	while (n < prefer_size)
	{
		n <<= 1;
	}
	media_packet_info_vector_.resize(n);
	media_packet_info_pool_.reserve(128);

	if (is_vod() && get_cache_service(io_service_))
	{
		async_dskcache_ = get_cache_manager(io_service_);
	}

	typedef boost::make_unsigned<seqno_t>::type u_seqno_t;
	BOOST_ASSERT((~u_seqno_t(0) + (int64_t)1) % media_packet_info_vector_.size() == 0);
}

bool absent_packet_list::just_known(seqno_t seqno, seqno_t smallest_seqno_i_care, timestamp_t now)
{
	BOOST_ASSERT(seqno_less_equal(smallest_seqno_i_care, seqno));

	//播放过去，不再关心
	if (seqno_greater(smallest_seqno_i_care, seqno))
		return false;

	//超出调度范围
	if (seqno_minus(seqno, smallest_seqno_i_care) > (int)prefer_size_)
		return false;

	absent_packet_info* pktInfo = __get_packet_info(seqno, true);
	BOOST_ASSERT(pktInfo);
	if (pktInfo->is_this(seqno, now))
		return false;

	if (empty()
		|| seqno_less(seqno, get_client_param_sptr()->smallest_seqno_absenct)
		)
	{
		get_client_param_sptr()->smallest_seqno_absenct = seqno;
	}

	not_requesting_packets_.insert(seqno);
	BOOST_ASSERT(requesting_packets_.find(seqno) == requesting_packets_.end());
	pktInfo->just_known(seqno, now);
	BOOST_ASSERT(pktInfo->is_this(seqno, now));

	if (is_vod())
	{
		if (async_dskcache_)
		{
			pktInfo->m_dskcached = async_dskcache_->has_piece(
				get_client_param_sptr()->channel_uuid, seqno);
		}
	}

	return true;
}

void absent_packet_list::request_failed(const iterator& itr, timestamp_t now, int reRequestDelay)
{
	BOOST_ASSERT(requesting_packets_.find(*itr) == itr);
	request_failed(*itr, now, reRequestDelay);
}

void absent_packet_list::request_failed(seqno_t seqno, timestamp_t now, int reRequestDelay)
{
	absent_packet_info* pktInfo = get_packet_info(seqno);
	BOOST_ASSERT(pktInfo);
	if (pktInfo)
	{
		pktInfo->request_failed(now, reRequestDelay);
		not_requesting_packets_.insert(seqno);
		requesting_packets_.erase(seqno);
	}
}

void absent_packet_list::erase(const iterator& itr, bool inRequestingState)
{
	seqno_t seqno = *itr;
	BOOST_ASSERT(itr == find(seqno, inRequestingState));

	if (inRequestingState)
		requesting_packets_.erase(itr);
	else
		not_requesting_packets_.erase(itr);
	BOOST_ASSERT(!find(seqno));

	__delete(seqno);

	if (seqno_less(seqno, get_client_param_sptr()->smallest_seqno_absenct))
	{
		if (!empty())
			get_client_param_sptr()->smallest_seqno_absenct = min_seqno();
	}
}

void absent_packet_list::erase(seqno_t seqno)
{
	BOOST_AUTO(itr, not_requesting_packets_.find(seqno));
	if (itr != not_requesting_packets_.end())
	{
		erase(itr, false);
	}
	else if ((itr = requesting_packets_.find(seqno)) != requesting_packets_.end())
	{
		erase(itr, true);
	}
	else
	{
		BOOST_AUTO(&pktInfoPtr, get_slot(media_packet_info_vector_, seqno));
		if (pktInfoPtr&&pktInfoPtr->is_this(seqno, timestamp_now()))
		{
			__delete(seqno);
		}
	}
	BOOST_ASSERT(!find(seqno));
}

void absent_packet_list::clear()
{
	BOOST_FOREACH(const seqno_t seqno, not_requesting_packets_)
	{
		__delete(seqno);
	}
	BOOST_FOREACH(const seqno_t seqno, requesting_packets_)
	{
		__delete(seqno);
	}
	not_requesting_packets_.clear();
	requesting_packets_.clear();
	media_packet_info_pool_.clear();
}

seqno_t absent_packet_list::min_seqno()const
{
	BOOST_ASSERT(!empty());
	seqno_t seq;
	bool assigned = false;
	if (!not_requesting_packets_.empty())
	{
		seq = *not_requesting_packets_.begin();
		assigned = true;
	}
	if (!requesting_packets_.empty())
	{
		seqno_t seq2 = *requesting_packets_.begin();
		if (!assigned || seqno_less(seq2, seq))
			seq = seq2;
	}
	return seq;
}

void absent_packet_list::trim(seqno_t smallest_seqno_i_care, int max_bitmap_range_cnt)
{
	__trim(smallest_seqno_i_care, max_bitmap_range_cnt, not_requesting_packets_);
	__trim(smallest_seqno_i_care, max_bitmap_range_cnt, requesting_packets_);
	get_client_param_sptr()->smallest_seqno_absenct = empty() ? smallest_seqno_i_care : min_seqno();
}

void absent_packet_list::__trim(seqno_t smallest_seqno_i_care, int max_bitmap_range_cnt, 
	absent_packet_set& lst)
{
	//去尾
	for (; !lst.empty(); )
	{
		seqno_t seqno = *lst.rbegin();
		if (seqno_minus(seqno, smallest_seqno_i_care) > max_bitmap_range_cnt)
		{
			__delete(seqno);
			lst.erase(--lst.end());
		}
		else
		{
			break;
		}
	}
	//去头
	for (; !lst.empty();)
	{
		seqno_t seqno = *lst.begin();
		if (seqno_less(seqno, smallest_seqno_i_care))
		{
			__delete(seqno);
			lst.erase(lst.begin());
		}
		else
		{
			break;
		}
	}
}

absent_packet_info* absent_packet_list::__get_packet_info(seqno_t seqno, bool justKnown)const
{
	BOOST_AUTO(&infoPtr, get_slot(media_packet_info_vector_, seqno));
	//BOOST_ASSERT(!infoPtr||infoPtr->m_seqno==seqno);
	if (!infoPtr&&justKnown)
	{
		if (!media_packet_info_pool_.empty())
		{
			infoPtr = media_packet_info_pool_.back();
			media_packet_info_pool_.pop_back();
		}
		else
		{
			infoPtr.reset(new absent_packet_info);
		}
		BOOST_ASSERT(infoPtr->inited__ == false);
	}
	return infoPtr.get();
}

void absent_packet_list::__delete(seqno_t seqno)
{
	BOOST_AUTO(&pktInfoPtr, get_slot(media_packet_info_vector_, seqno));
	if (!pktInfoPtr)
		return;
	if (media_packet_info_pool_.size() <= 128)
	{
		boost::intrusive_ptr<absent_packet_info> tmp;
		tmp.swap(pktInfoPtr);
		absent_packet_info& info = *tmp;
		info.reset();
		media_packet_info_pool_.push_back(tmp);
	}
	else
	{
		pktInfoPtr.reset();
	}
	BOOST_ASSERT(!pktInfoPtr);
}

NAMESPACE_END(p2client);


