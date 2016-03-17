#ifndef _SHUNT_FLUID_SENDER_H__
#define _SHUNT_FLUID_SENDER_H__

#include "shunt/sender.h"

namespace p2shunt{
	//udp分流器
	class udp_fluid_sender
		: public sender
	{
		typedef udp_fluid_sender this_type;
		SHARED_ACCESS_DECLARE;

	public:
		static shared_ptr create(io_service& ios)
		{
			return shared_ptr(new this_type(ios), 
				shared_access_destroy<this_type>());
		}
		virtual void shunt(const safe_buffer& buf);
		virtual bool updata(const std::string& edp, error_code& ec);

	protected:
		udp_fluid_sender(io_service& ios);
		void handle_send(error_code ec, size_t len);

	protected:
		asio::ip::udp::socket socket_;
		std::set<asio::ip::udp::endpoint>dest_endpoints_;
		std::list<safe_buffer> pkt_list_;
		int send_cnt_;
	};

	//http分流器
	class http_fluid_sender
		:public sender
	{
		typedef http_fluid_sender this_type;
		SHARED_ACCESS_DECLARE;

		typedef asio::ip::tcp tcp;
		typedef http::basic_http_connection<http::http_connection_base> connection_type;
		typedef http::basic_http_acceptor<connection_type, connection_type>	http_acceptor;
		typedef boost::shared_ptr<connection_type> connection_sptr;

	protected:
		http_fluid_sender(io_service& ios);
		virtual ~http_fluid_sender();

	public:
		static shared_ptr create(io_service& ios)
		{
			return shared_ptr(new this_type(ios), 
				shared_access_destroy<this_type>());
		}

		virtual bool updata(const std::string& edp, error_code& ec);
		virtual void shunt(const safe_buffer& buf);

	protected:
		/// Perform work associated with the server.
		void on_accept(connection_sptr conn, error_code ec);

		void on_request(const http::request& req, connection_type* conn);

		void on_disconnected(connection_type* conn);

		void on_data(safe_buffer);
	private:
		boost::shared_ptr<http_acceptor> acceptor_;
		std::string path_;
		std::set<connection_sptr>connections_;
		timed_keeper_set<connection_sptr> connections_keeper_;
	};

}

#endif // _SHUNT_FLUID_SENDER_H__


