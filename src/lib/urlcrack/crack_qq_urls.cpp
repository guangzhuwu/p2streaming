#include "urlcrack/crack_qq_urls.hpp"

#include <p2engine/push_warning_option.hpp>
#include <boost/bind.hpp>
#include <boost/regex.hpp>
#include <boost/algorithm/string.hpp>
#include <p2engine/pop_warning_option.hpp>

namespace urlcrack{
	void qq_crack::get_crack_urls()
	{
		const boost::regex vid_reg(".*vid=([a-zA-Z0-9|]+)$");
		boost::smatch match_res;
		if (boost::regex_match(url_, match_res, vid_reg))
		{
			result_url_count_ = 1;
			mapresult_vod_urls_.clear();
			std::string url = "http://vv.video.qq.com/geturl?format=2&ran=0%2E37202127324417233&platform=1&vid=" + std::string(match_res[1]) + "&otype=xml";
			funcType func = boost::bind(&this_type::get_vod_url, this, _1, true);
			get_io_service().post(boost::bind(&this_type::down_content<funcType>, this, url, func));
		}
		else
		{
			funcType func = boost::bind(&this_type::get_mid_urls, this, _1);
			get_io_service().post(boost::bind(&this_type::down_content<funcType>, this, url_, func));
		}
	}

	void qq_crack::get_mid_urls(const std::string& _content)
	{
		const boost::regex vid_reg(".*vid:\"([a-zA-Z0-9|]+)\".*");
		boost::smatch match_res;
		if (!boost::regex_match(_content, match_res, vid_reg))
		{
			resolve_failed();
			return;
		}
		std::string strmatch = std::string(match_res[1]);
		const char* p=strmatch.c_str();
		std::vector<std::string> res_vids;
		boost::algorithm::split(res_vids, p, boost::algorithm::is_any_of("|"));
		if (res_vids.size() == 0)
		{
			resolve_failed();
			return;
		}
		mapresult_vod_urls_.clear();
		result_url_count_ = res_vids.size();
		for (int i=0; i<(int)res_vids.size(); i++)
		{
			funcType func = boost::bind(&this_type::get_vod_url, this, _1, i);
			std::string url = "http://vv.video.qq.com/geturl?format=2&ran=0%2E37202127324417233&platform=1&vid=" + res_vids[i] + "&otype=xml";
			get_io_service().post(boost::bind(&this_type::down_content<funcType>, this, url, func));
		}
	}

	void qq_crack::get_vod_url(const std::string& _content, int _index)
	{
		const boost::regex url_reg(".*<url>(.+)</url>.*");
		boost::smatch match_res;
		if (boost::regex_match(_content, match_res, url_reg))
		{
			mapresult_vod_urls_[_index] = std::string(match_res[1]);
		}
		else
		{
			resolve_failed();
		}

		if (mapresult_vod_urls_.size() == result_url_count_)
		{
			url_crack_sucess();
		}
	}
}//end namespace urlcrack