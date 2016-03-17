#include "tracker/member_table.h"
#include "tracker/member_service.h"

typedef member_table::peer peer;
/************************************************************************/
/* channel																*/
/************************************************************************/
void channel::register_message_handler(message_socket* conn)
{
#define REGISTER_HANDLER(msgType, handler)\
	conn->register_message_handler(msgType, boost::bind(&this_type::handler, this, conn, _1));

	REGISTER_HANDLER(tracker_peer_msg::logout, on_recvd_logout);
	REGISTER_HANDLER(tracker_peer_msg::peer_request, on_recvd_peer_request);
	REGISTER_HANDLER(tracker_peer_msg::failure_report, on_recvd_failure_report);
	REGISTER_HANDLER(tracker_peer_msg::local_info_report, on_recvd_local_info_report);
	REGISTER_HANDLER(tracker_peer_msg::play_quality_report, on_recvd_quality_report);

	REGISTER_HANDLER(global_msg::relay, on_recvd_relay);

#undef REGISTER_HANDLER
}

void channel::register_server_message_handler(message_socket* conn)
{
#define REGISTER_HANDLER(msgType, handler)\
	conn->register_message_handler(msgType, boost::bind(&this_type::handler, this, conn, _1));

	REGISTER_HANDLER(server_tracker_msg::info_report, on_recvd_server_info_report);
	REGISTER_HANDLER(server_tracker_msg::info_request, on_recvd_channel_info_request);

#undef REGISTER_HANDLER
}

channel::channel(boost::shared_ptr<member_service> svc, 
	message_socket_sptr sock, 
	const std::string& channelID, 
	int64_t totalVideoDurationMsec)
	: basic_tracker_object(svc->get_tracker_param_sptr())
	, m_server_socket(sock)
	, m_channel_id(channelID)
	, m_peer_count(0)
	, m_member_table(new member_table(svc, totalVideoDurationMsec))
	, member_service_(svc)
	, recent_offline_peers_not_broadcast_cnt_(0)
	, playing_quality_(1.0)
	, global_rtol_lost_rate_(0.0)
	, last_write_quality_time_(timestamp_now())
{
	m_member_table->update_video_time(totalVideoDurationMsec, 0);
}

const peer* channel::insert(message_socket_sptr conn, const peer_info& infoIn)
{
	//转成动态时间坐标系坐标
	int dynamic_pt = dynamic_play_point(infoIn.relative_playing_point());
	size_t cnt=m_member_table->size();
	const peer* p = m_member_table->insert(conn, infoIn, dynamic_pt);
	if(!p)
		return NULL;
	if (m_member_table->size()>cnt)//new peer
		add_recent_login_peers(p);

	register_message_handler(conn.get());
	conn->register_disconnected_handler(
		boost::bind(&this_type::on_peer_disconnected, this, conn.get(), _1)
		);
	DEBUG_SCOPE(
		std::cout<<"member channel(" <<channel_id()
		<<") has " <<m_member_table->size()<<" peers"
		<<std::endl;
	);
	return p;
}

void channel::erase(message_socket_sptr conn, error_code/* = error_code()*/)
{
	//OBJ_PROTECTOR(hold_this); //
	m_member_table->erase(conn);
	if (m_member_table->empty())
	{
		BOOST_AUTO(svc, member_service_.lock());
		if (svc)
		{
			svc->get_io_service().post(
				boost::bind(&member_service::remove_channel, svc, m_channel_id)
				);
		}
	}
}

void channel::kickout(const peer_id_t& id)
{
	BOOST_AUTO(svc, member_service_.lock());
	if (!svc)
		return;

	//查找这个节点
	const peer* p = m_member_table->find(id);
	if (!p) 
		return;

	svc->known_offline(*p);
	recent_offline_peers_increase(p);
	//只有离线节点个数占所有节点比例达到一定数目才发通知
	on_broadcast_room_info();
	//将他从表中移除
	m_member_table->erase(id);

	//if (m_member_table->empty())
	//{
	//	svc->get_io_service().post(
	//		boost::bind(&member_service::remove_channel, svc, m_channel_id)
	//		);
	//}
}

void channel::start(const peer_info& serv_info, const live_channel_info& info)
{
	BOOST_AUTO(svc, member_service_.lock());
	if (!svc)
		return;

	peer_info serv_peer_info = serv_info;
	serv_peer_info.set_join_time(static_cast<tick_type>(tick_now()));
	serv_peer_info.set_nat_type(NAT_UNKNOWN);
	serv_peer_info.set_upload_capacity(1024*1024);
	serv_peer_info.set_info_version(0);

	set_server_info(serv_peer_info);
	set_channel_info(info);
	set_last_update_time(timestamp_now());
	start_broadcast_timer(svc->get_io_service());

	//注册server链接的消息处理
	register_server_message_handler(server_socket().get());
}

void channel::start(const peer_info& serv_info, const vod_channel_info& info)
{
	set_channel_info(info);
	set_server_info(const_cast<peer_info&>(serv_info));

	//注册server链接的消息处理
	register_server_message_handler(server_socket().get());
}

void channel::stop()
{
	if (channel_info_broadcast_timer_)
	{
		channel_info_broadcast_timer_->cancel();
		channel_info_broadcast_timer_.reset();
	}
}

timestamp_t channel::server_now()const
{
	//BOOST_ASSERT(live_channel_info_);
	//点播在login_reply中也调用
	timestamp_t now=timestamp_now();
	if (live_channel_info_)
	{
		return timestamp_t(live_channel_info_->server_time())
			+time_minus(now, last_updata_live_channel_info_time_);
	}
	BOOST_ASSERT(vod_channel_info_);
	return now;
}

seqno_t channel::server_seqno()const
{
	if(live_channel_info_)
	{
		timestamp_t now=timestamp_now();
		return live_channel_info_->server_seqno()
			+live_channel_info_->server_packet_rate()
			*time_minus(now, last_updata_live_channel_info_time_)/1000;
	}

	BOOST_ASSERT(vod_channel_info_);
	return 0;
}

void channel::start_broadcast_timer(io_service& io_service_in)
{
	//只对直播有效
	if(!live_channel_info_) 
		return;
	if (channel_info_broadcast_timer_)
		return;

	last_broadcast_channel_info_time_ = timestamp_now();
	channel_info_broadcast_timer_ = timer::create(io_service_in);
	channel_info_broadcast_timer_->set_obj_desc("tracker::channel::channel_info_broadcast_timer_");
	channel_info_broadcast_timer_->register_time_handler(
		boost::bind(&this_type::on_broadcast_room_info, this)
		);
	int millSec=(5*1000);
	channel_info_broadcast_timer_->async_keep_waiting(
		milliseconds(random(0, millSec)), milliseconds(millSec));
}

void channel::reset_broadcaster_timer(const time_duration& periodical_duration)
{
	if(!channel_info_broadcast_timer_)
		return;

	channel_info_broadcast_timer_->cancel();

	int millSec=(int)periodical_duration.total_milliseconds();
	channel_info_broadcast_timer_->async_keep_waiting(
		milliseconds(random(0, millSec)), milliseconds(millSec));
}

bool channel::broadcast_condition()
{
	return (
		(m_member_table->size()<100
		||recent_offline_peers_not_broadcast_cnt_>=64
		||recent_offline_peers_not_broadcast_cnt_>(int)(m_member_table->size()/10)
		)
		&&is_time_passed(2000, last_broadcast_channel_info_time_, (timestamp_t)timestamp_now())
		);
}

void channel::recent_offline_peers_increase(const peer* p)
{
	if (p)
	{
		recent_offline_peers_not_broadcast_cnt_++;
		m_recent_offline_peers.push_back(std::make_pair(p->get_id(), 0));
		if (m_recent_offline_peers.size()>64)
			m_recent_offline_peers.pop_front();
	}
}

void channel::add_recent_login_peers(const peer*p)
{
	m_recent_login_peers.push_back(
		std::make_pair(boost::weak_ptr<peer>(((peer*)p)->shared_obj_from_this<peer>()), 0)
		);

	if ((m_recent_login_peers.size()>=8
		||(m_member_table->size()<60&&m_recent_login_peers.size()>m_member_table->size()/10)
		)
		&&is_time_passed(2000, last_broadcast_channel_info_time_, (timestamp_t)timestamp_now())
		)
	{
		on_broadcast_room_info();
	}

	while(m_recent_login_peers.size()>2*MAX_CNT)
	{
		m_recent_login_peers.pop_front();
	}
}

void channel::on_broadcast_room_info()
{
	//非直播 返回
	if(!live_channel_info_) 
		return;

	if(!broadcast_condition()) 
		return;

	bool must_broadcast = is_interactive_category(get_tracker_param_sptr()->type);
	channel_broadcast()(this, must_broadcast);
}

void channel::set_server_socket(message_socket_sptr sock)
{
	boost::shared_ptr<member_service> svc(member_service_.lock());
	if (!svc)
		return;

	if (m_server_socket==sock)
		return;

	m_server_socket = sock;
	m_server_socket->register_disconnected_handler(boost::bind(
		&this_type::on_server_disconnected, this, sock.get(), _1
		));
}

void channel::on_server_disconnected(message_socket* conn, error_code ec)
{
	if (conn==m_server_socket.get()||!m_server_socket)
	{
		boost::shared_ptr<member_service> svc(member_service_.lock());
		if (!svc)
			return;
		svc->remove_channel(channel_id());
	}
}

void channel::on_peer_disconnected(message_socket* conn, error_code ec)
{
	if (conn)
		erase(conn->shared_obj_from_this<message_socket>());
}

void channel::on_recvd_logout(message_socket* sock, const safe_buffer& buf)
{
	p2ts_logout_msg msg;
	if (!parser(buf, msg))
		return;

	BOOST_AUTO(conn, sock->shared_obj_from_this<message_socket>());
	conn->close();
	erase(conn);
}

void channel::on_recvd_peer_request(message_socket* sockPtr, const safe_buffer& buf)
{
	p2ts_peer_request_msg reqMsg;
	if(!parser(buf, reqMsg)) 
		return;

	const peer* p = m_member_table->find(sockPtr);
	if (!p) 
		return;

	//更新当前频道播放质量等信息
	const peer_info& info =reqMsg.peer_info();
	double alf=std::max(0.01, 1.0/double(m_member_table->size()+1));
	playing_quality_ = alf*info.playing_quality() + (1.0-alf)*playing_quality_;
	global_rtol_lost_rate_ =alf*info.global_remote_to_local_lost_rate()
		+ (1.0-alf)*global_rtol_lost_rate_;

	//发送响应消息
	ts2p_peer_reply_msg msg;
	set_reply_msg(p, msg, reqMsg);
	sockPtr->async_send_reliable(serialize(msg), tracker_peer_msg::peer_reply);
}

template<typename msg_type>
void channel::add_peer_info(msg_type& msg, const std::vector<const peer*>& returnVec)
{
	BOOST_FOREACH(const peer* p, returnVec)
	{
		if (p)
		{
			peer_info* pinfo = msg.add_peer_info_list();
			(*pinfo) = p->m_peer_info;
		}
	}
}

template<typename msg_type>
void channel::find_member_for(const peer& target, msg_type& msg)
{
	std::vector<const peer*> returnVec;
	m_member_table->find_assist_server_for(target, returnVec);
	add_peer_info(msg, returnVec);

	returnVec.clear();
	m_member_table->find_member_for(target, returnVec);
	add_peer_info(msg, returnVec);
}

void channel::set_reply_msg(const peer* p, ts2p_peer_reply_msg& msg, 
	const p2ts_peer_request_msg& reqMsg)
{
	msg.set_error_code(e_no_error);
	msg.set_session_id(reqMsg.session_id());
	msg.set_cache_peer_cnt(0);
	msg.set_channel_id(channel_id());

	//首先放入server信息, 然后增加其它节点的信息
	peer_info* pinfo = msg.add_peer_info_list();
	(*pinfo) = server_info();

	//辅助服务器和普通节点
	find_member_for(*p, msg);
}

void channel::on_recvd_local_info_report(message_socket*sockPtr, const safe_buffer&buf)
{
	p2ts_local_info_report_msg msg;
	if (!parser(buf, msg)) 
		return;

	message_socket_sptr conn=sockPtr->shared_obj_from_this<message_socket>();
	const peer_info& info = msg.peer_info();
	int dynamic_pt = dynamic_play_point(info.relative_playing_point());
	const peer* p=m_member_table->updata(conn, info, dynamic_pt);
	if (!p) 
		return;

	double peerCnt=m_member_table->size();
	double alf=std::max(0.01, 1.0/double(peerCnt+1.0));
	playing_quality_ = alf*info.playing_quality() + (1.0-alf)*playing_quality_;
	global_rtol_lost_rate_ =alf*info.global_remote_to_local_lost_rate()
		+ (1.0-alf)*global_rtol_lost_rate_;

	DEBUG_SCOPE(
		std::cout<<"member channel: "
		<<channel_id()
		<<" recvd client info report from peer: "
		<<string_to_hex(p->m_peer_info.peer_id())
		<<" relative play point: "<<p->get_relative_playing_point_slot()
		<<std::endl;
	);
}

void channel::login_reply(message_socket_sptr conn, p2ts_login_msg& msg, const peer* p)
{
	ts2p_login_reply_msg repmsg;
	repmsg.set_external_ip(p->get_ipport().ip);
	repmsg.set_external_port(p->get_ipport().port);
	repmsg.set_error_code(e_no_error);
	repmsg.set_session_id(msg.session_id());
	repmsg.set_join_time(server_now());
	repmsg.set_online_peer_cnt((int)m_member_table->size());
	if(vod_channel_info_)
	{
		repmsg.set_channel_id(vod_channel_info_->channel_uuid());
		//首先放入server信息, 然后增加其它节点的信息, 点播的server节点从哪里获取？
		*repmsg.add_peer_info_list()=server_info();
		*repmsg.mutable_vod_channel_info()=*vod_channel_info_;
	}
	else if (live_channel_info_)
	{
		repmsg.set_channel_id(live_channel_info_->channel_uuid());
		//直播的话，首先放入server信息, 然后增加其它节点的信息
		*repmsg.add_peer_info_list()=server_info();
		*repmsg.mutable_live_channel_info()=*live_channel_info_;
	}
	repmsg.set_cache_tracker_addr(get_tracker_param_sptr()->external_ipport);
	BOOST_FOREACH(seqno_t seqno, iframes_)
	{
		repmsg.add_iframe_seqno(seqno);
	}
	find_member_for(*p, repmsg);

	//reply
	conn->async_send_reliable(serialize(repmsg), tracker_peer_msg::login_reply);
}

void channel::on_recvd_failure_report(message_socket* sockPtr, const safe_buffer&buf)
{
	//TODO:智能一些，要将那些“爱打假报告”的节点踢出去
	p2ts_failure_report_msg msg;
	if (!parser(buf, msg)) 
		return;

	const peer* p=m_member_table->find(peer_id_t(msg.peer_id()));
	if(!p)
		return;

	error_code ec;
	if(!p->get_socket()||!p->get_socket()->is_connected()) 
	{
		on_peer_disconnected(p->get_socket().get(), ec);
	}
	else
	{
		p->get_socket()->ping(ec);//向其发送一个ping，以探测器在线情况
	}
}

void channel::on_recvd_server_info_report(message_socket*sock, const safe_buffer&buf)
{
	s2ts_channel_report_msg msg;
	if (!parser(buf, msg))
		return;

	BOOST_ASSERT(live_channel_info_);//应该是一个直播频道
	if (!live_channel_info_)
		return;

	set_channel_info(msg.channel_info());
	set_last_update_time(timestamp_now());

	//清理错误的数据，这往往是由mds重启引起
	if (msg.iframe_seqno_size()>0&&!iframes_.empty())
	{
		if (abs(seqno_minus(msg.iframe_seqno(0), *iframes_.rbegin()))>2048)
			iframes_.clear();
	}

	for(int i=std::min((int)msg.iframe_seqno_size()-1, 20);i>=0;--i)
	{
		if(iframes_.insert(msg.iframe_seqno(i)).second==false)
			break;
	}
	while(iframes_.size()>20)
	{
		iframes_.erase(iframes_.begin());
	}
}

void channel::on_recvd_channel_info_request(message_socket* conn, 
	const safe_buffer& buf)
{
	s2ts_channel_status_req msg;
	if(!parser(buf, msg))
		return;

	std::string channel_id = vod_channel_info_? vod_channel_info_->channel_uuid() 
		: (live_channel_info_?live_channel_info_->channel_uuid():"");
	if(msg.channel_id() != channel_id)
		return;

	ts2s_channel_status reply_msg;
	reply_msg.set_live_cnt(m_member_table->size());
	reply_msg.set_playing_quality(playing_quality_);
	reply_msg.set_rtol_lost_rate(global_rtol_lost_rate_);
	//所有channel的play_quality和global_lost_rate

	conn->async_send_reliable(serialize(reply_msg), server_tracker_msg::info_reply);
}

void channel::on_recvd_quality_report(message_socket* sockPtr, const safe_buffer&buf)
{
	p2ts_quality_report_msg msg;
	if(!parser(buf, msg))
		return;

	const peer* p = m_member_table->find(sockPtr);
	if (!p)
		return;

	double alf=std::max(0.01, 1.0/double(m_member_table->size()+1.0));
	playing_quality_ = alf*msg.playing_quality() + (1.0-alf)*playing_quality_;

	timestamp_t now=timestamp_now();
	if(is_time_passed(QUALITY_REPORT_INTERVAL, last_write_quality_time_, now))
	{
		BOOST_AUTO(svc, member_service_.lock());
		if(svc) 
			svc->recvd_peer_info_report(const_cast<peer*>(p), msg);
		last_write_quality_time_=now;
	}
}

int channel::dynamic_play_point(const int play_point)
{
	if(!vod_channel_info_
		||vod_channel_info_->film_length()==0||vod_channel_info_->film_duration()==0) 
	{
		return -1;
	}

	//计算相对播放点
	int64_t now = tick_now();
	int n_c = floor(float(now/vod_channel_info_->film_duration()));
	int coord_o = now % vod_channel_info_->film_duration();

	//相对播放点 //返回的播放点都加了this_channel->vod_channel_info_->film_duration()，使得计算的桶号>= 0
	int delta = play_point*(double(vod_channel_info_->film_duration())/vod_channel_info_->film_length()) - coord_o;
	if (0 == n_c%2) //偶数
	{
		return delta + vod_channel_info_->film_duration();
	}
	else
	{
		return (delta < 0) ? (delta + 2 * vod_channel_info_->film_duration()):delta 
			/*- this_channel->vod_channel_info_->film_duration()*/;
	}
}

void channel::on_recvd_relay(message_socket*sock, const safe_buffer&buf)
{
	relay_msg msg;
	if (!parser(buf, msg)
		||!relay_msg_keeper_.try_keep(msg.msg_id(), seconds(20))
		||msg.ttl()<1
		)
	{
		return;
	}

	//目标节点与源节点是否存在
	peer* dstp = (peer*)m_member_table->find(peer_id_t(msg.dst_peer_id()));
	if (!dstp) return;

	peer* srcp = (peer*)m_member_table->find(sock);
	if (!srcp)  return;

	//频繁的要求中继报文是不允许的
	if(!keep_relay_msg(msg.msg_id(), srcp)) 
		return;

	//发送消息
	msg.set_ttl(msg.ttl()-1);
	dstp->get_socket()->async_send_reliable(serialize(msg), global_msg::relay);
}

bool channel::keep_relay_msg(uint64_t msg_id, peer* p)
{
	p->m_msg_relay_keeper.try_keep(msg_id, seconds(10));
	if(p->m_msg_relay_keeper.size()>20)
		return false;

	return true;
}

void channel::set_channel_info(const vod_channel_info& info)
{
	vod_channel_info_.reset(info);
	m_member_table->update_video_time(info.film_duration(), info.film_length());
}

/************************************************************************/
/* channel::channel_broad_caster                    */
/************************************************************************/
channel::channel_broadcast::channel_broadcast()
	: new_peer_n_(0)
{
}

void channel::channel_broadcast::operator ()(channel* this_channel, bool must_broad_cast)
{
	if(!this_channel)
		return;

	channel_ = this_channel;
	must_broad_cast_ = must_broad_cast;
	broadcast_info();
}

void channel::channel_broadcast::recent_offline(ts2p_room_info_msg& msg)
{
	int leftN = 0;
	channel_->recent_offline_peers_not_broadcast_cnt_ = 0;
	BOOST_AUTO(itr, channel_->m_recent_offline_peers.begin());
	for (; itr != channel_->m_recent_offline_peers.end()&&leftN<80;++leftN)
	{
		pair& idPair = *itr;
		msg.add_offline_peer_list(&idPair.first[0], idPair.first.size());

		++idPair.second;
		if (pair_need_erase(idPair))
			channel_->m_recent_offline_peers.erase(itr++);
		else
			++itr;
	}
	new_peer_n_ += leftN/8;
}

void channel::channel_broadcast::recent_login(ts2p_room_info_msg& msg)
{
	//写入最近刚刚加入的节点
	bool broadcastOnce=(channel_->m_recent_login_peers.size()>20);
	for (BOOST_AUTO(itr, channel_->m_recent_login_peers.begin())
		;itr != channel_->m_recent_login_peers.end();)
	{
		const boost::shared_ptr<peer>p(itr->first.lock());
		if (p)
		{
			hasWritten_.insert(p.get());
			*(msg.add_new_login_peer_list()) = p->m_peer_info;

			if (++itr->second>=2||broadcastOnce)
				channel_->m_recent_login_peers.erase(itr++);
			else
				++itr;
			if(++new_peer_n_ > MAX_CNT)
				break;
		}
		else
		{
			channel_->m_recent_login_peers.erase(itr++);
		}
	}
}

void channel::channel_broadcast::online_peers(ts2p_room_info_msg& msg)
{
	if (channel_->m_member_table->size()<MAX_CNT)
	{
		std::vector<const peer*> peers;
		channel_->m_member_table->get_all_peers(peers);
		for (size_t i=0;i<peers.size()&&new_peer_n_<MAX_CNT;++new_peer_n_, ++i)
		{
			const peer* p = peers[i];
			if (p&&!has_written(p))
			{
				*(msg.add_online_peer_list()) = p->m_peer_info;
			}
		}
	}
	msg.set_online_peer_cnt(channel_->m_member_table->size());
}

void channel::channel_broadcast::broad_cast_msg(const ts2p_room_info_msg& msg)
{
	safe_buffer sndbuf = serialize(tracker_peer_msg::room_info, msg);
	channel_->m_server_socket->async_send_reliable(sndbuf, server_tracker_msg::distribute);
	channel_->last_broadcast_channel_info_time_ = timestamp_now();
}

bool channel::channel_broadcast::pair_need_erase(const pair& idPair)
{
	return (
		idPair.second>5
		||(idPair.second>3 && channel_->m_recent_offline_peers.size()>20)
		||(idPair.second>1 && channel_->m_recent_offline_peers.size()>40)
		);
}

void channel::channel_broadcast::broadcast_info()
{
	new_peer_n_=0;

	ts2p_room_info_msg msg;
	recent_offline(msg);
	recent_login(msg);
	online_peers(msg);

	broad_cast_msg(msg);
};

/************************************************************************/
/* member_table                             */
/************************************************************************/
member_table::member_table(boost::shared_ptr<member_service> svc, 
	int totalVideoTimeMmsec
	)
	:basic_tracker_object(svc->get_tracker_param_sptr())
	, play_point_adjust_coeff_(1.0)
{
	total_video_time_msec_=totalVideoTimeMmsec;
}

member_table::~member_table(){
	//peers_.clear();
	//server_set_.clear();
}

void member_table::set_privilege(const peer_id_t& peerID, int privilegeID, 
	bool privilege)
{
	//TODO:实现
	BOOST_ASSERT(0&&"未实现");
}

//如果插入成功，返回peer*；否则，返回NULL
const peer* member_table::insert(peer_set& peers, socket_sptr conn, 
	const peer_info& info, int dynamic_pt)
{
	DEBUG_SCOPE(
		std::cout<<"-------play slot: "<<translate_relative_playing_point_slot(dynamic_pt)<<std::endl;
	);

	dynamic_pt=translate_relative_playing_point_slot(dynamic_pt);
	boost::shared_ptr<peer> p(new peer(info, conn, dynamic_pt));

	BOOST_AUTO(insertRst, socket_index(peers).insert(p));
	if (insertRst.second == true)
	{
		return (insertRst.first->get());
	}
	else
	{
		return updata(conn, info, dynamic_pt);
	}
}

//点播过程中，拖放等交互操作导致的更新
const peer* member_table::updata(peer_set& peers, socket_sptr conn, 
	const peer_info& info, int dynamic_pt)
{
	boost::shared_ptr<peer> peerInTable;
	peer_info* peerInfoInTable=NULL;
	peer_id_t newID(info.peer_id());
	socket_index_type::iterator itr=socket_index(peers).find(conn);
	if (itr !=socket_index(peers).end())
	{
		peerInTable=*itr;
		peerInfoInTable=const_cast<peer_info*>(&peerInTable->m_peer_info);
		if (dynamic_pt != peerInTable->get_relative_playing_point_slot()
			||newID!=peerInTable->get_id()
			)
		{
			socket_index(peers).erase(itr);
			return insert(conn, info, dynamic_pt);
		}
	}
	else
	{
		id_index_type::iterator it=id_index(peers).find(newID);
		if (it !=id_index(peers).end())
		{
			peerInTable=*it;
			peerInfoInTable=const_cast<peer_info*>(&peerInTable->m_peer_info);
			if (conn != peerInTable->get_socket()
				||dynamic_pt != peerInTable->get_relative_playing_point_slot()
				)
			{
				id_index(peers).erase(it);
				return insert(conn, info, dynamic_pt);
			}
		}
	}

	//只更新info
	if (peerInfoInTable)
	{
		*peerInfoInTable=info;
		error_code ec;
		endpoint edp = conn->remote_endpoint(ec);
		peerInfoInTable->set_external_ip(edp.address().to_v4().to_ulong());
		if(conn->connection_category()==message_socket::UDP)
			peerInfoInTable->set_external_udp_port(edp.port());
		else
			peerInfoInTable->set_external_tcp_port(edp.port());
	}
	return peerInTable.get();
}

const peer* member_table::insert(socket_sptr conn, const peer_info& infoIn, 
	int dynamic_pt)
{
	if (ASSIST_SERVER == infoIn.peer_type()
		||SERVER==infoIn.peer_type())
	{
		return insert(server_set_, conn, infoIn, dynamic_pt);
	}
	return insert(peers_, conn, infoIn, dynamic_pt);
}

const peer* member_table::updata(socket_sptr conn, const peer_info& info, 
	int dynamic_pt)
{
	if (ASSIST_SERVER == info.peer_type())
		return updata(server_set_, conn, info, dynamic_pt);

	return updata(peers_, conn, info, dynamic_pt);
}

int member_table::translate_relative_playing_point_slot(int dynamic_pt)
{
	if (is_vod_type())//点播
		return dynamic_pt/PLAYING_POINT_SLOT_DURATION;
	else
		return -1;
}

void member_table::find_vod_member(const peer& target, const peer_set& peers, 
	std::vector<const peer*>& returnVec, size_t maxReturnCnt)
{
	returnVec.resize(maxReturnCnt);
	size_t nearCnt=maxReturnCnt;
	size_t i=0;

#define ppsi(x) playing_point_slot_index(x) 
	playing_point_index_type::iterator itrRight
		= ppsi(peers).lower_bound(target.get_relative_playing_point_slot());

	playing_point_index_type::iterator itrLeft = itrRight;
	for (; itrRight != ppsi(peers).end() && i<nearCnt/2; ++itrRight)
	{
		const peer& p=**itrRight;
		if(!play_point_is_valide(target, p)) 
			break;
		if (!is_same_id(target, p))
		{
			returnVec[i++]=&p;
		}
	}

	if (itrLeft != ppsi(peers).begin() && ((--itrLeft) != ppsi(peers).begin()))
	{
		for (;i<nearCnt;)
		{
			const peer& p=**itrLeft;
			if(!play_point_is_valide(target, p)) 
				break;
			if (!is_same_id(target, p))
			{
				returnVec[i++]=&p;
			}
			if (itrLeft == ppsi(peers).begin()) break;

			--itrLeft;
		}
	}
	returnVec.resize(std::min(i, maxReturnCnt));
}

void member_table::find_live_member(const peer& target, const peer_set& peers, 
	std::vector<const peer*>& returnVec, size_t maxReturnCnt)
{
	size_t i=0;
	returnVec.resize(maxReturnCnt);

	if (maxReturnCnt==0)
		return;

	//不够时，全部插入
	if (maxReturnCnt>=peers.size())
	{
		id_index_type::iterator itr = id_index(peers).begin();
		for (;itr != id_index(peers).end(); ++itr)
		{
			const peer& p=**itr;
			if (&target==&p) 
				continue;
			returnVec[i++]=&p;
		}
		returnVec.resize(std::min(i, maxReturnCnt));
		return;
	}


	//首先，搜索maxReturnCnt/2个与节点的IP最近的节点（这会以较大概率返回位于相同子网的节点）
	//当然，如果使用"网络坐标来"选择节点，效果应该更好。
	size_t nearCnt=maxReturnCnt/2;
	ip_index_type::iterator itrRight=ip_index(peers).lower_bound(target.get_ipport());
	ip_index_type::iterator itrLeft=itrRight;
	for (;itrRight != ip_index(peers).end()&&i<nearCnt/2;++itrRight)
	{
		const peer& p=**itrRight;
		if (is_same_id(target, p)) 
			continue;
		returnVec[i++]=&p;
	}
	if (itrLeft != ip_index(peers).end())
	{
		for (;i<nearCnt;)
		{
			const peer& p=**itrLeft;
			if (&target!=&p)
				returnVec[i++]=&p;
			if (itrLeft == ip_index(peers).begin())
				break;
			--itrLeft;
		}
	}

	//然后搜索maxReturnCnt/2个随机节点，避免信息孤岛的产生
	//TODO：这在节点较多时候操作较费时
	double probability= generate_probability(peers, maxReturnCnt, i);
	BOOST_AUTO(itr, ip_index(peers).begin());
	for (;itr != itrLeft && i<maxReturnCnt; ++itr)
	{
		const peer& p=**itr;
		if ((&target!=&p) && in_probability(probability))
			returnVec[i++] = &p;
	}

	itr = itrRight;
	for (;ip_index(peers).end() != itr && i<maxReturnCnt; ++itr)
	{
		const peer& p=**itr;
		if ((&target!=&p) && in_probability(probability))
			returnVec[i++] = &p;
	}
	returnVec.resize(std::min(i, maxReturnCnt));
}

void member_table::find_member_for(const peer& target, const peer_set& peers, 
	std::vector<const peer*>& returnVec, size_t maxReturnCnt)
{
	//点播
	if (is_vod_type())
	{
		returnVec.clear();
		find_vod_member(target, peers, returnVec, maxReturnCnt);
		return;
	}

	//live streaming
	BOOST_ASSERT(total_video_time_msec_<0);
	find_live_member(target, peers, returnVec, maxReturnCnt);
}

bool member_table::play_point_is_valide(const peer& target, const peer& candidate)
{
	if(abs(target.get_relative_playing_point_slot() - candidate.get_relative_playing_point_slot())
		* PLAYING_POINT_SLOT_DURATION > 5000//播放点差距过大
		)
	{
		return false;
	}

	return true;
}

void member_table::find_assist_server_for(const peer& target, 
	std::vector<const peer*>& returnVec, size_t maxReturnCnt/*=2*/)
{
	find_member_for(target, server_set_, returnVec, maxReturnCnt);
}

void member_table::get_all_peers(std::vector<const peer*>& returnVec)
{
	returnVec.clear();
	returnVec.resize(id_index(peers_).size());
	int i=0;
	for (id_index_type::iterator itr=id_index(peers_).begin();
		itr!=id_index(peers_).end(); ++itr)
	{
		returnVec[i++] = itr->get();
	}
}

const peer* member_table::find(const peer_id_t& peerID)
{
	id_index_type::iterator itr=id_index(peers_).find(peerID);
	if (itr != id_index(peers_).end())
		return itr->get();
	return NULL;
}

const peer* member_table::find(const socket_sptr& conn)
{
	socket_index_type::iterator itr=socket_index(peers_).find(conn);
	if (itr!=socket_index(peers_).end())
		return itr->get();
	return NULL;
}

void member_table::erase(const socket_sptr& conn)
{
	socket_index(peers_).erase(conn);
	socket_index(server_set_).erase(conn);
}

double member_table::generate_probability(const peer_set& peers, 
	size_t max_return_cnt, size_t already_pick_cnt)
{
	//maxReturnCnt-i+1中+1是为了稍稍增大选中概率，多一个总比少一个强
	double p = (double)(max_return_cnt - already_pick_cnt + 1)
		/std::max(1.0, (double)(id_index(peers).size() - already_pick_cnt));
	return std::max(0.1, p);
}

void member_table::update_video_time(int64_t video_time_msec, int64_t film_length)
{
	total_video_time_msec_ = video_time_msec;
	if(video_time_msec>0 && film_length>0)
		play_point_adjust_coeff_ = double(video_time_msec)/film_length;
}
