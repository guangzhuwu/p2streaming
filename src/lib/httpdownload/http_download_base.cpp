#include "httpdownload/http_download_base.hpp"

#include <p2engine/push_warning_option.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <p2engine/pop_warning_option.hpp>

namespace http_download{
	const int http_download_base::elapse_reconnect = 1;
	const int http_download_base::connect_expire_time = 20;
	const int http_download_base::local_router_set::router_keep_time_hour = 24;
	http_download_base::local_router_set http_download_base::local_router_set_;

	http_download_base::http_download_base(io_service& ios)
		: basic_engine_object(ios)
		, status_(0)
		, content_length_(-1)
		, recvd_len_(0)
		, start_pos_(0)
		, ini_start_pos_(0)
		, bstart_(false)
	{
		bredirect_ = false;
		referurl_ = "";
		b_downloal_all = true;

		reconnect_timer_ = rough_timer::create(get_io_service());
		reconnect_timer_->set_obj_desc("httpdownload::http_download::reconnect_timer_");
		reconnect_timer_->register_time_handler(boost::bind(&this_type::__connect, this));

		connect_expire_timer_ = rough_timer::create(get_io_service());
		reconnect_timer_->set_obj_desc("httpdownload::http_download::reconnect_timer_");
		connect_expire_timer_->register_time_handler(boost::bind(&this_type::connect_overtime, this));
	}

	http_download_base::~http_download_base(){
		stop();
	}

	void http_download_base::connect_overtime()
	{
		local_router_set_.erase_address(uri_.host());
	}

	void http_download_base::start(const std::string& _url)
	{
		BOOST_ASSERT(!bstart_&&"一个对象仅完成一次下载任务");

		bstart_ = true;

		error_code ec;
		uri_.from_string(_url, ec);

		if(ec)
		{
			failed_down();
			std::cout<<"http_download_base::start url is not correct :"<<_url<<std::endl;
			return;
		}

		if(!socket_)
			socket_ = http::http_connection::create(get_io_service());

		socket_->register_connected_handler(
			boost::bind(&this_type::on_connected, this, socket_.get(), _1)
			);

		connect_expire_timer_->async_wait(seconds(connect_expire_time));

		__connect();
	}

	void http_download_base::set_range(int64_t _start, int64_t _end)
	{
		b_downloal_all = false;
		start_pos_ = _start;
		ini_start_pos_ = _start;
		end_pos_ = _end;
		recvd_len_ = 0;
		content_length_ = _end - _start + 1;
		strrange_ = (boost::format("bytes=%d-%d")%_start%_end).str();
	}

	void http_download_base::redirect(const std::string& url)
	{
		bredirect_ = true;
		referurl_ = uri_.to_string(p2engine::uri::all_components);

		error_code ec;
		uri_.from_string(url, ec);

		if(ec)
		{
			failed_down();
			std::cout<<"http_download_base::redirect url is not correct :"<<url<<std::endl;
			return;
		}

		connect_expire_timer_->async_wait(seconds(connect_expire_time));
		
		__connect();
	}

	void http_download_base::stop()
	{
		if(socket_)
		{
			socket_->close();
			socket_.reset();
		}

		if(reconnect_timer_)
		{
			reconnect_timer_->cancel();
		}

		if (connect_expire_timer_)
		{
			connect_expire_timer_->cancel();
		}
	}

	void http_download_base::on_connected(http::http_connection* conn, const error_code& ec)
	{
		BOOST_ASSERT(conn == socket_.get());

		if(ec)
		{
			std::cout<<ec.message()<<std::endl;
			reconnect_timer_->async_wait(seconds(elapse_reconnect));
			return;
		}

		if(reconnect_timer_){
			reconnect_timer_->cancel();
		}

		if(connect_expire_timer_){
			connect_expire_timer_->cancel();
		}

		socket_->register_disconnected_handler(boost::bind(&this_type::on_disconnected, this, _1));
		socket_->register_response_handler(boost::bind(&this_type::on_header, this, conn, _1));
		socket_->register_data_handler(boost::bind(&this_type::on_data, this, conn, _1));
		__request();

		error_code temerr;
		endpoint enp = socket_->remote_endpoint(temerr);
		local_router_set_.add_address(uri_.host(), enp.address().to_string(temerr));
	}

	void http_download_base::on_disconnected(const error_code& ec)
	{
		if(status_<100 && -1 != content_length_)
		{
			start_pos_ = end_pos_ - content_length_ + 1 + recvd_len_;
			strrange_ = (boost::format("bytes=%d-%d")%start_pos_%end_pos_).str();
			reconnect_timer_->async_wait(seconds(elapse_reconnect));
			connect_expire_timer_->async_wait(seconds(connect_expire_time));
			return;
		}
		stop();
	}

	void http_download_base::on_header(http::http_connection* conn, const http::response& _response)
	{
		BOOST_ASSERT(conn == socket_.get());

		DEBUG_SCOPE(
			std::string strresponse;
		_response.serialize(strresponse);
		std::cout<<strresponse<<std::endl
			);

		if (_response.status() >= http::header::HTTP_MULTIPLE_CHOICES && _response.status() <= http::header::HTTP_TEMPORARY_REDIRECT )
		{
			std::string key_name = "Location";
			if(_response.has(key_name))
			{
				std::string new_url = _response.get(key_name);
				if (new_url.find_last_of('/') == new_url.size() - 1)
				{
					failed_down();
				}	
				redirect(new_url);
			}
			else
			{
				failed_down();
			}

			return;
		}
		else if (_response.status() != http::header::HTTP_OK && _response.status() != http::header::HTTP_PARTIAL_CONTENT)
		{
			failed_down();
			return;
		}

		content_length_ = _response.content_length();

		if (b_downloal_all && 0 == start_pos_)
		{
			if (content_length_ > 0)
			{
				strrange_ = (boost::format("bytes=%d-%d")%start_pos_%content_length_).str();
				end_pos_ = content_length_;
			}
			else
			{
				strrange_ = "";
			}
		}

		if (content_length_ < (end_pos_ - start_pos_) && -1 != content_length_)
		{
			end_pos_ = start_pos_ + content_length_;
			strrange_ = (boost::format("bytes=%d-%d")%start_pos_%end_pos_).str();
			return;
		}

		if (0 == content_length_ )
		{
			failed_down();
		}
	}

	void http_download_base::on_data(http::http_connection* conn, const safe_buffer& buf)
	{
		BOOST_ASSERT(conn == socket_.get());

		recvd_len_ += buffer_size(buf);
		if (-1 != content_length_)
		{
			if (!content_length_)
				status_=100;
			else
				status_ = static_cast<int>(recvd_len_ * 100 / content_length_);
		}

		get_io_service().post(boost::bind(&this_type::write, this, buf));

		if(100 <= status_)
		{
			stop();
		}
	}

	void http_download_base::__request()
	{
		http::request req;	
		req.version("HTTP/1.1");

		req.url(uri_.to_string(p2engine::uri::all_components
			&~p2engine::uri::protocol_component
			&~p2engine::uri::host_component
			&~p2engine::uri::port_component));
		req.host(uri_.host());

		if (bredirect_)
		{
			req.set(http::HTTP_ATOM_Referer, referurl_);	
		}

		if ("" != strrange_)
		{
			req.set(http::HTTP_ATOM_Range, strrange_);
		}

		safe_buffer buf;
		safe_buffer_io bio(&buf);
		bio<<req;
		socket_->async_send(buf);
	}

	void http_download_base::__connect()
	{
		std::string host = local_router_set_.get_address(uri_.host());
		if("" == host)
			host = uri_.host();

		socket_->async_connect(host, uri_.port());
	}

	void http_download_base::write(const safe_buffer& _buf)
	{
		try
		{
			__write(buffer_cast<const char*>(_buf), buffer_size(_buf));
		}
		catch (std::exception& e)
		{
			std::cout<<"http_download_base write error:"<<e.what()<<std::endl;
		}
	}
};