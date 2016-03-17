#ifndef _http_download_base_
#define _http_download_base_

#include <p2engine/push_warning_option.hpp>
#include <cstdlib>
#include <vector>
#include <string>
#include <p2engine/pop_warning_option.hpp>

#include <p2engine/http/http.hpp>
#include <p2engine/socket_utility.hpp>
#include <p2engine/keeper.hpp>
#include <p2engine/http/header.hpp>

namespace http_download{ 
	using namespace  p2engine;
	typedef boost::shared_ptr<http::http_connection> socket_sptr;

	class http_download_base
		: public basic_engine_object
	{
		typedef http_download_base this_type;
		typedef boost::shared_ptr<rough_timer> timer;
		SHARED_ACCESS_DECLARE;
	private:
		class local_router_set{
			typedef std::map<std::string, std::string> local_router;
			static const int router_keep_time_hour;
		public:
			local_router_set(){};
			~local_router_set(){};

			void add_address(const std::string& _host, const std::string& _ip)
			{
				if(keeped_host_.is_keeped(_host))
				{
					keeped_host_.erase(_host);
				}

				keeped_host_.try_keep(_host, hours(router_keep_time_hour));
				local_router_[_host] = _ip;
			}

			void erase_address(const std::string& _host)
			{
				keeped_host_.erase(_host);
				local_router_.erase(_host);
			}

			std::string get_address(const std::string& _host)
			{
				if(keeped_host_.is_keeped(_host))
				{
					return local_router_[_host];
				}
				else
				{
					return "";
				}
			}

		private:
			local_router_set(local_router_set& _local_router_set){};
			local_router_set& operator=(const local_router_set& rhs){};

		private:
			timed_keeper_set<std::string> keeped_host_;
			local_router                  local_router_;   
		};

		static const int elapse_reconnect;
		static const int connect_expire_time;
		static const int router_keep_time;
		static local_router_set local_router_set_;

	public:
		void start(const std::string& _url);
		void set_range(int64_t _start, int64_t _end);
		virtual void redirect(const std::string& url);
		virtual void stop();
	protected:
		void on_connected(http::http_connection* conn, const error_code& ec);
		virtual void on_disconnected(const error_code& ec);
		virtual void on_header(http::http_connection* conn, const http::response& _response);
		void on_data(http::http_connection* conn, const safe_buffer& buf);
		void __connect();
		void __request();
		virtual void __write(const char* buf, uint32_t len){};
		void write(const safe_buffer& _buf);
	protected:
		http_download_base(io_service& ios);
		virtual ~http_download_base();

		virtual void finish_down(){};
		virtual void failed_down(){};
		virtual void connect_overtime();
	protected:
		uri                uri_;
		socket_sptr        socket_;
		timer              reconnect_timer_;
		timer              connect_expire_timer_;
		int                status_;
		int64_t            start_pos_;  //下载过程中使用
		int64_t            ini_start_pos_;
		int64_t            end_pos_;    
		int64_t            content_length_;
		uint64_t           recvd_len_;
		std::string        strrange_;
		bool               b_downloal_all;
		bool               bredirect_;
		std::string        referurl_;
		bool               bstart_;
	};

};
#endif //_http_download_base_