#include "client/stream/absent_packet_list.h"

NAMESPACE_BEGIN(p2client);

DEBUG_SCOPE(
	BOOST_TYPEOF(absent_packet_info::s_pull_accumulator) absent_packet_info::s_pull_accumulator;
);


void owner_map::insert(const peer_connection_sptr& conn)
{
	if (!owner_index_)
		init();
	if (conn->local_id() >= STREAM_NEIGHTBOR_PEER_CNT)
	{
		BOOST_ASSERT(0);
		return;//invalid
	}
	int at = owner_index_[conn->local_id()];
	if (at >= 0)
	{
		BOOST_ASSERT((int)owner_vec_.size() > at);
		owner& oldOwner = owner_vec_[at];
		BOOST_ASSERT(oldOwner.id == conn->local_id());
		if (oldOwner.uuid == conn->local_uuid())
			return;//已经记录过
		else
			oldOwner.assign(conn);
	}
	else
	{
		owner_index_[conn->local_id()] = owner_vec_.size();
		owner_vec_.push_back(owner(conn));
	}
}

void owner_map::init()
{
	BOOST_ASSERT(!owner_index_);
	owner_index_ = (int8_t*)memory_pool::malloc(STREAM_NEIGHTBOR_PEER_CNT*sizeof(*owner_index_));
	memset(owner_index_, -1, STREAM_NEIGHTBOR_PEER_CNT*sizeof(*owner_index_));
	owner_vec_.reserve(4);
}

void owner_map::reset()
{
	if (owner_index_)
	{
		memory_pool::free(owner_index_);
		owner_index_ = NULL;
		std::vector<owner, p2engine::allocator<owner> >().swap(owner_vec_);
	}
}

peer_connection_sptr owner_map::select(size_t i)
{
	if (!owner_index_)
		return peer_connection_sptr();
	int at = owner_index_[i];
	if (at < 0)
		return peer_connection_sptr();
	BOOST_ASSERT(at < (int)owner_vec_.size());
	if (at < (int)owner_vec_.size())
	{
		peer_connection_sptr rst = owner_vec_[at].conn.lock();
		if (rst&&rst->is_connected())
		{
			if (owner_vec_[at].request_deadline >= 0)
				return rst;
		}
		else
		{
			erase_owner(owner_vec_[at].id);
		}
	}
	return peer_connection_sptr();
}

peer_connection_sptr owner_map::random_select()
{
	if (owner_vec_.empty())
		return peer_connection_sptr();

	int max_link_local_try = 2;
	for (int n = (int)size() / 2; n >= 0; --n)
	{
		int i = random<int>(0, owner_vec_.size()) % (int)owner_vec_.size();
		if (n > 1 && !owner_vec_[i].is_link_local&&--max_link_local_try > 0)
		{
			n++;
			continue;//尽量选择一个link_local的
		}
		peer_connection_sptr rst = select(i);
		if (rst || owner_vec_.empty())
		{
			return rst;
		}
	}

	bool reverse = in_probability(0.5);
	for (int n = 0; n < (int)owner_vec_.size(); ++n)
	{
		int i = reverse ? ((int)owner_vec_.size() - n - 1) : n;
		if (i < 0) break;
		peer_connection_sptr rst = select(i);
		if (rst || owner_vec_.empty())
		{
			return rst;
		}
	}

	int i = random<int>(0, owner_vec_.size()) % (int)owner_vec_.size();
	return select(i);
}

void owner_map::erase(const peer_connection_sptr& conn)
{
	int id = conn->local_id();
	int uuid = conn->local_uuid();
	erase_owner(id, &uuid);
}

peer_connection* owner_map::find(const peer_connection_sptr& conn)const
{
	if (!owner_index_)
		return NULL;

	int id = conn->local_id();
	int uuid = conn->local_uuid();
	int at = owner_index_[id];
	if (at < 0)
		return NULL;
	BOOST_ASSERT(at < (int)owner_vec_.size());
	owner_index_[id] = -1;
	if (!owner_vec_.empty())
		return NULL;
	if (uuid == owner_vec_[at].uuid)
		return conn.get();
	return NULL;
}

bool owner_map::dec_request_deadline(const peer_connection_sptr& conn)
{
	if (!owner_index_)
		return false;
	int id = conn->local_id();
	int at = owner_index_[id];
	if (at >= 0 && at < (int)owner_vec_.size())
	{
		BOOST_ASSERT(conn->local_uuid() == owner_vec_[at].uuid);
		--(owner_vec_[at].request_deadline);
		return true;
	}
	return false;
}

void owner_map::erase_owner(int id, const int* uuid)
{
	if (!owner_index_)
		return;
	int at = owner_index_[id];
	if (at < 0)
		return;
	BOOST_ASSERT(at < (int)owner_vec_.size());
	owner_index_[id] = -1;
	if (owner_vec_.empty())
		return;
	if (!uuid || *uuid == owner_vec_[at].uuid)
	{
		if (at == (int)owner_vec_.size() - 1)
		{
			owner_vec_.pop_back();
		}
		else
		{
			std::swap(owner_vec_[at], owner_vec_.back());
			owner_vec_.pop_back();
			owner_index_[owner_vec_[at].id] = at;
		}
	}
	else
	{
		BOOST_ASSERT(0);
	}
}

void owner_map::dump()
{
	error_code ec;
	for (size_t i = 0; i < size(); ++i)
	{
		BOOST_ASSERT(owner_vec_[i].id != INVALID_ID&&owner_index_[owner_vec_[i].id] == i&& i >= 0 && i < STREAM_NEIGHTBOR_PEER_CNT);
		peer_connection_sptr rst = owner_vec_[i].conn.lock();
		if (rst&&rst->is_connected())
		{
			peer_sptr p = rst->get_peer();
			std::cout << rst->remote_endpoint(ec)
				<< ", residual:" << p->residual_tast_count()
				<< ", deadline:" << (int)owner_vec_[i].request_deadline
				<< ", rto:" << p->rto()
				<< ", alive:" << rst->alive_probability()
				<< ", lostrate:" << rst->remote_to_local_lost_rate()
				<< "\n";
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void absent_packet_info::reset(){
	inited__ = false;
	m_seqno = 0;
	m_owners.reset();
	m_peer_incharge.reset();
	m_must_pull = false;
	m_dskcached = false;
	m_server_request_deadline = server_request_deadline;
	m_pull_cnt = 0;

	DEBUG_SCOPE(
	m_pull_edps.clear();
	);
}

void  absent_packet_info::just_known(seqno_t seqno, timestamp_t now){
	inited__ = true;
	m_must_pull = false;
	m_dskcached = false;

	m_seqno = seqno;
	m_owners.reset();
	m_peer_incharge.reset();
	m_pull_outtime = now - 1;
	m_pull_time = now + wrappable_integer<timestamp_t>::max_distance() - 1;

	m_first_known_this_piece_time = now;
	m_server_request_deadline = server_request_deadline;
	m_pull_cnt = 0;

	DEBUG_SCOPE(
	m_pull_edps.clear();
	);
}

void absent_packet_info::recvd(seqno_t seqno, const media_packet& pkt, seqno_t now)
{
	if (!is_this(seqno, now))
	{
		just_known(seqno, now);
	}
	else
	{//owner信息已经不再需要，清理掉节省内存
		m_owners.reset();
		m_peer_incharge.reset();
		DEBUG_SCOPE(
			s_pull_accumulator((double)m_pull_cnt);
			m_pull_edps.clear();
		);
	}
}

void absent_packet_info::request_failed(timestamp_t now, int reRequestDelay)
{
	peer_connection* conn = m_peer_incharge.lock().get();
	if (conn&&conn->is_connected())
	{
		peer* peerIncharge = conn->get_peer().get();
		if (peerIncharge)
			peerIncharge->task_fail(m_seqno);
	}
	m_peer_incharge.reset();
	m_must_pull = true;
	m_pull_outtime = now + reRequestDelay;
}

NAMESPACE_END(p2client);


