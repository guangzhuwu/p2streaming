#include "p2s_mds/media_relay.h"
#include <p2engine/push_warning_option.hpp>
#include <boost/lexical_cast.hpp>
#include <p2engine/pop_warning_option.hpp>

media_relay::media_relay(io_service& ios, int listenPort)
	:basic_engine_object(ios)
{
	tcp::endpoint localEdp(address(), listenPort);

	boost::system::error_code ec;
	acceptor_=http_acceptor::create(get_io_service());
	acceptor_->listen(localEdp, ec);
	acceptor_->keep_async_accepting();
	if (ec)
		acceptor_->close();
	acceptor_->register_accepted_handler(boost::bind(&this_type::on_accept, this, _1, _2));
}

media_relay::~media_relay()
{
	boost::system::error_code ec;
	acceptor_->close(ec);
	std::map<connection_type*, connection_sptr>::iterator itr=http_connections_.begin();
	for(;itr!=http_connections_.end();++itr)
	{
		itr->second->close();
	}
}

void media_relay::handle_media(const  safe_buffer& buf)
{
	if (http_connections_.empty())
		return;
	std::map<connection_type*, connection_sptr>::iterator itr=http_connections_.begin();
	for(;itr!=http_connections_.end();++itr)
	{
		itr->second->async_send(buf);
	}
}

void media_relay::on_accept(connection_sptr conn, error_code ec)
{
	http_connections_keeper_.try_keep(conn, seconds(5));
	conn->register_request_handler(boost::bind(&this_type::on_request, this, _1, conn.get()));
}

void media_relay::on_request(const http::request& req, connection_type* sock)
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

	std::string max_len=boost::lexical_cast<std::string>((std::numeric_limits<boost::int64_t>::max)());
	std::string max_len_1=boost::lexical_cast<std::string>((std::numeric_limits<boost::int64_t>::max)()-1);

	safe_buffer buf;
	safe_buffer_io bio(&buf);
	http::response res;
	if (req.method()==http::HTTP_METHORD_GET)
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

		bio<<res;
		
		http_connections_[conn.get()]=conn;
		conn->register_disconnected_handler(boost::bind(&this_type::on_disconnected, this, conn.get()));
		conn->register_data_handler(boost::bind(&this_type::on_data, this, _1));
	}
	else if(req.method()==http::HTTP_METHORD_OPTIONS)
	{
		res.status(http::header::HTTP_OK);
		res.content_length(0);
		res.set(http::HTTP_ATOM_Allow, http::HTTP_METHORD_GET);

		bio<<res;

		conn->register_data_handler(boost::bind(&this_type::on_data, this, _1));
		//when sendout ��close
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

		bio<<res;
		bio.write(resp_xml.c_str(), resp_xml.length());

		conn->register_data_handler(boost::bind(&this_type::on_data, this, _1));
		//when sendout ��close
		conn->register_writable_handler(boost::bind(&close_connection::close, conn));
	}
	conn->async_send(buf);
}

void media_relay::on_disconnected(connection_type* conn)
{
	http_connections_.erase(conn);
}

void media_relay::on_data(safe_buffer buf)
{
	//std::string s(p2engine::buffer_cast<char*>(buf), buf.length());
	//std::cout<<s<<std::endl;
}

