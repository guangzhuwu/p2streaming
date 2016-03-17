//
// absent_packet_info.h
// ~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2008 GuangZhu Wu (GuangZhuWu@gmail.com)
//
// All rights reserved. 
//
#ifndef client_absent_packet_info_h__
#define client_absent_packet_info_h__

#include <utility>
#include <list>

#include "client/typedef.h"
#include "client/peer_connection.h"

#ifdef DEBUG_SCOPE_OPENED
#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics/stats.hpp>
#include <boost/accumulators/statistics/mean.hpp>
#include <boost/accumulators/statistics/moment.hpp>
	using namespace boost::accumulators;
#endif

namespace p2client{

	class owner_map
	{
	public:
		owner_map() :owner_index_(NULL)
		{
			init();
		}
		~owner_map()
		{
			if (owner_index_) memory_pool::free(owner_index_);
		}
		size_t size()const
		{
			return owner_vec_.size();
		}
		bool empty()const
		{
			return owner_vec_.empty();
		}
		peer_connection_sptr select(size_t i);
		peer_connection_sptr random_select();
		void reset();
		void insert(const peer_connection_sptr& conn);
		void erase(const peer_connection_sptr& conn);
		peer_connection* find(const peer_connection_sptr& conn)const;
		bool dec_request_deadline(const peer_connection_sptr& conn);
		void dump();

	private:
		struct owner{
			enum { kRequestDeadline = 2 };
			owner() :id(INVALID_ID), uuid(INVALID_ID), request_deadline(kRequestDeadline)
				, is_link_local(0)
			{}
			owner(const peer_connection_sptr&_conn)
				: id(_conn->local_id()), uuid(_conn->local_uuid())
				, request_deadline(kRequestDeadline), is_link_local(_conn->is_link_local())
				, conn(_conn)
			{}
			~owner(){ reset(); }
			void assign(const peer_connection_sptr&_conn)
			{
				BOOST_ASSERT(
					id == INVALID_ID&&uuid == INVALID_ID
					|| (conn.expired() || !conn.lock()->is_open() || !conn.lock()->is_connected())
					|| request_deadline <= 0
					);
				id = _conn->local_id();
				uuid = _conn->local_uuid();
				request_deadline = kRequestDeadline;
				is_link_local = _conn->is_link_local();
				conn = _conn;
			}

			void reset()
			{
				id = uuid = INVALID_ID;
				request_deadline = 2;
				conn.reset();
			}
			boost::weak_ptr<peer_connection> conn;
			int32_t uuid;
			int8_t	id;
			uint8_t request_deadline : 4;
			uint8_t	is_link_local : 1;
		};
		void erase_owner(int id, const int* uuid = NULL);
		void init();
		//避免使用std::vector<char>时，有些编译器的resize会一个一个的初始化元素
		int8_t* owner_index_;
		std::vector<owner, p2engine::allocator<owner> > owner_vec_;
	};

	class absent_packet_info
		: public object_allocator
		, public basic_intrusive_ptr < absent_packet_info >
	{
	public:
		absent_packet_info()
			:inited__(false), m_seqno(0), m_server_request_deadline(6), m_pull_cnt(0)
		{
			DEBUG_SCOPE(
				total_info_object_cnt()++;
			);
		}
		~absent_packet_info()
		{
			DEBUG_SCOPE(
				total_info_object_cnt()--;
			);
		}

		bool is_this(seqno_t seqno, timestamp_t now)const
		{
			static const int kMaxTime = (sizeof(timestamp_t) <= 2 && sizeof(seqno_t) <= 2)
				? MAX_DELAY_GUARANTEE : (std::numeric_limits<int32_t>::max)() / 4;
			return(inited__&&m_seqno == seqno&&
				time_greater(m_first_known_this_piece_time + kMaxTime, now)
				);
		}
		void reset();
		void just_known(seqno_t seqno, timestamp_t now);
		void recvd(seqno_t seqno, const media_packet& pkt, seqno_t now);
		void request_failed(timestamp_t now, int reRequestDelay);

		//因为这一结构体将被存储在一个size很大的vector中，所以，要尽量减小这一结构体大小（例如使用位域）
		owner_map				m_owners;//有这个包的节点
		peer_connection_wptr	m_peer_incharge;//向谁请求的这个包

		timestamp_t				m_first_known_this_piece_time;//何时知道这一片段的
		timestamp_t				m_pull_time;//请求了这个包的时间
		timestamp_t				m_pull_outtime;//请求了这个包，其等待超时时间
		timestamp_t				m_last_check_peer_incharge_time;//最近一次检查peer_incharge的时间

		seqno_t					m_seqno;//序列号

		uint32_t					m_last_rto : 17;//上次校准m_pull_outtime时，peerincharge的rto
		uint32_t					m_priority : 3;//优先级
		uint32_t					m_server_request_deadline : 6; enum{ server_request_deadline = 16 };
		uint32_t					m_pull_cnt : 6;

		bool					inited__ : 1;
		bool					m_dskcached : 1;
		bool					m_must_pull : 1;//确认一个包只能通过pull方式获得（对super_seed起作用）


#ifdef DEBUG_SCOPE_OPENED
		static int& total_info_object_cnt()
		{
			static int  m_cnt = 0;
			return m_cnt;
		}
		struct endpoint_info{
			endpoint edp; timestamp_t t;
		};
		std::list<endpoint_info> m_pull_edps;
		static accumulator_set<double, stats<tag::mean > > s_pull_accumulator;
#endif
	};
}

#endif//client_absent_packet_info_h__


