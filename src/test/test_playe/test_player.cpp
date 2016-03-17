#include <p2engine/p2engine.hpp>
#include <p2engine/push_warning_option.hpp>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <p2engine/pop_warning_option.hpp>
#include "p2engine/http/header.hpp"
#include "common/common.h"

using namespace p2engine;


typedef http::basic_http_connection<http::http_connection_base> http_connection;
typedef http::basic_http_acceptor<http_connection,http_connection>	http_acceptor;

class player
	: public fssignal::trackable
{
	typedef  player this_type;
	SHARED_ACCESS_DECLARE;

public:
	static boost::shared_ptr<this_type> create(io_service& ios)
	{
		return boost::shared_ptr<this_type>(
			new this_type(ios), shared_access_destroy<this_type>()
			);
	}
public:
	void start(const endpoint& edp)
	{
		if(re_connect_timer_)
			re_connect_timer_->cancel();

		if (socket_)
			socket_->close();

		if(!socket_)
		{
			socket_=http_connection::create(ios_,true);

			socket_->connected_signal().bind(
			&this_type::on_connected, this, _1
			);
		}
		socket_->async_connect(edp);
		
		if(!re_connect_timer_)
		{
		    re_connect_timer_ = rough_timer::create(ios_);
			re_connect_timer_->time_signal()
				.bind(&this_type::start, this, edp);
		}
		
	}
protected:
	void on_connected(const error_code& ec)
	{
		if(ec)
		{
			std::cout<<ec.message()<<std::endl;
			return;
		}
		std::cout<<"connected ppc succeed!"<<std::endl;
		socket_->disconnected_signal().bind(&this_type::on_disconnected,this,_1);

		if(re_connect_timer_)
			re_connect_timer_->cancel();

		std::string tracker = "/p2p-live/127.0.0.1:9082/default_channel_key/default_channel_uuid.ts";
		std::string host = "127.0.0.1:9906";

		http::request req;		
		req.url(tracker);
		req.host(host);

		safe_buffer buf;
		safe_buffer_io bio(&buf);
		bio<<req;

		socket_->async_send(buf);
	}
	void on_disconnected(const error_code& ec)
	{
		std::cout<<"on_disconnected\n";
		/*if(re_connect_timer_)
			re_connect_timer_->cancel();

		re_connect_timer_->async_wait(seconds(1));*/
	}
protected:
	player(io_service& ios)
		: ios_(ios)
	{

	}
	~player(){}

protected:
	boost::shared_ptr<http_connection> socket_;
	boost::shared_ptr<rough_timer>     re_connect_timer_;
	io_service& ios_;
};

boost::condition g_close_condition;
boost::mutex g_close_mutex;
int main(int argc, char* argv[])
{

	boost::mutex::scoped_lock lock(g_close_mutex);

	io_service ios;
	boost::shared_ptr<player> player_sptr = player::create(ios);

	std::string ipport = "127.0.0.1:9906";
	player_sptr->start(endpoint_from_string<endpoint>(ipport));
	
	ios.run();

	g_close_condition.wait(lock);

	return 0;
}
