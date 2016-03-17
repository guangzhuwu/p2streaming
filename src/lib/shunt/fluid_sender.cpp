#include "shunt/fluid_sender.h"

#include <boost/lexical_cast.hpp>
#include <boost/regex.hpp>
#include <boost/asio/ip/multicast.hpp>
#include <boost/asio/ip/unicast.hpp>

namespace p2shunt{
	void dummy_handle_send(const error_code& ec, size_t len){}
	boost::function<void(const error_code&, size_t)>send_handler = boost::bind(&dummy_handle_send, _1, _2);

	udp_fluid_sender::udp_fluid_sender(io_service& ios)
		: sender(ios)
		, socket_(ios)
		, send_cnt_(0)
	{
		error_code ec;
		asio::socket_base::send_buffer_size send_buffer_size_option(2*1024*1024);
		asio::socket_base::non_blocking_io nonblock_command(true);
		asio::ip::multicast::hops multihop(4);
		asio::ip::unicast::hops unihop(4);

		socket_.open(asio::ip::udp::v4(), ec);

		socket_.io_control(nonblock_command, ec);
		socket_.set_option(multihop, ec);
		socket_.set_option(unihop, ec);
		ec.clear();
		socket_.set_option(send_buffer_size_option, ec);
		if (ec)
		{
			asio::socket_base::send_buffer_size send_buffer_size_option(1024*1024);
			socket_.set_option(send_buffer_size_option, ec);
		}
		disable_icmp_unreachable(socket_.native());
	}
	
	void udp_fluid_sender::shunt(const safe_buffer& buf)
	{
		BOOST_AUTO(asioBuf, buf.to_asio_const_buffers_1());
		for (BOOST_AUTO(itr, dest_endpoints_.begin());itr!=dest_endpoints_.end();++itr)
		{
			socket_.async_send_to(asioBuf, *itr, send_handler);
		}
	}

	void udp_fluid_sender::handle_send(error_code ec, size_t len)
	{
		//do nothing
	}

	bool udp_fluid_sender::updata(const std::string& url, error_code& ec)
	{
		uri u(url, ec);
		BOOST_ASSERT(u.protocol()=="udp");
		std::string port=u.port()?boost::lexical_cast<std::string>(u.port()):"0";
		udp::endpoint edp=endpoint_from_string<udp::endpoint>(u.host()+":"+port);
		if (!is_any(edp.address())&&edp.port())
			dest_endpoints_.insert(edp);
		return true;
	}


	//////////////////////////////////////////////////////////////////////////
	http_fluid_sender::http_fluid_sender(io_service& ios)
		:sender(ios)
	{
	}

	http_fluid_sender::~http_fluid_sender()
	{
		error_code ec;
		acceptor_->close(ec);
		std::set<connection_sptr>::iterator itr=connections_.begin();
		for(;itr!=connections_.end();++itr)
		{
			(*itr)->close();
		}
	}

	bool http_fluid_sender::updata(const std::string&url, error_code& ec)
	{
		//http://127.0.0.1:8000[/path]
		uri u(url, ec);
		BOOST_ASSERT(u.protocol()=="http");
		std::string port=u.port()?boost::lexical_cast<std::string>(u.port()):"80";
		tcp::endpoint edp=endpoint_from_string<tcp::endpoint>(u.host()+":"+port);
		if (acceptor_&&acceptor_->is_open()&&(tcp::endpoint)acceptor_->local_endpoint(ec)==edp)
		{
			return true;
		}
		path_=u.path();
		if (acceptor_)
			acceptor_->close(ec);
		acceptor_=http_acceptor::create(get_io_service());
		acceptor_->listen(edp, ec);
		if (ec)
		{
			acceptor_->close();
			acceptor_.reset();
		}
		else
		{
			acceptor_->register_accepted_handler(boost::bind(&this_type::on_accept, this, _1, _2));
			acceptor_->keep_async_accepting();
		}
		return !ec;
	}

	void http_fluid_sender::shunt(const safe_buffer& data)
	{
		if (connections_.empty())
			return;

		std::set<connection_sptr>::iterator itr=connections_.begin();
		for(;itr!=connections_.end();++itr)
		{
			(*itr)->async_send(data);
		}
	}

	void http_fluid_sender::on_accept(connection_sptr conn, error_code ec)
	{
		connections_keeper_.try_keep(conn, seconds(10));
		conn->register_request_handler(boost::bind(&this_type::on_request, this, _1, conn.get()));
	}

	void http_fluid_sender::on_request(const http::request& req, connection_type* sock)
	{
		struct close_connection{
			static void close(connection_sptr conn)
			{
				conn->unregister_all_dispatch_handler();
				conn->close();
			}
		};
		connection_sptr conn=sock->shared_obj_from_this<connection_type>();

		//req.write(std::cout);

		std::string max_len=boost::lexical_cast<std::string>(
			(std::numeric_limits<boost::int64_t>::max)());
		std::string max_len_1=boost::lexical_cast<std::string>(
			(std::numeric_limits<boost::int64_t>::max)()-1);

		http::response res;
		std::string s;
		error_code ec;
		if (uri(req.url(), ec).path()!=path_)
		{
			res.status(http::header::HTTP_NOT_FOUND);

			res.serialize(s);

			conn->register_data_handler(boost::bind(&this_type::on_data, this, _1));
			//when sendout £¬close
			conn->register_writable_handler(boost::bind(&close_connection::close, conn));
		}
		else if (req.method()==http::HTTP_METHORD_GET)
		{
			if (!req.get(http::HTTP_ATOM_Range).empty())
			{
				res.status(http::header::HTTP_PARTIAL_CONTENT);
				res.set("Content-Range", std::string("0-")+max_len_1+"/"+max_len);
			}
			else
			{
				res.status(http::header::HTTP_OK);
			}
			res.content_length((std::numeric_limits<boost::int64_t>::max)());

			res.serialize(s);

			connections_.insert(conn);
			conn->register_disconnected_handler(boost::bind(&this_type::on_disconnected, this, conn.get()));
			conn->register_data_handler(boost::bind(&this_type::on_data, this, _1));
		}
		else if(req.method()==http::HTTP_METHORD_OPTIONS)
		{
			res.status(http::header::HTTP_OK);
			res.content_length(0);
			res.set(http::HTTP_ATOM_Allow, http::HTTP_METHORD_GET);

			res.serialize(s);

			conn->register_data_handler(boost::bind(&this_type::on_data, this, _1));
			//when sendout £¬close
			conn->register_writable_handler(boost::bind(&close_connection::close, conn));
		}
		else
		{
			const static std::string resp_xml="<?xml version=\"1.0\" ?>"
				"<D:multistatus xmlns:D=\"DAV:\">"
				"<D:response>"
				"<D:status>HTTP/1.1 200 OK</D:status>"
				"</D:response>"
				"</D:multistatus>";

			res.status(http::header::HTTP_OK);
			res.content_length(resp_xml.length());
			res.set(http::HTTP_ATOM_Allow, http::HTTP_METHORD_GET);

			res.serialize(s);
			s+=resp_xml;

			conn->register_data_handler(boost::bind(&this_type::on_data, this, _1));
			//when sendout £¬close
			conn->register_writable_handler(boost::bind(&close_connection::close, conn));
		}

		safe_buffer buf;
		safe_buffer_io bio(&buf);
		bio.write(s.c_str(), s.length());
		conn->async_send(buf);
	}

	void http_fluid_sender::on_disconnected(connection_type* conn)
	{
		BOOST_ASSERT(conn);
		connections_.erase(conn->shared_obj_from_this<connection_type>());
	}

	void http_fluid_sender::on_data(safe_buffer buf)
	{
		//std::string s(p2engine::buffer_cast<char*>(buf), buf.length());
		//std::cout<<s<<std::endl;
	}
}
