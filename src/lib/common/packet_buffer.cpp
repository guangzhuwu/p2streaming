#include "common/packet_buffer.h"

#include <xutility>

#include "common/const_define.h"
#include "common/typedef.h"
#include "common/utility.h"

NAMESPACE_BEGIN(p2common);

void packet_buffer::ring_buffermap::get(seqno_t pos_start, seqno_t pos_end, void * dst, int dstLen)const
{
	BOOST_ASSERT(pos_start % 8 == 0);
	BOOST_ASSERT(seqno_greater(pos_end, pos_start));

	pos_end -= 1;//[pos_start,pos_end)
	int dis = seqno_minus(pos_end, pos_start);
	if (dis < 0)
		return;
	if (dis>=bitsize_)
		pos_end -= (dis - bitsize_)+1;//保证不会套圈
	BOOST_ASSERT(dis <= (int)bitsize_);
	size_t startBytePos = (pos_start >> 3) % bitset_.size();
	size_t endBytePos = (startBytePos + (dis >> 3)) % bitset_.size();
	BOOST_ASSERT(startBytePos < bitset_.size() && endBytePos < bitset_.size());
	const char* pstart = &bitset_[startBytePos];
	const char* pend = &bitset_[endBytePos];
	char* p = NULL;
	if (pstart < pend
		|| pstart == pend&&seqno_minus(pos_end, pos_start) < 8
		)
	{
		BOOST_ASSERT((pend + 1 - pstart) <= dstLen);
		p = std::copy(pstart, pend + 1, (char*)dst);
	}
	else
	{
		BOOST_ASSERT((&bitset_.back() + 1 - pstart) <= dstLen);
		p = std::copy(pstart, &bitset_.back() + 1, (char*)dst);
		BOOST_ASSERT(p + (pend + 1 - &bitset_[0]) <= (char*)dst + dstLen);
		p = std::copy(&bitset_[0], pend + 1, p);//上面已经保证不会套圈了，所以，pos_end所在的byte一定要被拷贝
	}

	//将超出pos_end的部分bit清零
	if (pos_end & 0x7)
	{
		BOOST_ASSERT(p > dst);
		BOOST_ASSERT(p <= (char*)dst + dstLen);
		int n = pos_end & 0x7;
		for (int i = 7; i > n; --i)
			set_bit(p - 1, i, false);
	}
}

packet_buffer::packet_buffer(const time_duration& bufTime, bool isVodCategory)
	: buf_time_msec_((int)bufTime.total_milliseconds())
	, is_vod_category_(isVodCategory)
{
	if (is_vod_category_)
	{
		media_packets_.resize(1024 * 2, packet_elm());
	}
	else
	{
		int minCnt = std::min(MAX_PKT_BUFFER_SIZE,
			(buf_time_msec_ / 1000)*((MAX_SUPPORT_BPS / 8) / PIECE_SIZE)
			);//按照MAX_SUPPORT_BPS码率算
		int n = 1;
		while (n < minCnt)
		{
			n <<= 1;
		}
		media_packets_.resize(n, packet_elm());
	}
	buffermap_cache_.resize(media_packets_.size());
	buf_time_msec_ += 5 * 1000;
}

bool packet_buffer::insert(const media_packet& pkt, int packetRate, timestamp_t now,
	const boost::optional<seqno_t>& currentPlayingSeqno)
{
	seqno_t seqno = pkt.get_seqno();
	timestamp_t timestamp = pkt.get_time_stamp();

	if (has(seqno, now) != NOT_HAS)
		return false;

	//插入队列
	get_slot(media_packets_, seqno).assign(pkt, now);

	//调整bigest_time_stamp_
	if (!bigest_time_stamp_ || time_less(*bigest_time_stamp_, timestamp))
	{
		bigest_time_stamp_ = timestamp;
	}

	//更新recent_seq_lst_long_
	for (; !recent_seq_lst_long_.empty();)
	{
		BOOST_AUTO(ritr, recent_seq_lst_long_.rbegin());
		if (recent_seq_lst_long_.size() > media_packets_.size()
			||
			(!is_vod_category_
			&& is_time_passed(buf_time_msec_, ritr->second, *bigest_time_stamp_)
			&& (!currentPlayingSeqno || seqno_greater(*currentPlayingSeqno, ritr->first))
			)
			)
		{
			packet_elm& tempEle = get_slot(media_packets_, ritr->first);
			if (tempEle.m_seqno == ritr->first)
				tempEle.reset(now);
			buffermap_cache_.set(ritr->first, false);
			recent_seq_lst_long_.erase(--recent_seq_lst_long_.end());
		}
		else
		{
			break;
		}
	}
	recent_seq_lst_long_.insert(std::make_pair(seqno, timestamp));

	//更新bitmap_cache_
	buffermap_cache_.set(seqno, true);

	//更新recent_insert_
	static const int64_t s_out_time = 2 * BUFFERMAP_EXCHANGE_INTERVAL.total_milliseconds();
	recent_packet_info info;
	info.m_seqno = seqno;
	info.m_out_time = now + (timestamp_t)(s_out_time);
	recent_insert_.push_front(info);
	while (recent_insert_.size() > (size_t)8
		|| (!recent_insert_.empty() && time_less(recent_insert_.back().m_out_time, now))
		)
	{
		recent_insert_.pop_back();
	}

	return true;
}

bool packet_buffer::smallest_seqno_in_cache(seqno_t& seq)const
{
	if (!recent_seq_lst_long_.empty())
	{
		seq = recent_seq_lst_long_.rbegin()->first;
		return true;
	}
	return false;
}

bool packet_buffer::bigest_seqno_in_cache(seqno_t& seq)const
{
	if (!recent_seq_lst_long_.empty())
	{
		seq = recent_seq_lst_long_.begin()->first;
		return true;
	}
	return false;
}

bool packet_buffer::get_buffermap(std::vector<char>& outBitset, seqno_t&firstSeqno, int count)const
{
	if (recent_seq_lst_long_.empty() || count <= 0)
		return false;

	seqno_t bigestSeqno = recent_seq_lst_long_.begin()->first;
	seqno_t smallestSeqno = recent_seq_lst_long_.rbegin()->first;
	count = std::min(count, seqno_minus(bigestSeqno, smallestSeqno) + 1);
	count = std::min(count, (int)media_packets_.size());

	seqno_t seqnoBegin = bigestSeqno - seqno_t(count);//这是左侧第一个bit位上的seqno
	firstSeqno = ROUND_UP(seqnoBegin, 8);//firstSeqno必须可以被8整除，这样才好按照byte去memory buffermap_cache_中的数据
	BOOST_ASSERT(seqno_minus(firstSeqno, seqnoBegin) >= 0);
	count -= seqno_minus(firstSeqno, seqnoBegin);
	if (count <= 0 || firstSeqno > bigestSeqno)
		return false;

	int bytes_size = (count + 7) / 8;
	if (bytes_size > 0)
	{
		BOOST_ASSERT(bytes_size < 4096);
		outBitset.resize(bytes_size);
		memset(&outBitset[0], 0, bytes_size);
		buffermap_cache_.get(firstSeqno, firstSeqno + count, &outBitset[0], bytes_size);

		DEBUG_SCOPE(
			timestamp_t now = timestamp_now();
		for (int i = 0; i < count; ++i)
		{
			if (is_bit(&outBitset[0], i))
			{
				if (!has(firstSeqno + i, now))
				{
					std::cout << "bitset=1 but not has!" << std::endl;
					BOOST_ASSERT(0);
				}
			}
			else
			{
				if (has(firstSeqno + i, now))
				{
					std::cout << "bitset=0 but has!" << std::endl;
					BOOST_ASSERT(0);
				}
			}
		}
		);

		return true;
	}
	outBitset.resize(0);
	return false;
}

bool packet_buffer::get(media_packet&pkt, seqno_t seqno, timestamp_t now)const
{
	if (has(seqno, now))
	{
		BOOST_AUTO(elm, get_slot(media_packets_, seqno));
		pkt = media_packet(elm.m_packet_buf);
		return true;
	}
	return false;
}

int packet_buffer::has(seqno_t seqno, timestamp_t now)const
{
	packet_elm& elm = const_cast<packet_elm&>(get_slot(media_packets_, seqno));

	if (elm.__inited&&elm.m_seqno == seqno)
	{
		if (is_vod_category_
			||
			!is_time_passed(buf_time_msec_ * 8, elm.m_time, now)//和一个较长时间比较即可
			)
		{
			BOOST_ASSERT(!elm.m_packet_buf.empty());
			return elm.m_pushed ? PUSHED_HAS : PULLED_HAS;
		}
	}
	return NOT_HAS;
}

void packet_buffer::reset()
{
	recent_insert_.clear();
	recent_seq_lst_long_.clear();
	bigest_time_stamp_.reset();
	buffermap_cache_.reset();

	timestamp_t now = timestamp_now();
	for (size_t i = 0; i < media_packets_.size(); ++i)
		media_packets_[i].reset(now);
}

NAMESPACE_END(p2common);
