/* config.ini - for shunt

[SERVICE]
name= p2s_shunt_service				//��������
[ALARM]
alive_alarm_port=8000				//����report�˿�
[CMD]
operation_http_port=60000			//��������˿�
[APP]
control_name=p2s_shunt_control.exe	//���ؽ���exe
sub_process=p2s_shunt.exe			//���ؽ���exe
*/

/* config.ini - for mds

[SERVICE]
name= p2s_p2v_service				//��������
[ALARM]
alive_alarm_port=8000				//����report�˿�
[CMD]
operation_http_port=60000			//��������˿�
[APP]
control_name=p2s_mds_control.exe	//���ؽ���exe
sub_process=p2s_mds_2.exe			//���ؽ���exe
*/

#include "control_base.h"

#include <boost/format.hpp>

#include <p2engine/utf8.hpp>

#include "libupnp/utility.h"
#include "libupnp/escape_string.hpp"

NAMESPACE_BEGIN(p2control);

control_base::control_base(io_service& ios)
	: basic_engine_object(ios)
{
	set_obj_desc("control_base");
}

control_base::~control_base()
{
	stop_cmd_accptor();
	stop_wild_check_timer();
	stop_time_out_timer();
	stop_sub_process_check_timer();
}

void control_base::start(const db_name_t& dbName,
	const host_name_t& hostName,
	const user_name_t& userName,
	const password_t& pwd,
	const char* subAppName, int aliveAlarmPort, int cmdPort)
{
	param_.dbName = dbName;
	param_.hostName = hostName;
	param_.userName = userName;
	param_.pwds = pwd;

	param_.child_process_name_ = subAppName;
	param_.http_cmd_port_ = (unsigned short)cmdPort;
	param_.max_channel_per_server_ = 100;
	param_.alive_alarm_port_ = aliveAlarmPort;

	__start();

	interprocess_server_ = interprocess_server::create(get_io_service());
	interprocess_server_->start(aliveAlarmPort, SHARED_OBJ_FROM_THIS);
	start_cmd_accptor(param_.http_cmd_port_);
}

void control_base::__start()
{
}

void control_base::start_cmd_accptor(int& port)
{
	if (!http_acceptor_)
		http_acceptor_ = http_acceptor::create(get_io_service());

	if (!port)
		port = random((int)PORT_BEGIN, (int)PORT_END);
	int org_port = port;
	error_code ec;
	do{
		ec.clear();
		endpoint localEdp(asio::ip::address(), port);
		http_acceptor_->open(localEdp, ec);
		http_acceptor_->set_option(asio::socket_base::linger(true, 1), ec);
		http_acceptor_->listen(ec);
		if (ec)
		{
			http_acceptor_->close();
			port = random((int)PORT_BEGIN, (int)PORT_END);
		}
		else
		{
			param_.http_cmd_port_ = (unsigned short)port;
			http_acceptor_->register_accepted_handler(boost::bind(&this_type::on_accepted, this, _1, _2));
			http_acceptor_->keep_async_accepting();
		}

	} while (ec);
	BOOST_ASSERT(port > 0);
	if (org_port != port)
	{
		std::string errorMsg;
		__set_operation_http_port(port, errorMsg);
	}
}

void control_base::start_sub_process_check_timer()
{
	if (sub_process_check_timer_)
		return;

	sub_process_check_timer_ = timer::create(get_io_service());
	//sub_process_check_timer_->set_obj_desc("p2control::control_base::sub_process_check_timer_");
	sub_process_check_timer_->register_time_handler(
		boost::bind(&this_type::on_sub_process_check_timer, this));

	sub_process_check_timer_->async_keep_waiting(seconds(1), seconds(5));
}

void control_base::start_wild_check_timer()
{
	if (wild_sub_process_check_timer_)
		return;

	wild_sub_process_check_timer_ = timer::create(get_io_service());
	//wild_sub_process_check_timer_->set_obj_desc("p2control::control_base::wild_sub_process_check_timer_");
	wild_sub_process_check_timer_->register_time_handler(
		boost::bind(&this_type::on_wild_sub_process_timer, this));

	wild_sub_process_check_timer_->async_keep_waiting(seconds(1), seconds(21));
}

void control_base::start_time_out_timer()
{
	if (time_out_timer)
		return;

	time_out_timer = timer::create(get_io_service());
	//time_out_timer->set_obj_desc("control_base::time_out_timer");
	time_out_timer->register_time_handler(
		boost::bind(&this_type::on_request_time_out, this));
	time_out_timer->async_keep_waiting(seconds(30), seconds(5));
}

void control_base::stop_cmd_accptor()
{
	if (http_acceptor_)
	{
		http_acceptor_->close();
		http_acceptor_.reset();
	}
}

void control_base::stop_sub_process_check_timer()
{
	if (wild_sub_process_check_timer_)
	{
		wild_sub_process_check_timer_->cancel();
		wild_sub_process_check_timer_.reset();
	}
}

void control_base::stop_wild_check_timer()
{
	if (sub_process_check_timer_)
	{
		sub_process_check_timer_->cancel();
		sub_process_check_timer_.reset();
	}
}

void control_base::stop_time_out_timer()
{
	if (time_out_timer)
	{
		time_out_timer->cancel();
		time_out_timer.reset();
	}
}

void control_base::handle_sentout_and_close(http_connection_sptr sock)
{
	sock->unregister_all_dispatch_handler();//��������䣬������ѭ�����õ�����Դй©��
	sock->close();
	cmd_sockets_.erase(sock);
}

void control_base::on_accepted(http_connection_sptr conn, const error_code& ec)
{
	if (ec)
	{
		LogError("accepted error:%s", ec.message());
		return;
	}

#ifndef P2ENGINE_DEBUG
	//Ϊ�˰�ȫ����Ӫϵͳcontrol_base��������������
	bool is_local = false;
	error_code e;

	//������Ҫ��Ϊ���ų�������ַ���ǹ�����ַ�����
	std::vector<ip_interface> ipInterface = enum_net_interfaces(get_io_service(), e);
	for (BOOST_AUTO(itr, ipInterface.begin());
		itr!=ipInterface.end(); ++itr)
	{
		if(conn->remote_endpoint(e).address()==(*itr).interface_address)
		{
			is_local = true;
			break;
		}
	}

	if (!is_local&&is_global(conn->remote_endpoint(e).address()))
	{
		http::response rep;
		rep.status(http::header::HTTP_OK);
		std::string errorMsg="<html><head><title>Error Message</title>"
			"<style >body{font:12px arial;text-align:left;background:#fff}</style>"
			"</head><body><h1>Forbidden:403</h1><h3>Message:</h3>"
			"<p>Only request from local address is allowed.</p><p>"+asc_time_string()+"</p></body></html>";
		rep.content_length(errorMsg.size());
		rep.set("app_status", boost::lexical_cast<std::string>(http::header::HTTP_FORBIDDEN));

		safe_buffer buf;
		safe_buffer_io bio(&buf);
		bio<<rep;
		bio<<errorMsg;

		conn->async_send(buf);
		conn->register_writable_handler(
			boost::bind(&this_type::handle_sentout_and_close, this, conn)
			);

		std::cout<<errorMsg<<std::endl;
		return;
	}
#endif

	conn->register_request_handler(boost::bind(&this_type::on_request, this, _1, conn.get()));
	cmd_sockets_.try_keep(conn, seconds(5));//5s��û������ر�����
}

void control_base::on_request(const http::request& req, http_connection* sock)
{
	std::cout << "on_request: " << req.url() << std::endl;
	http_connection_sptr conn = sock->shared_obj_from_this<http_connection>();
	try{
		error_code ec;
		uri u(req.url(), ec);

		http::header::status_type state = http::header::HTTP_OK;
		std::string errorMsg = "request invalid";

		//����session_id
		std::string stamp_t = boost::lexical_cast<std::string>((int)timestamp_now());
		std::string session_id = string_to_hex(md5(stamp_t));
		req_session reqSess;
		reqSess.qmap = u.query_map();
		reqSess.session_id = session_id;

		int type = get_int_type(reqSess.qmap, "service_type");
		std::string cmd = get(reqSess.qmap, "type");
		std::string link = get(reqSess.qmap, "link");

		sessions_.insert(std::make_pair(session_id, session(type, conn, link)));

		if (!on_request_handler(reqSess, errorMsg))
			state = http::header::HTTP_NOT_FOUND;

		response_cmd_exec_state(conn, state, reqSess, errorMsg);
	}
	catch (const std::exception& e)
	{
		req_session reqSess;
		response_cmd_exec_state(conn, http::header::HTTP_BAD_REQUEST, reqSess, e.what());
	}
	catch (...)
	{
		req_session reqSess;
		response_cmd_exec_state(conn, http::header::HTTP_BAD_REQUEST, reqSess, "unknow exception while parser request url!");
	}
}

void control_base::response_cmd_exec_state(http_connection_sptr conn,
	http::header::status_type state, const req_session& req, const std::string& errorMsg)
{
	if (state != http::header::HTTP_OK)//�д������������
	{
		std::string htmlMessage = std::string(
			"<html><head><title>Error</title>"
			"<style >body{font:12px arial;text-align:left;background:#fff}</style>"
			"</head><body><h1>Bad Request:400</h1><h3>Message:</h3><p>")
			+ errorMsg + std::string("</p><p>") + asc_time_string() + std::string("</p></body></html>");

		http::response rep;
		rep.status(http::header::HTTP_OK);
		rep.content_length(htmlMessage.size());
		rep.set("app_status", boost::lexical_cast<std::string>(state));

		safe_buffer buf;
		safe_buffer_io bio(&buf);
		bio << rep;
		bio << htmlMessage;

		conn->async_send(buf);
		conn->register_writable_handler(
			boost::bind(&this_type::handle_sentout_and_close, this, conn));//connΪshared_ptr

		std::cout << htmlMessage << std::endl;

		return;
	}

	//������ʱ���������ʱ��û�лظ������, ����timeout
	start_time_out_timer(); //��ʱ
}

void control_base::on_request_time_out()
{
	timestamp_t now = timestamp_now();
	for (BOOST_AUTO(itr, sessions_.begin());
		itr != sessions_.end(); ++itr)
	{
		session& sess = const_cast<session&>(itr->second);
		if (is_time_passed(0, sess.expire_time, now))
		{
			on_session_time_out(itr->first);
		}
	}
}

void control_base::on_session_time_out(const std::string& sessionID)
{
	BOOST_AUTO(itr, sessions_.find(sessionID));
	if (itr == sessions_.end())
	{
		std::cout << "Internal request!" << std::endl;
		return; //�ظ�����
	}

	http_connection_sptr conn = itr->second.conn;
	int failedCnt = itr->second.expected_reply_cnt;
	sessions_.erase(itr);

	http::response rep;
	rep.status(http::header::HTTP_OK);

	std::string errorMsg = "<html><head><title>Error Message</title>"
		"<style >body{font:12px arial;text-align:left;background:#fff}</style>"
		"</head><body><h1>Time Out:408</h1><h3>Message:</h3>"
		"<p>http request time out, " + boost::lexical_cast<std::string>(failedCnt)
		+" client have no response.</p><p>" + asc_time_string() + "</p></body></html>";

	rep.content_length(errorMsg.size());
	rep.set("app_status", boost::lexical_cast<std::string>(http::header::HTTP_REQUEST_TIMEOUT));

	safe_buffer buf;
	safe_buffer_io bio(&buf);
	bio << rep;
	bio << errorMsg;

	conn->async_send(buf);
	conn->register_writable_handler(boost::bind(&this_type::handle_sentout_and_close, this, conn));

	std::cout << errorMsg << std::endl;
}

void control_base::try_kill_wild_sub_process(pid_t pid)
{
	if (wild_sub_process_set_.end() != wild_sub_process_set_.find(pid))
	{
		kill_process_by_id(pid);
		wild_sub_process_set_.erase(pid);
	}
	else
	{
		wild_sub_process_set_.try_keep(pid, seconds(KEEP_WILD_INTERVAL));
	}
}

void control_base::on_recvd_cmd_reply(const safe_buffer& buf, const std::string& data)
{
	c2s_cmd_reply_msg msg;
	if (!parser(buf, msg))
		return;

	if (msg.has_msg())
		on_recvd_error_message(msg.msg());

	reply_http_request(msg, data);
}

void control_base::reply_http_request(const c2s_cmd_reply_msg& msg, const std::string& data)
{
	//����session_id�ҵ�conn
	BOOST_AUTO(itr, sessions_.find(msg.session_id()));
	if (itr == sessions_.end())
	{
		DEBUG_SCOPE(
			std::cout << msg.msg() << std::endl;
		);
		return;
	}

	std::string replyMSG;
	int failCnt = 0;
	if (0 != msg.code())
	{
		replyMSG.append(msg.msg());
		replyMSG.append("\n");
		++failCnt;
	}
	if (!data.empty())
		replyMSG = data;

	session& sess = const_cast<session&>(itr->second);
	if (--sess.expected_reply_cnt != 0)
		return;

	http_connection_sptr conn = sess.conn;
	sessions_.erase(msg.session_id());

	stop_time_out_timer();
	http::response rep;
	rep.status(http::header::HTTP_OK);
	http::header::status_type Status = http::header::HTTP_OK;
	if (0 != failCnt)
		Status = http::header::HTTP_NOT_ACCEPTABLE;

	rep.content_length(replyMSG.length());
	rep.set("app_status", boost::lexical_cast<std::string>(Status));

	safe_buffer sendBuf;
	safe_buffer_io bio(&sendBuf);
	bio << rep << replyMSG;

	conn->async_send(sendBuf);
	conn->register_writable_handler(
		boost::bind(&this_type::handle_sentout_and_close, this, conn));//conn��ҪΪshared_ptr

	std::cout << buffer_cast<char*>(sendBuf) << std::endl;
}


void control_base::on_recvd_auth_message(const safe_buffer& buf)
{
	c2s_auth_msg msg;
	if (!parser(buf, msg))
		return;

	if (!msg.message().empty())
		param_.auth_message_ = msg.message();
}

//////////////////////////////////////////////////////////////////////////
template<typename SocketType>
struct is_port_usable_impl
{
	bool operator()(io_service& ios, const std::string& url, error_code& ec)const
	{
		uri u(url, ec);
		if (ec)
			return false;
		if (u.port())
		{
			typename SocketType::socket Socket(ios);
			std::string ip = u.host();
			if (ip.empty())
				ip = "0.0.0.0";
			SocketType::endpoint local_edp(asio::ip::address::from_string(ip, ec),
				u.port()
				);

			if (is_multicast(local_edp.address()))
				local_edp.address(asio::ip::address());

			Socket.open(local_edp.protocol(), ec);
			if (ec)
				return false;

			asio::socket_base::reuse_address reuse_address_option(false);
			Socket.set_option(reuse_address_option, ec);
			ec.clear();
			Socket.bind(local_edp, ec);
			error_code er;
			Socket.close(er);
			return !ec;
		}
		else
			return true;
	}
};

bool is_port_usable::operator()(io_service& ios,
	const std::string& url, error_code& ec)const
{
	uri u;
	u.from_string(url, ec);
	if (ec)
		return false;

	if (boost::iequals(u.protocol(), "udp"))
	{
		return is_port_usable_impl<udp>()(ios, url, ec);
	}
	else if (boost::iequals(u.protocol(), "shunt"))
	{
		return is_port_usable_impl<udp>()(ios, url, ec)
			&& is_port_usable_impl<tcp>()(ios, url, ec);
	}
	else
	{
		return is_port_usable_impl<tcp>()(ios, url, ec);
	}
}

const std::string& get(const config_map_type& req, const std::string& k)
{
	BOOST_AUTO(itr, req.find(k));
	if (itr != req.end())
		return itr->second;

	const static std::string NULL_STRING;
	return NULL_STRING;
}

int get_int_type(const config_map_type& req, const std::string& k)
{
	BOOST_AUTO(itr, req.find(k));
	if (itr != req.end())
	{
		try
		{
			return boost::lexical_cast<int>(itr->second);
		}
		catch (...)
		{
		}
	}
	return -1;
}


NAMESPACE_END(p2control);
