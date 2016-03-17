#include "client/stream/buffer_manager.h"
#include "client/stream/scheduling_typedef.h"
#include "client/local_param.h"
#include "client/cache/cache_service.h"
#include "asfio/async_dskcache.h"

#if !defined(_DEBUG_SCOPE) && defined(NDEBUG)
#	define  SCHEDULING_DBG(x)
#else 
#	define  SCHEDULING_DBG(x)/* x*/
#endif

NAMESPACE_BEGIN(p2client);

buffer_manager::buffer_manager(io_service& ios, client_param_sptr param, int64_t film_length)
	: basic_client_object(param)
	, memory_packet_cache_(milliseconds(std::max(param->back_fetch_duration, param->delay_guarantee)), is_vod())
	, absent_packet_list_(ios, param, MAX_PKT_BUFFER_SIZE)
	, disk_packet_cache_(get_cache_manager(ios))
{
	if (film_length > 0)
		seqno_range_ = get_seqno_range(get_client_param_sptr()->offset, film_length);
	max_bitmap_range_cnt_ = std::min((int)absent_packet_list_.capacity() - 1, 
		memory_packet_cache_.max_size());
	max_backfetch_cnt_ = std::min(memory_packet_cache_.max_size() - 64, MAX_BACKFETCH_CNT);
	backfetch_cnt_ = 0;//实际值要在reset_seqno函数中设置
}

buffer_manager::~buffer_manager()
{
}

void buffer_manager::reset()
{
	absent_packet_list_.clear();
	memory_packet_cache_.reset();
	bigest_sqno_i_know_.reset();
	current_server_seqno_.reset();
}

void buffer_manager::set_bigest_seqno(seqno_t current_bigest_seq, int backfetch_cnt, timestamp_t now)
{
	backfetch_cnt_ = backfetch_cnt;
	seqno_t smallest = (seqno_t)(current_bigest_seq - backfetch_cnt);
	bigest_sqno_i_know_ = smallest - 1;//smallest是还不知道的片段（否则vod时候会漏掉0片段）
	*bigest_sqno_i_know_ += std::min(backfetch_cnt, 1000);//不需要知道1000个以上那么多的片段

	absent_packet_list_.trim(smallest, backfetch_cnt);
	for (seqno_t seqno = smallest; seqno_less_equal(seqno, *bigest_sqno_i_know_); ++seqno)
	{
		if (!memory_packet_cache_.has(seqno, now))//这一片段数据临时未知，也还没有收到
			absent_packet_list_.just_known(seqno, smallest, now);
	}
}

void buffer_manager::inject_absent_seqno(seqno_t smallest_seqno_i_care, 
	int buffer_size, timestamp_t now)
{
	//当VoD时，需要自己填充just known
	if (!is_vod())
		return;

	if (!bigest_sqno_i_know_)
		return;

	BOOST_ASSERT(seqno_range_);
	/*
	int max_count=absent_packet_list_.capacity();
	int cnt=max_count-(int)absent_packet_list_.size()-1;
	if (cnt>memory_packet_cache_.max_size()-1)
	cnt=memory_packet_cache_.max_size()-1;
	if (cnt>0)
	{
	seqno_t bigest;
	if (!absent_packet_list_.empty())
	bigest=*absent_packet_list_.begin()+cnt;
	else
	bigest=*bigest_sqno_i_know_+cnt;
	if (seqno_greater(bigest, seqno_range_->second))
	bigest=seqno_range_->second;
	int rangeCnt=bound(50, 5*buffer_size+10, 200);

	seqno_t seqno=*bigest_sqno_i_know_+1;
	int dif=seqno_minus(seqno, smallest_seqno_i_care);
	for(;seqno_less_equal(seqno, bigest)&&--rangeCnt>0;++seqno)
	{
	if (!memory_packet_cache_.has(seqno, now))
	absent_packet_list_.just_known(seqno, smallest_seqno_i_care, now);
	bigest_sqno_i_know_=seqno;
	}
	}
	*/

	//每次插入不能太多，以免占用过多CPU。
	int cnt = max_inject_count();
	if (cnt > 0)
	{
		seqno_t bigest = smallest_seqno_i_care + cnt;
		if (seqno_greater(bigest, seqno_range_->second))
			bigest = seqno_range_->second;
		int rangeCnt = bound(300, 5 * buffer_size + 10, 600);
		seqno_t seqno = *bigest_sqno_i_know_ + 1;

		SCHEDULING_DBG(int inject_count = 0;);
		
		for (; seqno_less_equal(seqno, bigest) && --rangeCnt > 0; ++seqno)
		{
			if (!memory_packet_cache_.has(seqno, now)&&
				!absent_packet_list_.just_known(seqno, smallest_seqno_i_care, now)
				)
			{
				break;
			}
			SCHEDULING_DBG(++inject_count;);
		}
		bigest_sqno_i_know_ = seqno - 1;//!!!seqno这个号并没有插入到absent_packet_list_。

		SCHEDULING_DBG(
			if (inject_count > 0)
				std::cout << "---------inject count-----------------" << inject_count << std::endl;
		);
	}
}

void buffer_manager::get_absent_seqno_range(seqno_t& absent_first_seq, 
	seqno_t& absent_last_seq, const peer_sptr& per, int intervalTime, 
	const boost::optional<seqno_t>& smallest_seqno_i_care, double src_packet_rate)
{
	if (!is_vod())
		return;

	BOOST_ASSERT(seqno_range_);

	if (!smallest_seqno_i_care)
	{
		absent_first_seq = absent_last_seq = 0;
		return;
	}

	if (per->smallest_buffermap_exchange_seqno())
	{
		absent_first_seq = per->smallest_buffermap_exchange_seqno().get();
		int offset = seqno_minus(absent_first_seq, *smallest_seqno_i_care);
		if (offset < 0 || offset >((80 * 1000 + 10000) * src_packet_rate / 1000))
		{
			per->smallest_buffermap_exchange_seqno().reset();
		}
	}

	if (!per->smallest_buffermap_exchange_seqno())
	{
		absent_first_seq = absent_packet_list_.empty() ?
			*smallest_seqno_i_care : absent_packet_list_.min_seqno();
	}

	BOOST_ASSERT(seqno_less_equal(absent_first_seq, seqno_range_->second));

	int cnt = max_inject_count();
	seqno_t bigest = *smallest_seqno_i_care + cnt;
	absent_last_seq = bigest + seqno_t((intervalTime + 5000)*src_packet_rate / 1000);

	if (seqno_minus(absent_last_seq, absent_first_seq) < 0)
		absent_first_seq = *smallest_seqno_i_care;

	if (seqno_greater(absent_last_seq, seqno_range_->second))
	{
		absent_last_seq = seqno_range_->second;
	}
}

void buffer_manager::get_buffermap(buffermap_info* mutable_buffermap, 
	int src_packet_rate, bool using_bitset, bool using_longbitset, 
	const std::string* channel_uuid_for_erased_seqno_on_disk_cache
	)
{
	BOOST_ASSERT(mutable_buffermap);

	int lstSize = 0;
	if (using_bitset)
	{
		const static int INTERVAL = (int)BUFFERMAP_EXCHANGE_INTERVAL.total_milliseconds();
		lstSize = using_longbitset ?
			(max_backfetch_cnt_ + 200) : (src_packet_rate * (2 * INTERVAL) / 1000);
		lstSize = std::max(16, lstSize);
	}

	get_memory_packet_cache_buffermap(mutable_buffermap, lstSize);
	if (channel_uuid_for_erased_seqno_on_disk_cache)
	{
		get_disk_packet_cache_buffermap(mutable_buffermap, 
			*channel_uuid_for_erased_seqno_on_disk_cache);
	}
}

void buffer_manager::get_memory_packet_cache_buffermap(buffermap_info* mutableBufferMap, 
	int bitSize)
{
	BOOST_ASSERT(mutableBufferMap);
	//BOOST_ASSERT(bigest_sqno_i_know_);

	BOOST_AUTO(const &recentSeqnoLst, memory_packet_cache_.recent_insert());
	BOOST_FOREACH(const recent_packet_info& recentInfo, recentSeqnoLst)
	{
		mutableBufferMap->add_recent_seqno(recentInfo.m_seqno);
	}

	if (bitSize > 0)
	{
		buffermap_.clear();
		seqno_t minSeq;
		if (memory_packet_cache_.get_buffermap(buffermap_, minSeq, bitSize))
		{
			mutableBufferMap->set_smallest_seqno_i_have(minSeq);
			mutableBufferMap->set_bigest_seqno_i_know(minSeq + bitSize);
			mutableBufferMap->set_first_seqno_in_bitset(minSeq);
			mutableBufferMap->set_bitset(&buffermap_[0], buffermap_.size());
		}
	}
	seqno_t seq;
	if (memory_packet_cache_.smallest_seqno_in_cache(seq))
	{
		mutableBufferMap->set_smallest_seqno_i_have(seq);
	}
	if (bigest_sqno_i_know_)
	{
		mutableBufferMap->set_bigest_seqno_i_know(*bigest_sqno_i_know_);
	}
}

void buffer_manager::get_disk_packet_cache_buffermap(buffermap_info* mutableBufferMap, 
	const std::string& channel_uuid)
{
	if (!disk_packet_cache_)
		return;
	if (channel_uuid.empty())
		return;

	seqno_t minSeq = 0, maxSeq = 0;
	disk_packet_cache_->pop_piece_erased(channel_uuid, minSeq, maxSeq, true);
	if (minSeq != maxSeq)
	{
		mutableBufferMap->set_erased_seq_begin(minSeq);
		mutableBufferMap->set_erased_seq_end(maxSeq);
		for (seqno_t seqno = minSeq; seqno_less_equal(seqno, maxSeq); ++seqno)
		{
			absent_packet_info* pktInfo = absent_packet_list_.get_packet_info(seqno);
			if (pktInfo)
				pktInfo->m_dskcached = false;
		}
		DEBUG_SCOPE(
			std::cout << "XXXXXXXXXXXXX--tell same channel neighbor set buffermap ["
			<< minSeq << ", " << maxSeq << "]\n";
		);
	}
}

void buffer_manager::process_erased_buffermap(peer_connection* conn, seqno_t seq_begin, 
	seqno_t seq_end, seqno_t smallest_seqno_i_care, timestamp_t now)
{
	SCHEDULING_DBG(
		std::cout << "XXXXXXXXXXXXXX--know buffermap from neighbor seq["
		<< seq_begin << ", " << seq_end << "] erased\n";
	);

	if (!bigest_sqno_i_know_)
		return;

	if (seqno_greater_equal(seq_end, smallest_seqno_i_care)
		&& seqno_less_equal(seq_begin, *bigest_sqno_i_know_))
	{
		if (seqno_less_equal(seq_begin, smallest_seqno_i_care))
			seq_begin = smallest_seqno_i_care;

		if (seqno_greater_equal(seq_end, *bigest_sqno_i_know_))
			seq_end = *bigest_sqno_i_know_;

		peer_connection_sptr connSptr(conn->shared_obj_from_this<peer_connection>());
		for (seqno_t seqno = seq_begin; seqno_less_equal(seqno, seq_end); ++seqno)
		{
			absent_packet_info* pktInfo = absent_packet_list_.get_packet_info(seqno);
			if (pktInfo&&pktInfo->is_this(seqno, now))
				pktInfo->m_owners.erase(connSptr);
		}
	}
}

void buffer_manager::process_recvd_buffermap(const buffermap_info& bufmap, 
	const connection_vector& in_substream, peer_connection* conn, 
	seqno_t smallest_seqno_i_care, timestamp_t now, bool add_to_owner)
{
	//此前必须调用过reset_seqno设置bigest_sqno_i_know_
	BOOST_ASSERT(bigest_sqno_i_know_);

	seqnomap_buffer_.clear();

	seqno_t seq_min = smallest_seqno_i_care;
	if (absent_packet_list_.empty())
	{//只关注还没收到的片段即可
		seq_min = *bigest_sqno_i_know_;
	}
	else
	{
		seq_min = absent_packet_list_.min_seqno();
	}
	BOOST_ASSERT(max_bitmap_range_cnt_ < (int)absent_packet_list_.capacity());
	seqno_t seq_max = seq_min + max_bitmap_range_cnt_;

	//处理bitset携带的
	if (bufmap.has_bitset() && bufmap.has_first_seqno_in_bitset())
	{
		const std::string& bitset = bufmap.bitset();
		seqno_t firstSeqno = bufmap.first_seqno_in_bitset();
		seqno_t bigestSeqno = bufmap.bigest_seqno_i_know();
		size_t i = 0;
		if (seqno_less(firstSeqno, seq_min))
		{
			i = seqno_minus(seq_min, firstSeqno) / 8;
			firstSeqno += i * 8;
		}
		bool breakLoop = false;
		for (; !breakLoop&&i < bitset.size(); ++i)
		{
			if (!bitset[i])
			{
				firstSeqno += 8;
				continue;
			}
			for (int j = 0; j < 8; j++)
			{
				if (seqno_greater(firstSeqno, bigestSeqno)
					|| seqno_greater(firstSeqno, seq_max)
					)
				{
					breakLoop = true;
					break;
				}
				if (is_bit(&bitset[i], j) && seqno_greater_equal(firstSeqno, seq_min))
				{
					seqnomap_buffer_.push_back(firstSeqno);
				}
				++firstSeqno;
			}
		}

		if (!seqnomap_buffer_.empty())
		{
			//seqnomap_buffer_已经是排序好的了
			BOOST_ASSERT(seqno_less_equal(seqnomap_buffer_.front(), seqnomap_buffer_.back()));
			seqno_t bigest = seqnomap_buffer_.back();
			if (bufmap.has_bigest_seqno_i_know()
				&& seqno_less(bigest, (seqno_t)bufmap.bigest_seqno_i_know())
				)
			{
				bigest = (seqno_t)(bufmap.bigest_seqno_i_know());
			}
			process_recvd_buffermap(seqnomap_buffer_, in_substream, conn, bigest, 
				smallest_seqno_i_care, now, add_to_owner, false);
		}
	}

	//处理recent携带的
	seqnomap_buffer_.clear();
	if (is_live())//!!vod中携带seqno是不准的, 不能处理
	{
		for (int i = 0; i < bufmap.recent_seqno_size(); i++)
		{
			seqno_t seqno = bufmap.recent_seqno(i);
			if (!current_server_seqno_)
				current_server_seqno_ = seqno;
			else if (seqno_greater(seqno, *current_server_seqno_))
				current_server_seqno_ = *current_server_seqno_ + seqno_minus(seqno, *current_server_seqno_) / 2;
			if (seqno_less_equal(seq_min, seqno, seq_max))
			{
				seqnomap_buffer_.push_back(bufmap.recent_seqno(i));
			}
		}
	}
	//if (conn==stream_seed_->get_connection().get())
	//	seqnomap_buffer_.push_back(bufmap.bigest_seqno_i_know());//对于server，bigest_seqno_i_know就是有片段bigest_seqno_i_know
	if (!seqnomap_buffer_.empty())
	{
		//seqnomap_buffer_没有排序，需要sort
		std::sort(seqnomap_buffer_.begin(), seqnomap_buffer_.end(), seqno_less);
		BOOST_ASSERT(seqno_less_equal(seqnomap_buffer_.front(), seqnomap_buffer_.back()));
		seqno_t bigest = seqnomap_buffer_.back();
		process_recvd_buffermap(seqnomap_buffer_, in_substream, conn, bigest, 
			smallest_seqno_i_care, now, add_to_owner, true);
	}

	//处理其他信息
	if (bufmap.has_smallest_seqno_i_have())
	{
		BOOST_ASSERT(conn&&conn->get_peer());
		conn->get_peer()->smallest_cached_seqno() = bufmap.smallest_seqno_i_have();
	}
}

void buffer_manager::process_recvd_buffermap(const std::vector<seqno_t>&seqnomap, 
	const connection_vector& in_substream, peer_connection* conn, seqno_t bigest, 
	seqno_t smallest_seqno_i_care, timestamp_t now, bool add_to_owner, 
	bool recentRecvdSeqno)
{
	BOOST_ASSERT(bigest_sqno_i_know_);

	if (seqnomap.empty())
		return;

	//std::cout<<"============+++++++++++==================================="
	//<<*seqnomap.begin()<<", "<<seqnomap.size()<<" , "<<get_absent_packet_list().size()
	//<<std::endl;

	BOOST_ASSERT(seqno_greater_equal(bigest, seqnomap.back()));

	//校正bigest
	seqno_t maxSeqnoLocalCare = smallest_seqno_i_care + max_bitmap_range_cnt_;
	if (seqno_greater(bigest, maxSeqnoLocalCare))
		bigest = maxSeqnoLocalCare;
	if (seqno_range_&&seqno_greater(bigest, seqno_range_->second))
		bigest = seqno_range_->second;

	if (recentRecvdSeqno)
	{
		if (!current_server_seqno_)
			current_server_seqno_ = bigest;
		else if (seqno_greater(bigest, *current_server_seqno_))
			current_server_seqno_ = *current_server_seqno_ + seqno_minus(bigest, *current_server_seqno_) / 2;
	}

	peer_connection_sptr connSptr(conn->shared_obj_from_this<peer_connection>());
	for (seqno_t seqno = *bigest_sqno_i_know_; seqno_less_equal(seqno, bigest); ++seqno)
	{
		BOOST_ASSERT(!seqno_range_ || seqno_less_equal(seqno_range_->first, seqno, seqno_range_->second));
		if (!memory_packet_cache_.has(seqno, now))//这一片段数据临时未知，也还没有收到
			just_known(seqno, connSptr, smallest_seqno_i_care, now, false);
	}
	if (seqno_less(*bigest_sqno_i_know_, bigest))
		bigest_sqno_i_know_ = bigest;
	if (seqno_greater(smallest_seqno_i_care, *bigest_sqno_i_know_))
		bigest_sqno_i_know_ = smallest_seqno_i_care;

	conn->get_peer()->smallest_buffermap_exchange_seqno() = bigest;

	if (add_to_owner)//server是不写入owners的
	{
		for (size_t i = 0; i < seqnomap.size(); ++i)
		{
			seqno_t seqno = seqnomap[i];
			BOOST_ASSERT(!seqno_range_ || seqno_less_equal(seqno_range_->first, seqno, seqno_range_->second));
			BOOST_ASSERT(i <= 0 || seqno_greater_equal(seqno, seqnomap[i - 1]));
			if (seqno_range_&&seqno_greater(seqno, seqno_range_->second))
				break;
			if (seqno_less(seqno, smallest_seqno_i_care))
				continue;
			else if (seqno_greater(seqno, *bigest_sqno_i_know_))
				break;
			if (!memory_packet_cache_.has(seqno, now))
				just_known(seqno, connSptr, smallest_seqno_i_care, now, true);
		}
	}
	else
	{
		//SCHEDULING_DBG(std::cout<<"----------------------:add_to_owner? conn!=server_connection_.get()?"
		//	<<add_to_owner<<(conn!=server_connection_.get())<<"\n");
	}
}

void buffer_manager::just_known(seqno_t seqno, const peer_connection_sptr& conn, 
	seqno_t smallest_seqno_i_care, timestamp_t now, bool add_to_owner)
{
	absent_packet_info* pktInfo = absent_packet_list_.get_packet_info(seqno);
	if (!pktInfo)
	{
		absent_packet_list_.just_known(seqno, smallest_seqno_i_care, now);
		pktInfo = absent_packet_list_.get_packet_info(seqno);
	}
	if (add_to_owner&&pktInfo&&pktInfo->is_this(seqno, now))
	{
		pktInfo->m_owners.insert(conn);
	}
}

int buffer_manager::max_inject_count()
{
	int max_count = absent_packet_list_.capacity() * 2 / 3;
	int cnt = max_count - (int)absent_packet_list_.size() - 1;
	if (cnt > memory_packet_cache_.max_size() - 100)
		cnt = memory_packet_cache_.max_size() - 100;
	return cnt;
}

NAMESPACE_END(p2client);
