#include "libupnp/device_xml_fetcher.h"
#include "libupnp/utility.h"
#include "libupnp/error_code.hpp"

#include <iostream>
#include <algorithm>

NAMESPACE_BEGIN(libupnp);

device_xml_fetcher::device_xml_fetcher(io_service& ios)
	:basic_engine_object(ios)
	, read_pos_(0)
	, on_data_called_(false)
{
}

device_xml_fetcher::~device_xml_fetcher()
{
	stop();
}

void device_xml_fetcher::stop()
{
	if(connection_)
	{
		connection_->close();
		connection_.reset();
	}
}

void device_xml_fetcher::start(const std::string& url, 
							   connect_handler_type const& conn_handler, 
							   process_handler_type const& process_handler
							   )
{
	connect_handler_ = conn_handler;
	process_handler_ = process_handler;
	parser_.reset();

	error_code ec;
	uri_.from_string(url, ec);
	connection_ = http_connection::create(get_io_service());
	connection_->register_connected_handler(
		boost::bind(&this_type::on_connect, this, connection_.get(), _1)
		);
	connection_->async_connect(uri_.host(), uri_.port());
}

void device_xml_fetcher::get(std::string const& url, process_handler_type const& handler)
{
	process_handler_ = handler;
	error_code ec;
	uri_.from_string(url, ec);
	connection_ = http_connection::create(get_io_service());
	connection_->register_connected_handler(
		boost::bind(&this_type::on_connect, this, connection_.get(), _1)
		);
	connection_->async_connect(uri_.host(), uri_.port());
}

void device_xml_fetcher::on_connect(http_connection* conn, const libupnp::error_code& ec)
{
	if ((ec)||(conn != connection_.get()))
		return;

	connection_->register_response_handler(
		boost::bind(&this_type::on_header, this, conn, _1)
		);
	connection_->register_data_handler(
		boost::bind(&this_type::on_data, this, conn, _1)
		);
	connection_->register_disconnected_handler(
		boost::bind(&this_type::on_closed, this, conn, _1)
		);
	if(connect_handler_)
		connect_handler_();	
	
	safe_buffer buf;
	safe_buffer_io bio(&buf);
	if(connect_handler_)
	{
		bio<<send_buffer_;
	}
	else
	{
		http::request req;	
		req.version("HTTP/1.1");
		req.url(uri_.path());
		req.host(uri_.to_string(uri::host_component|uri::port_component));
		bio<<req;
	}
	connection_->async_send(buf);
}

void device_xml_fetcher::on_data(http_connection* conn, const safe_buffer& buf)
{
	on_data_called_=true;

	if(conn != connection_.get())
		return;

	char* recv_buf = buffer_cast<char*>(buf);
	str_recvbuffer_.append(recv_buf, buf.size());
	read_pos_ += buffer_size(buf);

	if(parser_.header_finished())
	{
		libupnp::buffer::const_interval rcv_buf(str_recvbuffer_.c_str()
			, str_recvbuffer_.c_str() + read_pos_);
		bool error = false;
		parser_.incoming(rcv_buf, error);
	}

	if(read_pos_ == parser_.content_length()+str_header_.length())
	{
		char const* data = str_recvbuffer_.c_str();
		p2engine::error_code ec = asio::error::eof;
		callback(ec, data, str_recvbuffer_.length());
	}
}

void device_xml_fetcher::on_header(http_connection* conn, const http::response& _response)
{
	str_header_.clear();
	_response.serialize(str_header_);
	read_pos_ = str_header_.size();

	libupnp::buffer::const_interval rcv_buf(str_header_.c_str(), str_header_.c_str()+read_pos_);
	bool error = false;
	parser_.incoming(rcv_buf, error);

	str_recvbuffer_.clear();
	str_recvbuffer_.append(str_header_);

	if (!on_data_called_&&_response.content_length()==0)
	{
		on_data(conn, safe_buffer());
	}
}

void device_xml_fetcher::on_closed(http_connection* conn, error_code ec)
{
	if (!on_data_called_)
		on_data(conn, safe_buffer());
}

void device_xml_fetcher::callback(libupnp::error_code e, char const* data, int size)
{
	//if (m_bottled && m_called) return;

	std::vector<char> buf;
	if (data && parser_.header_finished())
	{
		if (parser_.chunked_encoding())
		{
			// go through all chunks and compact them
			// since we're bottled, and the buffer is our after all
			// it's OK to mutate it
			char* write_ptr = (char*)data;
			// the offsets in the array are from the start of the
			// buffer, not start of the body, so subtract the size
			// of the HTTP header from them
			int offset = parser_.body_start();
			std::vector<std::pair<size_type, size_type> > const& chunks = parser_.chunks();
			for (std::vector<std::pair<size_type, size_type> >::const_iterator i = chunks.begin()
				, end(chunks.end()); i != end; ++i)
			{
				BOOST_ASSERT(i->second - i->first < INT_MAX);
				int len = int(i->second - i->first);
				if (i->first - offset + len > size) len = size - int(i->first) + offset;
				memmove(write_ptr, data + i->first - offset, len);
				write_ptr += len;
			}
			size = write_ptr - data;
		}

		std::string const& encoding = parser_.header("content-encoding");
		if ((encoding == "gzip" || encoding == "x-gzip") && size > 0 && data)
		{
			std::string error;
			if (inflate_gzip(data, size, buf, MAX_BOTTLED_BUFFER, error))
			{
				if (process_handler_) 
					process_handler_(errors::http_failed_decompress, parser_, *this);
				stop();
				return;
			}
			size = int(buf.size());
			data = size == 0 ? 0 : &buf[0];
		}

		// if we completed the whole response, no need
		// to tell the user that the connection was closed by
		// the server or by us. Just clear any error
		if (parser_.finished()) e.clear();
	}
	//m_called = true;
	error_code ec;
	if (process_handler_) 
		process_handler_(e, parser_, *this);
}

NAMESPACE_END(libupnp);
