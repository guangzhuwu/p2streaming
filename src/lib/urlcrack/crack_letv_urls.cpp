#include "urlcrack/crack_letv_urls.hpp"

#include <p2engine/push_warning_option.hpp>
#include <boost/regex.hpp>
#include <boost/bind.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <p2engine/pop_warning_option.hpp>

namespace urlcrack
{
	letv_crack::letv_crack(const std::string& _url, io_service& _ios):basic_crack(_url, _ios)
	{
		basexml_root_key_ = "root";
		basexml_containurl_key_ = "mmsJson";
		basejson_root_key_ = "bean";
		basejson_contain_key_ = "video";
		basejson_url_key_ = "url";
	};
	void letv_crack::get_crack_urls()
	{
		const boost::regex index_reg_1("http://www.letv.com/ptv/vplay/([0-9]+).html");
		const boost::regex index_reg_2("http://www.letv.com/ptv/pplay/([0-9]+)/([0-9]+).html");
		const boost::regex index_reg_3("http://www.letv.com/ptv/pplay/([0-9]+).html");
		boost::smatch what;
		std::string strid= "";
		if (boost::regex_match(url_, what, index_reg_1)
			|| boost::regex_match(url_, what, index_reg_3))
		{
			strid = std::string(what[1]);
		}
		else if (boost::regex_match(url_, what, index_reg_2))
		{
			down_content<funcType>(url_, boost::bind(&this_type::get_vid, this, _1));
			return;
		}

		if("" == strid)
		{
			resolve_failed();
			return;
		}

		get_crack_urls(strid);
	}

	void letv_crack::get_vid(const std::string& _content)
	{
		const boost::regex vid_reg(".*vid:([0-9]+).*");

		boost::smatch match_res;
		if (!boost::regex_match(_content, match_res, vid_reg))
		{
			resolve_failed();

			return;
		}

		std::string strvid = std::string(match_res[1]);

		get_crack_urls(strvid);
	}

	void letv_crack::get_crack_urls(const std::string& _strvid)
	{
			std::string xmlurl = "http://app.letv.com/v.php?id=";
			xmlurl += _strvid;
			down_content<funcType>(url_, boost::bind(&this_type::get_containurl_url, this, _1) );
	}

	void letv_crack::get_containurl_url(const std::string& _content)
	{
		try
		{
			std::vector<std::string> urls;
			std::stringstream sstrm;
			sstrm.str(_content);
			boost::property_tree::ptree pt;
			boost::property_tree::xml_parser::read_xml(sstrm, pt);
			boost::property_tree::ptree pchild = pt.get_child(basexml_root_key_);
			boost::property_tree::ptree pvod =pchild.get_child(basexml_containurl_key_);
			std::string strcontent = pchild.get<std::string>(basexml_containurl_key_);
			sstrm.str(strcontent);
			boost::property_tree::json_parser::read_json(sstrm, pt);
			pchild = pt.get_child(basejson_root_key_);
			BOOST_FOREACH(boost::property_tree::ptree::value_type& itr, pchild)
			{
				if (itr.first.compare(basejson_contain_key_) != 0)
				{
					continue;
				}
				else
				{
					BOOST_AUTO(vodnodeiter, itr.second.ordered_begin());
					urls.push_back(vodnodeiter->second.get<std::string>(basejson_url_key_));
				}
			}
			mapresult_vod_urls_.clear();
			result_url_count_ = (int)urls.size();
			for (int i=0; i<(int)urls.size(); i++)
			{
				funcType func = boost::bind(&this_type::get_vod_url, this, _1, i);
				down_content(urls[i], func);
			}
		}
		catch (...)
		{
			resolve_failed();
		}
	}


	void letv_crack::get_vod_url(const std::string& _content, int _index)
	{
		try
		{
			std::stringstream sstrm;
			sstrm.str(_content);
			boost::property_tree::ptree pt;
			boost::property_tree::json_parser::read_json(sstrm, pt);
			mapresult_vod_urls_[_index] = pt.get<std::string>("location");
		}
		catch (...)
		{
			resolve_failed();
		}

		if (mapresult_vod_urls_.size() == result_url_count_)
		{
			url_crack_sucess();
		}
	}
}  //end namespace urlcrack
