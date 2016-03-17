#ifndef cms_acceptor_h__
#define cms_acceptor_h__

#include <p2engine/push_warning_option.hpp>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <p2engine/pop_warning_option.hpp>

#include "p2s_ppc/typedef.h"

class cms_service
	: public basic_engine_object
	, public fssignal::trackable
{
	typedef cms_service this_type;
	SHARED_ACCESS_DECLARE;

	typedef http_acceptor  connection_type;
	typedef boost::shared_ptr<connection_type> connection_sptr;
public:
	cms_service(io_service& ios)
		:basic_engine_object(ios)
	{
		acceptor_=http_acceptor::create(ios);
		acceptor_->accepted_signal().bind(&this_type::on_accepted, this, _1);

		error_code ec;
		http_acceptor::endpoint_type edp(address(), 20100);
		acceptor_->listen(edp, ec);
	}

protected:
	void on_accepted(connection_sptr conn)
	{
		conns_.insert(conn);

		conn->received_request_header_signal().bind(
			&this_type::on_recvd_request_header, this, _1, conn.get()
			);
		conn->received_data_signal().bind(
			&this_type::on_recvd_data, this, _1, conn.get()
			);
		conn->disconnected_signal().bind(
			&this_type::on_disconnected, this, _1, conn.get()
			);
	}

protected:
	void on_recvd_request_header(const http::request& h, connection_type* c)
	{
		error_code ec;
		uri u(h.url(), ec);
		if (ec)
		{
			c->close();
			return;
		}
		std::string path=u.path();
		std::map<std::string, std::string>qmap=u.query_map();
		if (path=="/add_channel")
		{
			std::string id=qmap["id"];
			std::string inAddr=qmap["in_addr"];
			std::string outAddr=qmap["out_addr"];
			if(id.empty()||addr.empty()||outAddr.empty())
			{
				error_code ec;
				conn->close(ec);
				conns_.erase(conn->shared_obj_from_this<connection_type>());
				return;
			}

		}
		else if(path=="/del_channel")
		{

		}
	}
	void on_recvd_data(const safe_buffer& buf, connection_type*)
	{
	}
	void on_disconnected(error_code ec, connection_type*conn)
	{
		conns_.erase(conn->shared_obj_from_this<connection_type>());
	}

protected:
	boost::shared_ptr<http_acceptor> acceptor_;

	std::set<connection_sptr > conns_;

};

#endif // cms_acceptor_h__
