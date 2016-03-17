#include "urlcrack/crack_sohu_urls.hpp"

#include <p2engine/push_warning_option.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/regex.hpp>
#include <boost/foreach.hpp>
#include <p2engine/pop_warning_option.hpp>

#include <p2engine/gzip.hpp>

namespace urlcrack
{
	sohu_crack::sohu_crack(const std::string& _url, io_service& _ios)
		:basic_crack(_url, _ios)
	{
		basejson_allot_key_ = "allot";
		basejson_prot_key_ = "prot";
		basejson_data_key_ = "data";
		basejson_su_key_ = "su";
		basejson_st_key_ = "sT";
		basejson_clipurl_key_ = "clipsURL";
		save_parameter_ = "";
	}
	void sohu_crack::get_crack_urls()
	{
		funcType func = boost::bind(&this_type::get_content_url, this, _1);
		get_io_service().post(boost::bind(&this_type::down_content<funcType>, 
			SHARED_OBJ_FROM_THIS, url_, func));
	}

	void sohu_crack::get_content_url(const std::string& _content)
	{
		//gzip 解压
		std::vector<char> inflate_res;
		std::string str_error;
		std::string content;
		if(inflate_gzip(_content.c_str(), _content.size(), inflate_res, _content.size(), str_error))
		{
			content.assign(inflate_res.begin(), inflate_res.end());
		}
		else
		{
			content = _content;
		}

		const boost::regex vid_reg(".*vid[ ]*=[ ]*\"([0-9]+)\".*");
		const boost::regex pid_reg(".*pid[ ]*=[ ]*\"([0-9]+)\".*");
		boost::smatch vid_match;
		boost::smatch pid_math;
		if (boost::regex_match(content, vid_match, vid_reg) 
			&& boost::regex_match(content, pid_math, pid_reg))
		{
			std::string strurl = "http://hot.vrs.sohu.com/vrs_flash.action?vid=$vid&pid=$pid&ver=2&bw=1075&g=8&t=\".time()";
			boost::algorithm::replace_all(strurl, "$vid", std::string(vid_match[1]));
			boost::algorithm::replace_all(strurl, "$pid", std::string(pid_math[1]));
			strurl = p2engine::uri::escape(strurl.c_str());

			funcType func = boost::bind(&this_type::get_vod_mid_urls, this, _1);
			get_io_service().post(boost::bind(&this_type::down_content<funcType>, SHARED_OBJ_FROM_THIS, strurl, func));
		}
		else
		{
			resolve_failed();
		}
	}

	void  sohu_crack::process_content(std::string& _content)
	{
		boost::iterator_range<std::string::iterator> rge;
		rge = boost::find_first(_content, "\r\n");

		std::string str_length = _content.substr(0, rge.begin() - _content.begin());
		char* endpos;
		int         int_length = strtol(str_length.c_str(), &endpos, 16);
		_content = _content.substr(rge.end() - _content.begin(), int_length);
	}

	void  sohu_crack::get_vod_mid_urls(const std::string& _content)
	{
		try
		{
			//由于_content开始包含内容长度，需要处理
			std::string content = _content;
			process_content(content);

			std::stringstream sstrm;
			sstrm.str(content);
			boost::property_tree::ptree pt;
			boost::property_tree::json_parser::read_json(sstrm, pt);

			std::string allot = pt.get<std::string>(basejson_allot_key_);
			std::string prot = pt.get<std::string>(basejson_prot_key_);
			boost::property_tree::ptree pdata = pt.get_child(basejson_data_key_);
			save_parameter_ = pdata.get<std::string>(basejson_st_key_);

			boost::property_tree::ptree pclipurl =  pdata.get_child(basejson_clipurl_key_);
			boost::property_tree::ptree psu = pdata.get_child(basejson_su_key_);
			std::vector<std::string> clip_urls;
			std::vector<std::string>  mid_urls;

			BOOST_FOREACH(boost::property_tree::ptree::value_type&itr, pclipurl)
			{
				clip_urls.push_back(itr.second.data());
			}

			save_su_urls.clear();
			BOOST_FOREACH(boost::property_tree::ptree::value_type&itr, psu)
			{
				save_su_urls.push_back(itr.second.data());
			}

			BOOST_ASSERT(clip_urls.size() == save_su_urls.size());

			for (size_t i=0; i<clip_urls.size(); i++)
			{
				boost::replace_all(clip_urls[i], "http://data.vod.itc.cn", "");
				mid_urls.push_back("http://" + allot + "/" + "?prot=" + prot + "&file=" + clip_urls[i] + "&new=" + save_su_urls[i] + "&t=\".time()");
				mid_urls[i] = p2engine::uri::escape(mid_urls[i]);
			}

			mapresult_vod_urls_.clear();
			result_url_count_ = mid_urls.size();
			for (int i=0; i<(int)mid_urls.size(); i++)
			{
				funcType func = boost::bind(&this_type::get_vod_url, this, _1, i);
				get_io_service().post(
					boost::bind(&this_type::down_content<funcType>, SHARED_OBJ_FROM_THIS, mid_urls[i], func)
					);
			}
		}
		catch (...)
		{
			resolve_failed();
		}
	}

	void sohu_crack::get_vod_url(const std::string& _content, int _index)
	{
		try
		{
			std::string content = _content;
			process_content(content);

			std::vector<std::string> split_vals;
			boost::algorithm::split(split_vals, content, boost::algorithm::is_any_of("|"));
			BOOST_ASSERT(split_vals.size() > 3);

			mapresult_vod_urls_[_index] = split_vals[0].substr(0, split_vals[0].size() - 1)
				+ save_su_urls[_index] + "?start" + save_parameter_ +
				"&key=" + split_vals[3];
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
}//end namespace urlcrack