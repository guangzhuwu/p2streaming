#include "urlcrack/crack_youku_urls.hpp"

#include <p2engine/push_warning_option.hpp>
#include <p2engine/utilities.hpp>
#include <boost/bind.hpp>
#include <boost/regex.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/algorithm/string.hpp>
#include <p2engine/pop_warning_option.hpp>

namespace urlcrack
{
	typedef boost::property_tree::ptree json_tree;
	typedef boost::property_tree::basic_ptree<std::string, std::string>::assoc_iterator json_tree_iterator;
	void youku_crack::get_crack_urls()
	{
		const boost::regex vid_reg(".*v_show/id_(.*).html.*");
		boost::smatch match_res;
		if (!boost::regex_match(url_, match_res, vid_reg))
		{
			return;
		}
		std::string url = "http://v.youku.com/player/getPlayList/VideoIDS/" + std::string(match_res[1])
			+ "/timezone/+08/version/5/source/video?password=&ran=8159&n=3";

		funcType func = boost::bind(&this_type::get_vod_urls, this, _1);
		get_io_service().post(boost::bind(&this_type::down_content<funcType>, this, url, func));
	}

	std::string youku_crack::get_file_idmix_string(const std::string& _seed)
	{
		std::string source = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ/\\:._-1234567890";
		std::string mixed = "";
		boost::int64_t int_seed = boost::lexical_cast<boost::int64_t>(_seed);
		int souce_len = source.size();
		int index = -1;
		for (int i=0; i<souce_len; i++)
		{
			int_seed = (int_seed * 211 + 30031) % 65536;
			index =  int(int_seed * source.size() / 65536);
			mixed = mixed + source.substr(index, 1);
			source = source.substr(0, index) + source.substr(index + 1);
		}
		return mixed;
	}

	std::string youku_crack::get_file_id(const std::string& _file_id, const std::string& _seed)
	{
		std::string mixed = get_file_idmix_string(_seed);
		std::vector<std::string> file_ids;
		boost::algorithm::split(file_ids, _file_id, boost::algorithm::is_any_of("*"));

		std::string read_id;
		int int_idx = -1;
		for (size_t i=0; i<file_ids.size() - 1; i++)
		{
			int_idx = boost::lexical_cast<int>(file_ids[i]);
			read_id += mixed.substr(int_idx, 1);
		}
		return read_id;
	}

	std::string youku_crack::generate_key(const std::string& _key1, const std::string& _key2)
	{
		char* endpos;
		boost::int64_t int_key1 = strtoll(_key1.c_str(), &endpos, 16);
		int_key1 = int_key1^0xA55AA5A5LL;		
		std::string hex_res_str = int_to_hexstr(int_key1);
		return _key2 + hex_res_str;
	}

	std::string youku_crack::generate_sid()
	{
		boost::uint_fast64_t sid_1 = 1000 + boost::uint_fast64_t((random(0, 8) % 8 + 1) * 9999);
		boost::uint_fast64_t sid_2 = 10000 + boost::uint_fast64_t((random(0, 8) % 8 + 1) * 99999ULL);
		boost::int64_t t = time(NULL);
		return boost::lexical_cast<std::string>(t) + boost::lexical_cast<std::string>(sid_1) + boost::lexical_cast<std::string>(sid_2);
	}

	void youku_crack::get_vod_urls(const std::string& _content)
	{
		try
		{
			std::stringstream ssm;
			ssm.str(_content);
			json_tree pt;
			boost::property_tree::read_json(ssm, pt);
			json_tree_iterator vodnodeiter = pt.get_child("data").ordered_begin();
			json_tree pseg = vodnodeiter->second;
			std::string seed = pseg.get_child("seed").data();
			json_tree pstream_file = pseg.get_child("streamfileids");
			std::string file_id = pstream_file.get_child("flv").data();
			json_tree pseg_s = pseg.get_child("segs");
			json_tree pflv = pseg_s.get_child("flv");
			std::vector<std::string> keys_list;
			int seg_flv_count = 0;
			BOOST_FOREACH(json_tree::value_type& itr, pflv)
			{
				json_tree_iterator pkey_iter = itr.second.ordered_begin();
				keys_list.push_back(pkey_iter->second.data());
				seg_flv_count++;
			}
			std::string decode_d = get_file_id(file_id, seed);
			std::string decode_s = generate_sid();
			std::string temp_ss = "";
			std::string temp_d;

			mapresult_vod_urls_.clear();
			for (int i=0; i<seg_flv_count; i++)
			{
				temp_ss = int_to_hexstr(i);

				//×ª³É´óÐ´
				boost::algorithm::to_upper(temp_ss);

				if (temp_ss.size() == 1)
				{
					temp_ss = "0" + temp_ss;
				}
				temp_d = decode_d.substr(0, 8) + temp_ss + decode_d.substr(10);
				temp_ss = decode_s + "_" + temp_ss;
				mapresult_vod_urls_[i] = "http://f.youku.com/player/getFlvPath/sid/" + temp_ss
					+ "/st/flv/fileid/" + temp_d + "?K=" + keys_list[i];
			}

			url_crack_sucess();
		}
		catch (...)
		{
			resolve_failed();
		}
	}
}//end namespace urlcrack