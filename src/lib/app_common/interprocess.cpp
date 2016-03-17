#include "app_common/interprocess.h"
#include "app_common/control_base.h"

NAMESPACE_BEGIN(p2control);


#define CONTROL_LOCK \
	boost::shared_ptr<control_base> Ctrl=ctrl_.lock(); \
	if(!Ctrl) return; \

void interprocess_server::register_message_handler(message_socket* conn)
{
#define REGISTER_HANDLER(msgType, handler)\
	conn->register_message_handler(msgType, boost::bind(&this_type::handler, this, conn, _1));

	REGISTER_HANDLER(control_cmd_msg::login_report, on_recvd_client_login);
	REGISTER_HANDLER(control_cmd_msg::cmd_reply, on_recvd_cmd_reply);
	REGISTER_HANDLER(control_cmd_msg::auth_message, on_recvd_auth_message);
	REGISTER_HANDLER(control_cmd_msg::alive_alarm, on_recvd_alarm);

#undef REGISTER_HANDLER
}

void interprocess_server::start(int& port, boost::shared_ptr<control_base> ctrl)
{
	BOOST_ASSERT(port > 0);
	BOOST_ASSERT(ctrl);

	ctrl_ = ctrl;
	__start(port);
}

void interprocess_server::__start(int& port)
{
	error_code ec;
	if (trdp_acceptor_)
	{
		trdp_acceptor_->close(ec);
		trdp_acceptor_.reset();
	}

	trdp_acceptor_ = trdp_acceptor::create(get_io_service(), true);
	trdp_acceptor_->register_accepted_handler(
		boost::bind(&this_type::on_accept, this, _1, _2));

	endpoint edp(asio::ip::address(), port);
	do
	{
		trdp_acceptor_->listen(edp, "alive_alarm_domain", ec);
		if (ec)
		{
			port = random((int)control_base::PORT_BEGIN,
				(int)control_base::PORT_END);
			edp.port(port);
		}
	} while (ec);
	trdp_acceptor_->keep_async_accepting();
	start_resend_timer();
}

void interprocess_server::start_timer(int port)
{
	if (!timer_)
	{
		timer_ = rough_timer::create(get_io_service());
		timer_->set_obj_desc("interprocess_server::timer_");
		timer_->register_time_handler(boost::bind(&this_type::__start, this, port));
	}
	timer_->async_wait(seconds(1));
}

void interprocess_server::start_resend_timer()
{
	if (!resend_timer_)
	{
		resend_timer_ = rough_timer::create(get_io_service());
		resend_timer_->set_obj_desc("p2control::interprocess_server::resend_timer_");
		resend_timer_->register_time_handler(boost::bind(&this_type::on_post_waiting_msg, this));
	}
	resend_timer_->async_keep_waiting(seconds(10), seconds(5));
}

void interprocess_server::on_accept(message_socket_sptr conn, const error_code& ec)
{
	if (ec || !conn)
		return;

	pending_sockets_.try_keep(conn, seconds(10));

	//绑定消息处理
	register_message_handler(conn.get());
	conn->register_disconnected_handler(boost::bind(
		&this_type::on_disconnected, this, conn.get(), _1
		));

	conn->keep_async_receiving();

	DEBUG_SCOPE(
		error_code ec1;
	std::cout << "xxxxxxxxxxxxxxxxxxx-accept connection from: " << conn->remote_endpoint(ec1) << std::endl;
	);
}

void interprocess_server::on_disconnected(message_socket* conn, const error_code& ec)
{
	try{
		DEBUG_SCOPE(
			std::cout << "alive_alarm_server on_disconnected" << ec.message().c_str() << std::endl;
		);

		for (BOOST_AUTO(itr, chnl_sock_map_.begin());
			itr != chnl_sock_map_.end(); ++itr)
		{
			if ((itr->second.get() == conn))
			{
				CONTROL_LOCK;
				Ctrl->on_client_dropped(itr->first);

				chnl_sock_map_.erase(itr);
				return;
			}
		}
	}
	catch (std::exception& e)
	{
		LogInfo("error, msg=%s", e.what());
		std::cout << e.what() << std::endl;
	}
	catch (...){}
}

void interprocess_server::__on_client_login(message_socket_sptr sock, const safe_buffer& buf)
{
	if (pending_sockets_.find(sock) == pending_sockets_.end())
		return;

	alive_alarm_report_msg msg;
	if (!parser(buf, msg))
	{
		pending_sockets_.erase(sock);
		return;
	}

	/*DEBUG_SCOPE(*/
	std::cout << "-------------recvd report id=" << msg.id() << " type=" << msg.type() << std::endl;
	/*);*/
	try
	{
		pending_sockets_.erase(sock);

		__id id;
		id.id = msg.id();
		id.type = msg.type();

		chnl_sock_map_.erase(id);
		chnl_sock_map_.insert(value_t(id, sock));

		CONTROL_LOCK;
		Ctrl->on_client_login(sock, buf);
	}
	catch (std::exception& e)
	{
		LogInfo("error, msg=%s", e.what());
		std::cout << e.what() << std::endl;
	}
	catch (...)
	{
	}
}

void interprocess_server::on_recvd_client_login(message_socket* conn, const safe_buffer& buf)
{
	__on_client_login(conn->shared_obj_from_this<message_socket>(), buf);
}

void interprocess_server::__on_recvd_cmd_reply(message_socket* conn, const safe_buffer& buf)
{
	CONTROL_LOCK;
	Ctrl->on_recvd_cmd_reply(buf);
}

void interprocess_server::on_recvd_cmd_reply(message_socket* conn, const safe_buffer& buf)
{
	c2s_cmd_reply_msg msg;
	if (parser(buf, msg))
	{
		__id id;
		id.id = msg.id();
		id.type = msg.type();
		unconfirmed_msg_.erase(id);
	}

	__on_recvd_cmd_reply(conn, buf);
}

void interprocess_server::on_recvd_alarm(message_socket* conn, const safe_buffer& buf)
{
	CONTROL_LOCK;
	Ctrl->on_recvd_alarm_message(buf);
}

void interprocess_server::on_recvd_auth_message(message_socket* conn, const safe_buffer& buf)
{
	CONTROL_LOCK;
	Ctrl->on_recvd_auth_message(buf);
}

void interprocess_server::post_cmd(int type, const std::string&ID, const safe_buffer& buf)
{
	__id id;
	id.id = ID;
	id.type = type;
	__post_cmd(id, buf);
}

void interprocess_server::__post_cmd(message_socket_sptr conn, const safe_buffer& buf)
{
	if (conn&&conn->is_connected())
	{
		conn->async_send_reliable(buf, control_cmd_msg::cmd);
	}
}

void interprocess_server::__post_cmd(const __id& id, const safe_buffer& buf)
{
	__id ID = id;
	try{
		if (unconfirmed_msg_.end() == unconfirmed_msg_.find(ID))
		{
			ID.send_time = timestamp_now();
			unconfirmed_msg_.insert(std::make_pair(ID, buf));
		}

		message_socket_sptr conn = chnl_sock_map_[ID];
		__post_cmd(conn, buf);
	}
	catch (const std::exception& e)
	{
		std::cout << e.what() << std::endl;
	}
	catch (...)
	{
	}
}

void interprocess_server::on_post_waiting_msg()
{
	//在循环中会有erase和insert操作，这里生成一份copy
	timestamp_t now = timestamp_now();
	boost::unordered_map<__id, safe_buffer> unconfirmedMsg = unconfirmed_msg_;

	for (BOOST_AUTO(itr, unconfirmedMsg.begin());
		itr != unconfirmedMsg.end(); ++itr)
	{
		const __id& id = itr->first;
		safe_buffer buf = itr->second;
		if (!is_time_passed(30000, id.send_time, now))
		{
			__post_cmd(id, buf);
		}
		else
		{
			mds_cmd_msg msg;
			parser(buf, msg);
			if (INTERNAL_SESSION_ID != msg.session_id())
			{
				CONTROL_LOCK;
				Ctrl->on_session_time_out(msg.session_id());
				unconfirmed_msg_.erase(id);
			}
		}
	}
}

void interprocess_server::stop()
{
	if (trdp_acceptor_)
	{
		error_code ec;
		trdp_acceptor_->close(ec);
		trdp_acceptor_.reset();
	}

	if (timer_)
	{
		timer_->cancel();
		timer_.reset();
	}
}


interprocess_server::interprocess_server(io_service& ios)
	: basic_engine_object(ios)
{}

interprocess_server::~interprocess_server()
{
	stop();
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////


void interprocess_client::start_connect_timer()
{
	if (!connect_timer_)
	{
		connect_timer_ = rough_timer::create(get_io_service());
		connect_timer_->set_obj_desc("interprocess_server::connect_timer_");
		connect_timer_->register_time_handler(boost::bind(boost::bind(&this_type::connect, this)));
	}
	connect_timer_->async_wait(seconds(1));
}

void interprocess_client::register_message_handler(message_socket* conn)
{
#define REGISTER_HANDLER(msgType, handler)\
	conn->register_message_handler(msgType, boost::bind(&this_type::handler, this, conn, _1));

	REGISTER_HANDLER(control_cmd_msg::cmd, on_recvd_cmd);

#undef REGISTER_HANDLER
}

void interprocess_client::connect()
{
	if (socket_)
	{
		socket_->close();
		socket_.reset();
	}

	endpoint edp;
	error_code ec;
	socket_ = trdp_message_socket::create(get_io_service(), true);
	socket_->open(edp, ec);
	socket_->register_connected_handler(boost::bind(&this_type::on_connected, this, socket_.get(), _1));
	socket_->async_connect(remote_edp_, "alive_alarm_domain");

	DEBUG_SCOPE(
		std::cout << "-----------alive alarm connect udp: " << remote_edp_ << std::endl;
	);

	start_connect_timer(); //超时没连上重连
}

void interprocess_client::stop()
{
	if (socket_)
	{
		socket_->close();
		socket_.reset();
	}

	if (connect_timer_)
	{
		connect_timer_->cancel();
		connect_timer_.reset();
	}
}

void interprocess_client::on_connected(message_socket* conn, const error_code& ec)
{
	if (connect_timer_)
		connect_timer_->cancel();

	if (ec)
	{
		start_connect_timer();
		return;
	}


	BOOST_ASSERT(conn == socket_.get());
	socket_->register_disconnected_handler(boost::bind(&this_type::on_disconnected, this, _1));
	register_message_handler(socket_.get());
	//socket_->keep_async_receiving();

	report_id();
}

void interprocess_client::report_id()
{
	//向server报告自己的id
	alive_alarm_report_msg msg;
	msg.set_id(id_);
	msg.set_type(type_);
	msg.set_pid(getpid());

	send(serialize(msg), control_cmd_msg::login_report);

	std::cout << "-------------report id: " << id_ << std::endl;
}

void interprocess_client::on_disconnected(const error_code& ec)
{
	std::cout << "alive_alarm_proxy on_disconnected" << ec.message().c_str() << std::endl;
	start_connect_timer();
}


interprocess_client::interprocess_client(io_service& ios
	, const std::string& id
	, const endpoint& remote_edp
	, int type)
	: basic_engine_object(ios)
	, id_(id)
	, remote_edp_(remote_edp)
	, type_(type)
{}

interprocess_client::~interprocess_client()
{
	stop();
}

void interprocess_client::on_recvd_cmd(message_socket* conn, safe_buffer buf)
{
	//启动、停止 、添加、删除等
	on_recvd_cmd_sig_(conn, buf);
}

void interprocess_client::send(const safe_buffer& buf, message_type msg_type)
{
	BOOST_ASSERT(socket_);
	if (socket_&&socket_->is_connected())
		socket_->async_send_reliable(buf, msg_type);
	else
		start_connect_timer();
}

void interprocess_client::reply_cmd(const std::string& sessionID, int Code, const std::string& errorMsg)
{
	c2s_cmd_reply_msg repMsg;
	repMsg.set_session_id(sessionID);
	repMsg.set_code(Code);
	repMsg.set_msg(errorMsg);
	repMsg.set_id(id_);
	repMsg.set_type(type_);

	send(serialize(repMsg), control_cmd_msg::cmd_reply);
}



NAMESPACE_END(p2control);

