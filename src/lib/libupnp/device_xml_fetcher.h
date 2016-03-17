#ifndef device_xml_fetcher_h__
#define device_xml_fetcher_h__

#include <p2engine/push_warning_option.hpp>
#include <iostream>
#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>
#include <p2engine/pop_warning_option.hpp>

#include <p2engine/p2engine.hpp>
#include "libupnp/http_parser.hpp"

namespace libupnp
{
	using namespace p2engine;

	class device_xml_fetcher;
	typedef http::http_connection http_connection;
	PTR_TYPE_DECLARE(http_connection)
	typedef boost::function<void(error_code const&, http_parser const&, device_xml_fetcher&)> process_handler_type;
	typedef boost::function<void()> connect_handler_type;
	typedef p2engine::error_code error_code;

	class device_xml_fetcher
		: public basic_engine_object
	{
		typedef device_xml_fetcher this_type;
		SHARED_ACCESS_DECLARE;
		enum { MAX_BOTTLED_BUFFER = 1024 * 1024 };

	public:
		device_xml_fetcher(io_service& ios);
		virtual ~device_xml_fetcher();

		void get(std::string const& url, process_handler_type const& );
		void start(const std::string&, connect_handler_type const&, process_handler_type const& );
		void stop();
		void close(){stop();}
		std::string& send_buffer(){return send_buffer_;}
		http_connection& socket(){return *(connection_.get());}
		
	protected:
		void on_connect(http_connection* conn, const error_code& ec);
		void on_data(http_connection* conn, const safe_buffer& buf);
		void on_header(http_connection* conn, const http::response& _response);
		void on_closed(http_connection* conn, error_code ec);
		void callback(error_code e, char const* data, int size);
	
	protected:
		http_connection_sptr connection_;
		http_parser parser_;
		uri uri_;
		bool on_data_called_;
		std::string send_buffer_;
		process_handler_type process_handler_;
		connect_handler_type connect_handler_;
		std::string       str_recvbuffer_;
		std::string       str_header_;
		int read_pos_;
	};
};

#endif // device_xml_fetcher_h__
