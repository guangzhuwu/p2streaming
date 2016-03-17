#include "urlcrack/crack_qiyi_urls.hpp"

#include <p2engine/push_warning_option.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/regex.hpp>
#include <boost/filesystem.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <p2engine/pop_warning_option.hpp>

#include <p2engine/safe_buffer.hpp>
#include <p2engine/safe_buffer_io.hpp>

namespace urlcrack{

	qiyi_crack::qiyi_crack(const std::string& strUrl, io_service& ios)
		: basic_crack(strUrl, ios)
	{
		basexml_rootkey_ = "root";
		basexml_vodkey_ = "video";
		basexml_urlfilekey_ = "fileUrl";
		basexml_filekey_ = "file";
	}

	void qiyi_crack::get_crack_urls()
	{
		funcType func = boost::bind(&this_type::get_base_xml_url, this, _1);
		get_io_service().post(
			boost::bind(&this_type::down_content<funcType>, SHARED_OBJ_FROM_THIS, url_, func)
			);
	}

	void qiyi_crack::get_base_xml_url(const std::string& _content)
	{
		const boost::regex vodIDReg(".*videoId : \"([A-Za-z0-9]+)\".*");
		const boost::regex NewvodIDReg(".*\"videoId\":\"([A-Za-z0-9]+)\".*");

		boost::smatch whatvodID;
		if (boost::regex_match(_content, whatvodID, vodIDReg) 
			|| boost::regex_match(_content, whatvodID, NewvodIDReg))
		{
			std::string url =  "http://cache.video.qiyi.com/v/"  
				+ std::string(whatvodID[1]); 
			get_base_urls(url);
		}
		else
		{
			resolve_failed();
		}
	}

	void qiyi_crack::get_base_urls(const std::string& _url)
	{
		funcType func = boost::bind(&this_type::process_base_url, this, _1);
		get_io_service().post(
			boost::bind(&this_type::down_content<funcType>, SHARED_OBJ_FROM_THIS, _url, func)
			);
	}

	void qiyi_crack::process_base_url(const std::string& _content)
	{
		try
		{
			std::stringstream sstrm;
			sstrm.str(_content);

			boost::property_tree::ptree pt;
			boost::property_tree::xml_parser::read_xml(sstrm, pt);
			boost::property_tree::ptree pchild = pt.get_child(basexml_rootkey_);

			boost::property_tree::ptree pvod = pchild.get_child(basexml_vodkey_);
			boost::property_tree::ptree pfileurls = pvod.get_child(basexml_urlfilekey_);

			std::vector<std::string> _urls;
			BOOST_FOREACH(boost::property_tree::ptree::value_type& itr, pfileurls)
			{
				if(0 != basexml_filekey_.compare(itr.first))
					continue;
				_urls.push_back(itr.second.data());
			}
			modify_urls(_urls);
		}
		catch (...)
		{
			resolve_failed();
		}
	}

	void qiyi_crack::modify_urls(const std::vector<std::string>& urls)
	{
		get_time_rand_num(urls);
	}

	void qiyi_crack::get_time_rand_num(const std::vector<std::string>& _urls)
	{
		funcType func = boost::bind(&this_type::process_time_rand_num, this, _1, _urls);

		std::string url = "http://data.video.qiyi.com/t.hml?tn=";
		get_io_service().post(
			boost::bind(&this_type::down_content<funcType>, SHARED_OBJ_FROM_THIS, url, func)
			);
	}
	void qiyi_crack::process_time_rand_num(const std::string& rand_num, std::vector<std::string> _urls)
	{
		const boost::regex timeReg(".*\"t\":\"([0-9]+)\".*");
		boost::smatch what;
		if (boost::regex_match(rand_num, what, timeReg))
		{
			boost::int64_t time = boost::lexical_cast<boost::int64_t>(what[1]);
			std::string _srandnum = boost::lexical_cast<std::string>(time^2519219136ULL + 4294967296ULL);

			for (size_t i=0; i<_urls.size(); i++)
			{
				boost::replace_all(_urls[i], "f4v", "hml");
				_urls[i] = _urls[i] + "?v=" + _srandnum;
			}
			get_vod_urls(_urls);
		}
		else
		{
			resolve_failed();
		}
	}

	void qiyi_crack::get_vod_urls(const std::vector<std::string>& _urls)
	{
		mapresult_vod_urls_.clear();
		result_url_count_ = _urls.size();
		for (int i=0; i<(int)_urls.size(); i++)
		{
			funcType func = boost::bind(&this_type::get_vod_url, this, _1, i);
			get_io_service().post(
				boost::bind(&this_type::down_content<funcType>, SHARED_OBJ_FROM_THIS, _urls[i], func)
				);
		}
	}

	void qiyi_crack::get_vod_url(const std::string& _content, int _index)
	{
		const boost::regex timeReg(".*\"l\":\"([:&/\\.\\?\\=0-9A-Za-z]+)\".*");
		boost::smatch what;
		if (boost::regex_match(_content, what, timeReg))
		{
			mapresult_vod_urls_[_index] = boost::lexical_cast<std::string>(what[1]);
		}
		else
		{
			resolve_failed();
		}

		if(mapresult_vod_urls_.size() == result_url_count_)
		{
			url_crack_sucess();
		}
	}
} //end namespace urlcrack