#include "urlcrack/crack_urls_base.hpp"
#include <boost/cstdint.hpp>
namespace urlcrack{

	basic_crack::basic_crack(const std::string& _url, io_service& ios)
		: basic_engine_object(ios)
		, url_(_url)
		, result_url_count_(0)
	{
		watch_timer_ = rough_timer::create(ios);
		watch_timer_->set_obj_desc("urlcrack::basic_crack::watch_timer_");
		watch_timer_->register_time_handler(boost::bind(&this_type::on_timer, this));
		watch_timer_->async_keep_waiting(seconds(5), seconds(15));
	};

	basic_crack::~basic_crack()
	{
		if(watch_timer_)
		{
			watch_timer_->cancel();
			watch_timer_->unregister_time_handler();
		}

	};

	void basic_crack::url_crack_sucess()
	{
		std::vector<std::string> result_urls;
		std::map<int, std::string>::const_iterator itr = mapresult_vod_urls_.begin();
		while(itr!=mapresult_vod_urls_.end())
		{
			result_urls.push_back(itr->second);
			++itr;
		}
		finish_signal_(result_urls);
	}

	void basic_crack::on_timer()
	{
		download_keeper_.clear_timeout();
	}
}