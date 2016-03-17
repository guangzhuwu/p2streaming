#ifndef _MDS_MEDIA_SENDER_H__
#define _MDS_MEDIA_SENDER_H__

#include <p2engine/p2engine.hpp>
#include <p2engine/http/http.hpp>

using namespace p2engine;

class media_relay
	:public p2engine::basic_engine_object
{
	typedef media_relay this_type;
	SHARED_ACCESS_DECLARE;

	typedef boost::asio::ip::tcp tcp;
	typedef http::basic_http_connection<http::http_connection_base> connection_type;
	typedef http::basic_http_acceptor<connection_type, connection_type>	http_acceptor;
	typedef boost::shared_ptr<connection_type> connection_sptr;

protected:
	media_relay(io_service& ios, int listenPort);
	virtual ~media_relay();

public:
	static shared_ptr create(io_service& ios, int listenPort)
	{
		return shared_ptr(new this_type(ios, listenPort), 
			shared_access_destroy<this_type>());
	}

	void handle_media(const safe_buffer& buf);

protected:
	/// Perform work associated with the server.
	void on_accept(connection_sptr conn, error_code ec);

	void on_request(const http::request& req, connection_type* conn);

	void on_disconnected(connection_type* conn);

	void on_data(safe_buffer);
private:
	boost::shared_ptr<http_acceptor> acceptor_;
	std::map<connection_type*, connection_sptr>http_connections_;
	timed_keeper_set<connection_sptr> http_connections_keeper_;
};

#endif // _MDS_MEDIA_SENDER_H__
