#include "client/tracker_manager.h"
#include "client/client_service.h"
#include "client/peer.h"
#include "client/local_param.h"
#include "client/cache/cache_service.h"

#include "natpunch/auto_mapping.h"

#define DESIABLE_DEBUG_SCOPE 1
#if !defined(_DEBUG_SCOPE) && defined(NDEBUG) || DESIABLE_DEBUG_SCOPE
#	define  TRACKER_HANDLER_DBG(x)
#else  
#	define  TRACKER_HANDLER_DBG(x) x
#endif


NAMESPACE_BEGIN(p2client);

TRACKER_HANDLER_DBG(
namespace {
	static timestamp_t start_time;
	void on_unknown_message(message_type type, safe_buffer buf)
	{
		LogError("tracker_manager recvd unknown_message, type=%d, length=%d", type, buf.length());
	}
}
);

void tracker_manager::register_message_handler()
{
#define REGISTER_HANDLER(msgType, handler)\
	socket_->register_message_handler(msgType, boost::bind(&this_type::handler, this, _1));

	REGISTER_HANDLER(tracker_peer_msg::challenge, on_recvd_challenge);
	REGISTER_HANDLER(tracker_peer_msg::login_reply, on_recvd_login_reply);
	REGISTER_HANDLER(tracker_peer_msg::peer_reply, on_recvd_peer_reply);
	REGISTER_HANDLER(global_msg::relay, on_recvd_relay);

	TRACKER_HANDLER_DBG(
		socket_->register_unknown_message_handler(boost::bind(&on_unknown_message, _1, _2));
	);
#undef REGISTER_HANDLER
}


tracker_manager::tracker_manager(io_service& svc, client_param_sptr param, type_t type)
	: basic_engine_object(svc)
	, basic_client_object(param)
	, relogin_times_(0)
	, peer_cnt_(0)
	, tracker_type_(type)
{
	set_obj_desc("tracker_manager");
}

tracker_manager::~tracker_manager()
{
	TRACKER_HANDLER_DBG(
		LogInfo("~tracker_manager()");
	);
	do_stop(true);
}

void tracker_manager::start(const std::string& domain)
{
	do_stop(false);

	TRACKER_HANDLER_DBG(start_time = timestamp_now());

	timestamp_t now = timestamp_now();
	enum { kLongTime = 100 * 1000 };//一个够大的数就好
	state_ = state_init;
	domain_ = domain;
	relogin_times_ = 0;
	last_peer_request_time_.reset();
	last_connect_fail_time_.reset();
	last_connect_ok_time_.reset();

	connect_tracker(endpoint());
	start_timer();
}

void tracker_manager::stop()
{
	do_stop(true);
}

void tracker_manager::connect_tracker(const endpoint& localEdp, message_socket*conn, 
	error_code ec, coroutine coro)
{
	BOOST_ASSERT(get_client_param_sptr()->b_tracker_using_rudp
		|| get_client_param_sptr()->b_tracker_using_rtcp);

	message_socket_sptr connHolder;
	if (conn)//避免socket_map_.erase(conn)时候导致conn析构而造成本函数有问题（__do_connect_tracker是由conn回调的）
		connHolder = conn->shared_obj_from_this<message_socket>();

	message_socket_sptr newsock;
	CORO_REENTER(coro)
	{
		++relogin_times_;
		state_ = state_connecting;
		last_connect_fail_time_.reset();
		last_connect_ok_time_.reset();

		TRACKER_HANDLER_DBG(
			LogInfo("***********CONNECT TRACKER********************:%s, t=%d, try_times=%d", 
			get_client_param_sptr()->tracker_host.c_str(), (timestamp_t)timestamp_now(), relogin_times_
			);
		);

		//优先使用UDP
		if (get_client_param_sptr()->b_tracker_using_rudp)
		{
			newsock = urdp_message_socket::create(get_io_service(), true);
			newsock->open(endpoint(localEdp.address(), get_udp_mapping().first), ec);
			TRACKER_HANDLER_DBG(
				std::cout << "connecting trackor:" << get_client_param_sptr()->tracker_host
				<< ", UDP-localendpoint:" << newsock->local_endpoint(ec) << std::endl;
			);
			if (!ec)
			{
				socket_map_[newsock.get()] = newsock;
				CORO_YIELD(
					newsock->register_connected_handler(boost::bind(&this_type::connect_tracker, 
					this, localEdp, newsock.get(), _1, coro));
				newsock->async_connect(get_client_param_sptr()->tracker_host, domain_, seconds(10));
				);
			}
			else{
				TRACKER_HANDLER_DBG(
					std::cout << __LINE__ << " UDP connecting trackor error:" << ec.message() << std::endl;
				);
			}
		}
		if (conn)
		{
			if (!ec)
			{
				on_connected(conn, ec);
				return;
			}
			else
			{
				socket_map_.erase(conn);
				conn = NULL;
				TRACKER_HANDLER_DBG(
					std::cout << __LINE__ << " UDP connecting trackor error:" << ec.message() << std::endl;
				);
			}
		}

		//使用TCP
		if (get_client_param_sptr()->b_tracker_using_rtcp)
		{
			newsock = trdp_message_socket::create(get_io_service(), true);
			newsock->open(endpoint(localEdp.address(), 0), ec);
			TRACKER_HANDLER_DBG(
				std::cout << "connecting trackor  TCP-localendpoint:" << newsock->local_endpoint(ec)
				<< ", TCP-tractor:" << get_client_param_sptr()->tracker_host
				<< std::endl;
			);
			if (!ec)
			{
				socket_map_[newsock.get()] = newsock;
				CORO_YIELD(
					newsock->register_connected_handler(
					boost::bind(&this_type::connect_tracker, this, localEdp, newsock.get(), _1, coro)
					);
				newsock->async_connect(get_client_param_sptr()->tracker_host, domain_, seconds(5));
				);
			}
			else
			{
				TRACKER_HANDLER_DBG(
					std::cout << __LINE__ << " TCP connecting trackor error:" << ec.message() << std::endl;
				);
			}
		}
		if (conn)
		{
			if (!ec)
			{
				on_connected(conn, ec);
				return;
			}
			else
			{
				socket_map_.erase(conn);
				conn = NULL;

				TRACKER_HANDLER_DBG(
					std::cout << __LINE__ << " TCP connecting trackor error:" << ec.message() << std::endl;
				);
			}
		}
		TRACKER_HANDLER_DBG(
			std::cout << __LINE__ << " connecting trackor error:" << ec.message() << std::endl;
		);
		on_connected(conn, asio::error::not_socket);
	}
}

void tracker_manager::do_stop(bool clearSignal)
{
	state_ = state_init;
	if (socket_)
	{
		socket_->close();
		socket_.reset();
	}

	if (timer_)
	{
		timer_->cancel();
		timer_.reset();
	}

	if (clearSignal)
	{
		ON_KNOWN_NEW_PEER.clear();
		ON_LOGIN_FINISHED.clear();
		ON_RECVD_USERLEVEL_RELAY.clear();
	}
}

void tracker_manager::on_connected(message_socket* conn, error_code ec)
{
	BOOST_ASSERT(ec || conn&&conn->is_open());

	TRACKER_HANDLER_DBG(
		std::cout << "---tracker connected ?=" << !ec << std::endl;
	);

	if (!ec)
	{
		TRACKER_HANDLER_DBG(
			std::cout << "***********CONNECT TRACKER OK********************: now="
			<< (timestamp_t)timestamp_now()
			<< ", time=" << time_minus(timestamp_now(), start_time)
			<< ", type=" << (const char*)((conn->connection_category() == message_socket::UDP) ? "UDP" : "TCP")
			<< std::endl;
		);

		if (socket_map_.empty())
			return;
		BOOST_AUTO(itr, socket_map_.find(conn));
		if (socket_map_.end() == itr)
			return;
		else if (socket_ == itr->second)
			return;

		BOOST_ASSERT(socket_ != itr->second);

		state_ = state_logining;
		last_connect_ok_time_ = timestamp_now();
		error_code err;
		socket_ = itr->second;
		local_edp_ = socket_->local_endpoint(err);
		local_edp_->address(address());
		socket_map_.erase(itr);

		while (!socket_map_.empty())
		{
			BOOST_AUTO(it, socket_map_.begin());
			if (it->second)
				it->second->close();
			socket_map_.erase(it);
		}

		register_message_handler();
		socket_->register_disconnected_handler(boost::bind(&this_type::on_disconnected, this, conn, _1));
		socket_->keep_async_receiving();

		//将NAT类型信息更改为最新的探测结果
		bool internalAdded = false;
		std::set<address> addrs;
		get_available_address_v4(get_io_service(), addrs);
		peer_info& localInfo = get_client_param_sptr()->local_info;
		localInfo.clear_internal_ip();
		localInfo.clear_other_internal_ip();
		for (BOOST_AUTO(i, addrs.begin()); i != addrs.end(); ++i)
		{
			if (!is_loopback(*i) && !(is_any(*i)))
			{
				if (is_local(*i) && !internalAdded)
				{
					localInfo.set_internal_ip(i->to_v4().to_ulong());
					internalAdded = true;
				}
				else
				{
					localInfo.add_other_internal_ip(i->to_v4().to_ulong());
				}
			}
		}
		if (!localInfo.has_internal_ip() || localInfo.internal_ip() == 0)
			localInfo.set_internal_ip(local_edp_.get().address().to_v4().to_ulong());
		localInfo.set_internal_udp_port(local_edp_.get().port());
		localInfo.set_internal_tcp_port(local_edp_.get().port());
		//localInfo.set_nat_type(ovl->local_nat_type());
	}
	else
	{
		TRACKER_HANDLER_DBG(
			if (conn)
			{
			std::cout << "tracker_manager "
				<< (conn->connection_category() == message_socket::UDP ? "UDP" : "TCP")
				<< " connect failed, local edp:" << conn->local_endpoint(ec);
			}
		);

		last_connect_fail_time_ = timestamp_now();
		socket_map_.erase(conn);
	}
}

void tracker_manager::on_disconnected(message_socket* sock, const error_code& ec)
{
	TRACKER_HANDLER_DBG(
		std::cout << "!!!!!!!!!!!!!!!!!!!!!!!" << get_obj_desc() << " disconnected\n";
	);
	if (!socket_ || socket_&&sock == socket_.get())
	{
		socket_.reset();
		state_ = state_init;
		BOOST_ASSERT(timer_);
		if (timer_)
		{
			timer_->cancel();
			timer_->async_keep_waiting(seconds(random(10, 20)), seconds(1));//随机延迟一段时间后尝试重新链接
		}
		BOOST_ASSERT(socket_map_.empty());
	}
}

void tracker_manager::on_timer()
{
	switch (state_)
	{
	case state_init:
		connect_tracker(endpoint());
		break;
	case state_connecting:
	{//如果链接失败了，需要重新链接
		if (socket_map_.empty())
		{
			timestamp_t now = timestamp_now();
			int t = 1000 * (1 << (std::min(relogin_times_, 3)));//最多8秒
			if (last_connect_fail_time_&&is_time_passed(t, *last_connect_fail_time_, now))
				connect_tracker(endpoint());
		}
	}
		break;
	case state_logining:
	{//如果链接上了，但登录失败，需要重新链接（重新发送challenge？？）
		BOOST_ASSERT(last_connect_ok_time_);
		timestamp_t now = timestamp_now();
		if (last_connect_ok_time_&&is_time_passed(5000, *last_connect_ok_time_, now))
		{
			do_stop(false);
			//有时候UDP方式因为login reply过长而造成了udp frag，导致客户端收不到
			//这里交换使用两种链接方式吧。
			get_client_param_sptr()->b_tracker_using_rudp = !get_client_param_sptr()->b_tracker_using_rudp;
			connect_tracker(endpoint());
			get_client_param_sptr()->b_tracker_using_rudp = !get_client_param_sptr()->b_tracker_using_rudp;
		}
	}
		break;
	case state_logined:
	{//
		request_peer(get_client_param_sptr()->channel_uuid, false);
		if (cache_change_vector_.size())
		{
			cache_changed(cache_change_vector_);
			cache_change_vector_.clear();
		}
	}
		break;
	default:
		BOOST_ASSERT(0);
		break;
	}
}

void tracker_manager::request_peer(const std::string& channelID, bool definiteRequest)
{
	if (state_ != state_logined || !socket_ || !socket_->is_connected())
		return;

	const std::string* channelUID = &channelID;
	if (is_cache_category())
		channelUID = &channel_id_;
	if (channelUID->empty())
		return;

	if (ON_KNOWN_NEW_PEER.empty())
		return;

	timestamp_t now = timestamp_now();
	if (!definiteRequest)
	{
		tick_type duration = (live_type == tracker_type_ ?
			(50 + std::min(3 * peer_cnt_, 1000)) * 1000LL
			: (10 + std::min(3 * peer_cnt_, 1000)) * 1000LL
			);
		if (last_peer_request_time_&&!is_time_passed(duration, *last_peer_request_time_, now))
			return;
	}

	p2ts_peer_request_msg msg;
	if (channelUID)
		msg.set_channel_uuid(*channelUID);
	msg.set_session_id(0);//临时未用
	*(msg.mutable_peer_info()) = get_client_param_sptr()->local_info;

	socket_->async_send_reliable(serialize(msg), tracker_peer_msg::peer_request);
	last_peer_request_time_ = now;
}

void tracker_manager::set_channel_id(const std::string& channelID)
{
	channel_id_ = channelID;
	peer_cnt_ = 0;
}

void tracker_manager::report_failure(const peer_id_t& id)
{
	if (!is_online())
		return;

	p2ts_failure_report_msg msg;
	msg.set_peer_id(&id[0], id.size());

	socket_->async_send_reliable(serialize(msg), tracker_peer_msg::failure_report);
}

void tracker_manager::report_local_info()
{
	if (!is_online())
		return;

	p2ts_local_info_report_msg msg;
	*(msg.mutable_peer_info()) = get_client_param_sptr()->local_info;
	
	//TODO("别忘了去掉！！！！！！！！！！！！！！！！！！！！！"); msg.mutable_peer_info()->set_upload_capacity(1);

	socket_->async_send_reliable(serialize(msg), tracker_peer_msg::local_info_report);

	TRACKER_HANDLER_DBG(
		std::cout << "peer "
		<< string_to_hex(get_client_param_sptr()->local_info.peer_id())
		<< " report local info"
		<< std::endl;
	);
}

//向tracker报告播放质量
void tracker_manager::report_play_quality(const p2ts_quality_report_msg& msg)
{
	if (!is_online())
		return;

	socket_->async_send_reliable(serialize(msg), tracker_peer_msg::play_quality_report);
}

void tracker_manager::cache_changed(const std::vector<std::pair<std::string, int> >&changesVec)
{
	if (changesVec.empty()/*||!vod_channel_info_*/)
		return;
	if (!is_online())
	{
		if (socket_ && is_state(state_logining))
		{
			//note 这次操作返回的cache节点没有通知到topology，因为频道的实例还未创建
			//登录保证了handler和tracker之间连接的持续【pint interval】
			TRACKER_HANDLER_DBG(
				std::cout << "XXXXXXXXXXXXXX--register_playing_channel_to_cache\n";
			);
			//register_playing_channel_to_cache(changesVec.front().first);
		}

		for (size_t i = 0; i < changesVec.size(); ++i)
			cache_change_vector_.push_back(changesVec[i]);

		return;
	}

	p2ts_cache_announce_msg  msg;
	*(msg.mutable_peer_info()) = get_client_param_sptr()->local_info;
	for (size_t i = 0; i < changesVec.size(); ++i)
	{
		if (changesVec[i].second < 0)
		{
			msg.add_erased_channels(&(changesVec[i].first[0]), changesVec[i].first.size());
		}
		else
		{
			cached_channel_info* channel_info_sp = msg.add_cached_channels();
			channel_info_sp->set_channel_id(&(changesVec[i].first[0]), changesVec[i].first.size());
			channel_info_sp->set_healthy(changesVec[i].second);
		}
	}

	socket_->async_send_reliable(serialize(msg), tracker_peer_msg::cache_announce);
	TRACKER_HANDLER_DBG(
		std::cout << "XXXXXXXXXXXXXX--report cached channel\n";
	);
}

//////////////////////////////////////////////////////////////////////////
//message process
void tracker_manager::on_recvd_challenge(const safe_buffer& buf)
{
	if (!socket_)
		return;
	TRACKER_HANDLER_DBG(
		std::cout << "tracker_manager::on_recvd_challenge!!\n";
	);
	ts2p_challenge_msg msg;
	if (!parser(buf, msg))
	{
		if (state_logined == state_)
		{
			socket_->close();
			socket_.reset();
			socket_map_.clear();
			do_stop();
			return;
		}

		TRACKER_HANDLER_DBG(
			std::cout << "bad ts2p_challenge_msg!!!!!!!!!!!!\n";
		);
		do_stop();
		return;
	}
	challenge_response(msg, get_client_param_sptr(), socket_);
}

void tracker_manager::start_timer()
{
	if (!timer_)
	{
		timer_ = rough_timer::create(get_io_service());
		timer_->set_obj_desc("client::tracker_manager::relogin_timer_");
		timer_->register_time_handler(boost::bind(&this_type::on_timer, this));
	}
	else
	{
		timer_->cancel();
	}
	timer_->async_keep_waiting(seconds(1), seconds(1));
}

void tracker_manager::on_recvd_login_reply(const safe_buffer& buf)
{
	TRACKER_HANDLER_DBG(std::cout << "recvd ts2p_login_reply_msg.\n";);

	bool parserSuccess;
	ts2p_login_reply_msg rplMsg;
	//直播必须告知server
	if (!(parserSuccess = parser(buf, rplMsg))
		|| rplMsg.error_code() != e_no_error
		|| ((is_live_category(get_client_param_sptr()->type)) && (rplMsg.peer_info_list_size() == 0))//LIVE_TYPE时连server都没有告知
		|| (rplMsg.has_vod_channel_info() + rplMsg.has_live_channel_info()) > 1
		)
	{
		if (state_logined == state_)
		{
			socket_->close();
			socket_.reset();
			socket_map_.clear();
			do_stop();
			return;
		}
		else
		{
			error_code_enum ec = e_unreachable;
			if (parserSuccess)
			{
				int infoCnt = rplMsg.has_vod_channel_info() + rplMsg.has_live_channel_info();
				int errorCode = rplMsg.error_code();
				ec = (rplMsg.error_code() != e_no_error) ?
					(error_code_enum)rplMsg.error_code() : e_unreachable;
			}

			ON_LOGIN_FINISHED(ec, rplMsg);
			do_stop();
			return;
		}
	}

	//更新locle的peer_info信息
	{
		peer_info& localInfo = get_client_param_sptr()->local_info;
		if (is_global(address_v4(rplMsg.external_ip())))
			localInfo.set_external_ip(rplMsg.external_ip());
		localInfo.set_external_tcp_port(get_tcp_mapping().second);
		if (socket_->connection_category() == message_socket::UDP)
		{
			if (localInfo.nat_type() == NAT_UDP_BLOCKED)
				localInfo.set_nat_type(NAT_UNKNOWN);
			localInfo.set_external_udp_port(rplMsg.external_port());
		}
		else
		{
			localInfo.set_external_udp_port(get_udp_mapping().second);
		}
		localInfo.set_join_time(rplMsg.join_time());

		TRACKER_HANDLER_DBG(;
		std::cout << "------ internal_udp_endpoint: " << internal_udp_endpoint(localInfo) << std::endl;
		std::cout << "------ external_udp_endpoint: " << external_udp_endpoint(localInfo) << std::endl;
		std::cout << "------ internal_tcp_endpoint: " << internal_tcp_endpoint(localInfo) << std::endl;
		std::cout << "------ external_tcp_endpoint: " << external_tcp_endpoint(localInfo) << std::endl;
		);
	}

	//设置ping间隔，ping间隔要稍稍短于TRACKER_PEER_PING_INTERVAL，避免tracker误认为节点掉线
	socket_->ping_interval(get_client_param_sptr()->tracker_peer_ping_interval * 95 / 100);
	peer_cnt_ = rplMsg.online_peer_cnt();

	iframes_.clear();
	for (int i = 0; i < rplMsg.iframe_seqno_size(); ++i)
		iframes_.push_back(rplMsg.iframe_seqno(i));

	if (state_logined != state_)
	{
		state_ = state_logined;
		//last_peer_request_time_ = timestamp_now();
		relogin_times_ = 0;
		if (rplMsg.has_vod_channel_info())
		{
			vod_channel_info_ = rplMsg.vod_channel_info();
			BOOST_ASSERT(get_cache_tracker_handler(get_io_service()));
			shared_ptr cacheTrackerHandler = get_cache_tracker_handler(get_io_service());
			if (cacheTrackerHandler)
				cacheTrackerHandler->set_channel_id(vod_channel_info_->channel_uuid());
		}
		else if (rplMsg.has_live_channel_info())
		{
			live_channel_info_ = rplMsg.live_channel_info();
		}

		TRACKER_HANDLER_DBG(
			std::cout << "***********LOGIN FINISHED********************:"
			<< (timestamp_t)timestamp_now() << ", " << time_minus(timestamp_now(), start_time)
			<< std::endl;
		);

		ON_LOGIN_FINISHED(e_no_error, rplMsg);
	}

	std::vector<const peer_info*> peerInfo(rplMsg.peer_info_list_size());
	for (int i = 0; i < rplMsg.peer_info_list_size(); ++i)
	{
		peerInfo[i] = &rplMsg.peer_info_list(i);
	}
	if (!peerInfo.empty())
	{
		const std::string& channelUUID = rplMsg.channel_id();
		ON_KNOWN_NEW_PEER(channelUUID, peerInfo);
	}
}

void tracker_manager::on_recvd_relay(const safe_buffer&buf)
{
	relay_msg msg;
	if (!parser(buf, msg))
		return;

	if (!flood_or_relay_msgid_keeper_.try_keep(msg.msg_id(), seconds(120)))
		return;//本消息已经收到过
	if (msg.dst_peer_id() == get_client_param_sptr()->local_info.peer_id())
	{//消息的目标peer是本地
		switch (msg.level())
		{
		case SYSTEM_LEVEL:
		{
			if (msg.msg_data().length() < sizeof(message_t))
				return;
			safe_buffer msgbuf;
			safe_buffer_io io(&msgbuf);
			io.write(msg.msg_data().data(), msg.msg_data().length());
			message_t msgType;
			io >> msgType;

			if (msgType == peer_peer_msg::punch_request)
			{
				punch_request_msg requestMsg;
				if (!parser(buf, requestMsg))
					return;
				if (socket_&&socket_->connection_category() == message_socket::UDP)
				{
					socket_->on_received_punch_request(const_cast<p2engine::safe_buffer&>(buf));
				}
				else
				{
					error_code ec;
					endpoint localEdp;
					localEdp.port(get_client_param_sptr()->local_info.internal_udp_port());
					if (!punch_socket_ || !punch_socket_->is_open()
						|| punch_socket_->local_endpoint(ec).port() != localEdp.port())
					{
						ec.clear();
						punch_socket_ = urdp_message_socket::create(get_io_service(), true);
						punch_socket_->open(localEdp, ec);
					}
					if (!ec)
						punch_socket_->on_received_punch_request(const_cast<p2engine::safe_buffer&>(buf));
				}
			}
		}
			break;
		case  USER_LEVEL:
			ON_RECVD_USERLEVEL_RELAY(msg);
			break;

		default:
			BOOST_ASSERT(0);
		}
	}
	else
	{
		//不是dst，将消息往下传递，中继由hub层中完成，这里不做中继处理
		BOOST_ASSERT(0);
	}
}

//请求peer_request的回复报文
void tracker_manager::on_recvd_peer_reply(const safe_buffer& buf)
{
	ts2p_peer_reply_msg msg;
	if (!parser(buf, msg)
		|| msg.error_code() != e_no_error
		)
		return;

	if (tracker_type_ != live_type
		&&msg.channel_id() != channel_id_)
	{
		return;
	}

	if (msg.has_cache_peer_cnt())
		peer_cnt_ = msg.cache_peer_cnt();
	else
		peer_cnt_ = msg.peer_info_list_size();
	std::vector<const peer_info*> peerVec(msg.peer_info_list_size());
	TRACKER_HANDLER_DBG(std::cout << (tracker_type_ == live_type ? "Live" : "Cache")
		<< "##########################################[\n";);
	for (int i = 0; i < msg.peer_info_list_size(); ++i)
	{
		peerVec[i] = &msg.peer_info_list(i);
		TRACKER_HANDLER_DBG(
			std::cout << "peer reply peer: "/*<<string_to_hex(peerVec[i]->peer_id())*/
			<< " internal ip:" << internal_udp_endpoint(*peerVec[i])
			<< " external ip:" << external_tcp_endpoint(*peerVec[i])
			<< std::endl;
		);
	}
	TRACKER_HANDLER_DBG(std::cout << "##########################################]\n";);
	ON_KNOWN_NEW_PEER(msg.channel_id(), peerVec);
}

NAMESPACE_END(p2client);
