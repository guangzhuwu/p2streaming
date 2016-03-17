#include "p2s_ppc/p2p_processor.hpp"
#include "p2s_ppc/url_crack_processor.h"
#include "p2s_ppc/server.hpp"
#include "p2s_ppc/channel_req_processor.h"
#include "p2s_ppc/viewing_state_processor.h"
#ifdef PROFILER
#include "android-ndk-profiler-3.1/prof.h"
#endif

#include "natpunch/auto_mapping.h"
#include "client/cache/cache_service.h"
#include "client/nat.h"

#include <p2engine/push_warning_option.hpp>
#include <boost/format.hpp>
#include <p2engine/pop_warning_option.hpp>

std::string g_xor_key;


//#define  DEBUG_CRASH 	 system_time::sleep_millisec(1000); std::cout<<" ------ "<<__LINE__<<std::endl;

namespace
{
	const unsigned short DEFAULT_PORT = 9906;
}

NAMESPACE_BEGIN(ppc);

extern std::string g_orig_mac_id;
static std::string g_operators("P2S");

void set_operators(std::string& op)
{
	g_operators = op;
}

p2sppc_server::p2sppc_server(io_service& ios, int port, const std::string& pas_host,
	const std::string& ext_pas_host_list_string)
	: basic_engine_object(ios)
	, port_(port ? port : DEFAULT_PORT)
	, cache_dir_("./")
	, cache_file_size_(1024 * 1024 * 1024)
	, key_pair_(security_policy::generate_key_pair())
	, pas_host_(pas_host)
	, ext_pas_host_list_()
{

#ifdef PROFILER
	monstartup("p2s_ppc");
	setenv("CPUPROFILE", "/sdcard/gmon.out", 1);
#endif

	boost::split(ext_pas_host_list_, ext_pas_host_list_string, 
		boost::is_any_of(", "), boost::algorithm::token_compress_on);

}

p2sppc_server::~p2sppc_server()
{
	if (acceptor_)
	{
		acceptor_->close();
		acceptor_.reset();
	}
}

void p2sppc_server::run()
{
	if (acceptor_)
		return;
	start_acceptor();
	start_detectors();
	start_cache_server();
#ifndef POOR_CPU
	start_pana();
#endif

	prepare_processors();
}

void p2sppc_server::report_viewing_info(opt_type opt, const std::string& type,
	const std::string& link, const std::string& channel_name, const std::string& op)
{
#ifndef POOR_CPU
	start_pana();
	BOOST_FOREACH(boost::shared_ptr<p2client::pa_handler>& paHandler, pa_handler_list_)
	{
		paHandler->report_viewing_info(opt, type, link, channel_name, op);
	}
#endif
}

void p2sppc_server::set_cache_param(const std::string& dir, size_t file_size)
{
	cache_dir_ = dir;
	cache_file_size_ = file_size;
}

void p2sppc_server::start_acceptor()
{
	io_service& ios = get_io_service();

	error_code ec;
	tcp::endpoint localEdp(address(), port_);
	acceptor_ = http_acceptor::create(ios);
	acceptor_->open(localEdp, ec);
	if (!ec)
	{
		acceptor_->set_option(asio::socket_base::linger(true, 0), ec);
		acceptor_->set_option(asio::socket_base::reuse_address(true), ec);
		ec.clear();
		acceptor_->listen(ec);
	}
	if (ec)
	{
		std::cout <<"ppc start_acceptor listen("<< localEdp <<") error:"
			<< ec.message() <<". WE WILL EXIT!"
			<< std::endl;
		acceptor_->close();
		system_time::sleep_millisec(500);
		exit(0);
	}
	std::cout << localEdp << std::endl;

	acceptor_->keep_async_accepting();
	acceptor_->register_accepted_handler(boost::bind(&this_type::on_accept, this, _1));
}

void p2sppc_server::start_pana()
{
#ifndef POOR_CPU

	std::cout << "start_pana" << std::endl;

	if (!pa_handler_list_.empty())
	{
		return;
	}

	//�������Ӽ��
	boost::shared_ptr<p2client::pa_handler> paHandler = p2client::pa_handler::create(get_io_service(), pas_host_);
	paHandler->set_operators(g_operators);
	paHandler->set_mac(g_orig_mac_id);
	pa_handler_list_.push_back(paHandler);

	BOOST_FOREACH(std::string& pas_host, ext_pas_host_list_)
	{
		if (pas_host.empty())
			continue;

		paHandler = p2client::pa_handler::create(get_io_service(), pas_host);
		paHandler->set_operators(g_operators);
		paHandler->set_mac(g_orig_mac_id);
		pa_handler_list_.push_back(paHandler);
	}
#endif
}

void p2sppc_server::start_detectors()
{
	//�����������д�������Լ�nat̽��natmaping
	natpunch::start_auto_mapping(get_io_service());
	get_local_nat_type();
	get_upload_capacity();
}

void p2sppc_server::prepare_processors()
{
	processors_.push_back(channel_req_processor::create(get_io_service()));
	processors_.push_back(p2p_processor::create(SHARED_OBJ_FROM_THIS));
	processors_.push_back(url_crack_processor::create(SHARED_OBJ_FROM_THIS));
#ifndef POOR_CPU
	processors_.push_back(viewing_state_processor::create(SHARED_OBJ_FROM_THIS));
#endif
}

void p2sppc_server::start_cache_server()
{
	client_param_base param;
	param.tracker_host = TRACKER_SERVER_HOST + ":8080";
	param.public_key = key_pair_.first;
	param.private_key = key_pair_.second;
	param.type = VOD_TYPE;
	param.cache_directory = cache_dir_;
	param.channel_link = param.channel_uuid = "GLOBAL_TRACKER_CHANNEL";

	init_cache_service(get_io_service(), create_client_param_sptr(param), cache_file_size_);
}

void p2sppc_server::on_accept(connection_sptr conn)
{
	error_code ec;
	std::cout << "on_accept new request from " << conn->remote_endpoint(ec) << "\n";
	/*if (!is_loopback(conn->remote_endpoint(ec).address())
	&&!is_local(conn->remote_endpoint(ec).address())
	)
	{
	conn->close();
	return;
	}*/

	struct dummy_data_processer
	{
		static void on_data(const safe_buffer&){}
	};
	conn->set_option(asio::socket_base::linger(false, 1), ec);
	conn->register_request_handler(boost::bind(&this_type::on_request, this, _1, conn.get()));
	conn->register_data_handler(boost::bind(&dummy_data_processer::on_data, _1));

	conn_keeper_.try_keep(conn, seconds(10));
}

void p2sppc_server::on_request(const http::request& req, connection_type* conn)
{
	if (!conn->is_open())
		return;

	std::cout << req;

	connection_sptr sock = conn->shared_obj_from_this<connection_type>();
	error_code ec;
	std::ostringstream urlStrm;
	if (req.host().empty())
		urlStrm << "http://" << acceptor_->local_endpoint(ec) << req.url();
	else
		urlStrm << "http://" << req.host() << req.url();
	uri u(urlStrm.str(), ec);
	if (ec)
	{
		std::cout << "req.url()error?? " << (urlStrm.str() + req.url()) << std::endl;
		close_connection(sock, http::response::HTTP_BAD_REQUEST);
		return;
	}
	if (process(u, req, sock))
		return;
	for (size_t i = 0; i<processors_.size(); ++i)
	{
		if (processors_[i]->process(u, req, sock))
			return;
	}
#ifdef PROFILER
	moncleanup();
#endif

	close_connection(sock, http::response::HTTP_BAD_REQUEST);
}

bool p2sppc_server::process(const uri& u, const http::request& req,
	const connection_sptr& sock)
{
	//�Ƿ���cmd
	std::map<std::string, std::string> qmap = u.query_map();
	const std::string& cmd = qmap["cmd"];
	if (cmd == "get_challenge")
	{
		std::string pubkey = string_to_hex(key_pair_.first);

		http::response res;
		res.status(http::header::HTTP_OK);
		res.content_length(pubkey.length());

		safe_buffer buf;
		safe_buffer_io bio(&buf);
		bio << res << pubkey;

		sock->async_send(buf);
		sock->register_writable_handler(boost::bind(&p2sppc_server::handle_sentout_and_close, sock));

		return true;
	}
	return false;
}

/*
void p2sppc_server::process_cmd(std::map<std::string, std::string> qmap,
connection_sptr sock)
{

else if (cmd == "url_crack")
{
using urlcrack::crack_adapter;
std::string url = qmap["uri"];
url = hex_to_string(url);
url_crack_sptr_ = crack_adapter::create(url, get_io_service());
url_crack_sptr_->get_crack_urls(
boost::bind(&this_type::write_urls, this, _1, sock));
}
}
*/

void p2sppc_server::close_connection(connection_sptr sock, int replyCode,
	const std::string& content)
{
	if (!sock)
	{
		return;
	}

	if (replyCode>0)
	{
		http::response res;
		res.status((http::response::status_type)replyCode);
		res.content_length(content.length());

		safe_buffer buf;
		safe_buffer_io bio(&buf);
		bio << res;

		sock->async_send(buf);
		sock->register_writable_handler(boost::bind(&this_type::handle_sentout_and_close, sock));
		//�رգ���
	}
	else
	{
		sock->close();
	}
}

void p2sppc_server::close_connection(connection_sptr sock, const safe_buffer& buf)
{
	if (!sock)
	{
		return;
	}

	sock->async_send(buf);
	sock->register_writable_handler(boost::bind(&this_type::handle_sentout_and_close, sock));
}

void p2sppc_server::handle_sentout_and_close(connection_sptr sock)
{
	if (!sock)
	{
		return;
	}
	sock->unregister_all_dispatch_handler();//��������䣬������Դй©
	sock->close(true);
}


NAMESPACE_END(ppc);
