#ifndef _resolve_vod_urls_hpp_
#define _resolve_vod_urls_hpp_

#include "httpdownload/http_download_memory.hpp"

#include <p2engine/push_warning_option.hpp>
#include <vector>
#include <string>
#include <map>
#include <p2engine/pop_warning_option.hpp>

#include <p2engine/shared_access.hpp>
#include <p2engine/safe_buffer.hpp>
#include <p2engine/basic_engine_object.hpp>
#include <p2engine/keeper.hpp>
#include <p2engine/timer.hpp>

namespace urlcrack
{
	using namespace p2engine;
	class basic_crack
		: public p2engine::basic_engine_object
	{
		typedef basic_crack this_type;
		typedef http_download::http_download_memory http_download_memory;
		typedef http_download_memory::shared_ptr http_memory_down_sptr;
		typedef boost::shared_ptr<rough_timer>           timer_sptr;
	protected:
		typedef boost::function<void(const std::string&)> funcType;
		SHARED_ACCESS_DECLARE;

	public:
		typedef boost::function<void(const std::vector<std::string>&) > call_back_signal;

		call_back_signal& resolved_signal()
		{
			return finish_signal_;
		}
		virtual  void get_crack_urls(){};
		static shared_ptr create(const std::string& _url, io_service& _ios)
		{
			return shared_ptr(new this_type(_url, _ios), 
				shared_access_destroy<this_type>());
		}

	protected:
		template<typename Handler>
		void down_content(const std::string& _url, const Handler& handler)
		{
			http_memory_down_sptr downer_sptr = http_download::http_download_memory::create(get_io_service());
			
			boost::system::error_code boosterr;
			downer_sptr->register_down_finish_handler(
				boost::bind(&this_type::dispatch_handler<Handler>, this, handler, _1)
				);
			get_io_service().post(
				boost::bind(&http_download::http_download_memory::start, downer_sptr, _url)
				);
			download_keeper_.try_keep(downer_sptr, seconds(30));
		}

		void resolve_failed()
		{
			std::vector<std::string> null_urls;
			finish_signal_(null_urls);
		}

		static std::string int_to_hexstr(int64_t _ival)
		{
			char res[64] = {0};
#ifdef _MSC_VER
			sprintf(res, "%I64X", _ival);
#else
			sprintf(res, "%llX", _ival);
#endif
			return res;
		}

		basic_crack(const std::string& _url, io_service& ios);

		~basic_crack();
	protected:
		template<typename Handler>
		void dispatch_handler(Handler handler, const std::string& content)const
		{
			handler(content);
		}

		void url_crack_sucess();
		/*
		检查到期的下载，最长时间为30秒
		*/
		void on_timer();
	protected:
		std::string                               url_;
		call_back_signal                          finish_signal_;
		std::map<int, std::string>                mapresult_vod_urls_;
		timed_keeper_set<http_memory_down_sptr>   download_keeper_;
		int                                       result_url_count_;
		timer_sptr                                watch_timer_;
	};
}//end namespace urlcrack
#endif //_resolve_vod_urls_hpp_
