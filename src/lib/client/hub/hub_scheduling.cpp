#include "client/peer_connection.h"
#include "client/peer.h"
#include "client/client_service.h"
#include "client/tracker_manager.h"
#include "client/hub/hub_scheduling.h"
#include "client/hub/hub_topology.h"
#include "client/stream/stream_monitor.h"

#include <p2engine/push_warning_option.hpp>
#include <boost/dynamic_bitset.hpp>
#include <p2engine/pop_warning_option.hpp>

#if !defined(_DEBUG_SCOPE) && defined(NDEBUG)
#define  HUB_SCHEDULING_DBG(x) 
#else 
#define  HUB_SCHEDULING_DBG(x) x
#endif
using namespace p2client;
namespace{
	const time_duration SUPERVISOR_CHECK_INTERVAL=millisec(100);
	const time_duration SUPERVISOR_PING_INTERVAL=seconds(3);

	BOOST_STATIC_ASSERT(boost::is_unsigned<seqno_t>::value);

#define GUARD_TOPOLOGY \
	hub_topology_sptr topology=topology_.lock();\
	if (!topology) {/*stop();*/return;}

#define GUARD_OVERLAY \
	GUARD_TOPOLOGY\
	client_service_sptr  ovl=topology->get_client_service();\
	if (!ovl) {/*stop();*/return;}
}

void hub_scheduling::register_message_handler(peer_connection* conn)
{
#define REGISTER_HANDLER(msgType, handler)\
	conn->register_message_handler(msgType, boost::bind(&this_type::handler, this, conn, _1));\
	conn->register_disconnected_handler(boost::bind(&this_type::on_disconnected, this, conn, _1));\
	connections_[conn]=conn->shared_obj_from_this<peer_connection>();

	if (!conn) 
		return;
	REGISTER_HANDLER(peer_peer_msg::supervise_request, on_recvd_supervize_request);


#undef REGISTER_HANDLER
}

hub_scheduling::hub_scheduling(hub_topology_sptr tplgy)
:scheduling_base(tplgy->get_io_service(), tplgy->get_client_param_sptr())
, topology_(tplgy)
{
}

void hub_scheduling::start()
{
	timestamp_t now=timestamp_now();

	//启动各定时器
	if (!supervize_timer_)
	{
		supervize_timer_=timer::create(get_io_service());
		supervize_timer_->set_obj_desc("hub::hub_scheduling::supervize_timer_");

		supervize_timer_->register_time_handler(
			boost::bind(&this_type::on_check_supervize_timer, this VC9_BIND_BUG_PARAM_DUMMY)
			);
		supervize_timer_->async_keep_waiting(millisec(1000), SUPERVISOR_CHECK_INTERVAL);
	}
}

void hub_scheduling::stop(bool flush)
{
	(void)(flush);
	if (supervize_timer_)
	{
		supervize_timer_->cancel();
		supervize_timer_.reset();
	}
}

hub_scheduling::~hub_scheduling()
{
	stop();
}

void hub_scheduling::send_handshake_to(peer_connection* conn)
{
	//构造handshake消息
	p2p_handshake_msg msg;
	//msg.set_session_id(0);//暂时未用
	msg.set_playing_channel_id(get_client_param_sptr()->channel_uuid);
	*(msg.mutable_peer_info()) = get_client_param_sptr()->local_info;

	//发送消息
	conn->async_send_reliable(serialize(msg), peer_peer_msg::handshake_msg);
	conn->get_peer()->last_buffermap_exchange_time()=timestamp_now();
}

void hub_scheduling::set_play_offset(int64_t)
{
}

void hub_scheduling::neighbor_erased(const peer_id_t& id)
{
	if (supervisors_.find(id)!=supervisors_.end())
	{
		supervisors_.erase(id);

		//不要直接调用on_check_supervize_timer。因本函数是由topology回调的
		//此处若直接调用on_check_supervize_timer可能影响topology的
		//on_disconnected处理逻辑。
		get_io_service().post(
			make_alloc_handler(boost::bind(&hub_scheduling::on_check_supervize_timer, 
			SHARED_OBJ_FROM_THIS VC9_BIND_BUG_PARAM_DUMMY))
			);
	}
	if (supervised_.find(id)!=supervised_.end())
	{
		supervised_.erase(id);
		GUARD_OVERLAY;

		ovl->get_tracker_handler()->report_failure(id);
	}
	
	BOOST_AUTO(itr, connections_.begin());
	for (; itr != connections_.end(); ++itr)
	{
		peer_sptr p = itr->first->get_peer();
		if (!p)
			continue;

		const peer_id_t& p_id= peer_id_t(p->get_peer_info().peer_id());
		if(id == p_id)
		{
			connections_.erase(itr);
			break;
		}
	}
	
}

void hub_scheduling::on_disconnected(peer_connection* conn, const error_code& ec)
{
	peer_sptr p=conn->get_peer();
	if (p)
	{
		const peer_id_t& id= peer_id_t(p->get_peer_info().peer_id());
		if (supervisors_.find(id)!=supervisors_.end())
		{
			supervisors_.erase(id);

			//不要直接调用on_check_supervize_timer。因本函数是由topology回调的
			//此处若直接调用on_check_supervize_timer可能影响topology的
			//on_disconnected处理逻辑。
			get_io_service().post(
				make_alloc_handler(boost::bind(&hub_scheduling::on_check_supervize_timer, 
				SHARED_OBJ_FROM_THIS VC9_BIND_BUG_PARAM_DUMMY))
				);
		}
		if (supervised_.find(id)!=supervised_.end())
		{
			supervised_.erase(id);
			GUARD_OVERLAY;

			ovl->get_tracker_handler()->report_failure(id);
		}
	}

	connections_.erase(conn);
}

void hub_scheduling::on_recvd_supervize_request(peer_connection* conn, safe_buffer buf)
{
	HUB_SCHEDULING_DBG(
		std::cout<<"!!!!!!!!!!!!!!!!!!==recvd supervise_request"<<std::endl;
	);
	peer_sptr p=conn->get_peer();
	if (p)
	{
		supervised_.insert(peer_id_t(p->get_peer_info().peer_id()));
		conn->ping_interval(SUPERVISOR_PING_INTERVAL);
	}
}

void hub_scheduling::on_check_supervize_timer(VC9_BIND_BUG_PARAM)
{
	/*GUARD_TOPOLOGY*/GUARD_OVERLAY;
	const neighbor_map& neighbors_conn=topology->get_neighbor_connections();

	if (neighbors_conn.size()<=supervisors_.size())
		return;
	for (int i=0;i<SUPERVISOR_CNT;++i)
	{
		if ((int)supervisors_.size()>=SUPERVISOR_CNT)
			break;
		if (neighbors_conn.empty())
			return;
		BOOST_AUTO(itr, random_select(neighbors_conn.begin(), neighbors_conn.size()));
		peer_connection_sptr conn=itr->second;
		peer_sptr p=conn->get_peer();
		peer_id_t peerID(p->get_peer_info().peer_id());
		BOOST_ASSERT(p);
		if (supervisors_.find(peerID)==supervisors_.end())
		{
			//加入到监视者队列
			supervisors_.insert(peerID);

			conn->ping_interval(SUPERVISOR_PING_INTERVAL);

			//向对方发送请求
			p2message::p2p_supervise_request_msg msg;
		   //这里用peer_id生成key是不行的peer_key_t为32位 peer_id_t为128位
			msg.set_peer_key(ovl->local_peer_key()/*boost::lexical_cast<peer_key_t>(local_peer_info_.peer_id())*/);
			msg.mutable_buffermap();//TODO:此处没有用处，随便设置了
			conn->async_send_reliable(serialize(msg), peer_peer_msg::supervise_request);
			HUB_SCHEDULING_DBG(
				std::cout<<"!!!!!!!!!!!!!!!!!!==send supervise_request"<<std::endl;
				);
		}
	}

	//当监视者足够时，停止定时器轮询检查。neighbor_erased\on_disconnected会驱动进入本函数
	if ((int)supervisors_.size()>=SUPERVISOR_CNT)
	{
		if (supervize_timer_)
		{
			supervize_timer_->cancel();
			supervize_timer_.reset();
		}
	}
}