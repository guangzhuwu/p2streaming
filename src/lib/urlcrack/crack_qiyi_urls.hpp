#ifndef _resolve_qiyi_urls_
#define _resolve_qiyi_urls_

#include "urlcrack/crack_urls_base.hpp"

#include <p2engine/push_warning_option.hpp>
#include <string>
#include <vector>
#include <p2engine/pop_warning_option.hpp>

namespace urlcrack
{
	class qiyi_crack 
		: public basic_crack
	{
		typedef qiyi_crack this_type;
		SHARED_ACCESS_DECLARE;
	public:
		static shared_ptr create(const std::string& strUrl, io_service& ios)
		{
			return shared_ptr(new this_type(strUrl, ios), 
				shared_access_destroy<this_type>());
		}
		virtual void get_crack_urls();
	protected:
		void get_base_xml_url(const std::string& _content);
		void get_base_urls(const std::string& _url);
		void get_time_rand_num(const std::vector<std::string>&  _urls);
		void modify_urls(const std::vector<std::string>&  _urls);
		void get_vod_urls(const std::vector<std::string>& _urls);
		void process_base_url(const std::string& _content);
		void process_time_rand_num(const std::string& rand_num, std::vector<std::string> _urls);
		void get_vod_url(const std::string& _content, int _index);
		qiyi_crack(const std::string& strUrl, io_service& ios);
		~qiyi_crack(){};
	private:
		std::string                basexml_rootkey_;
		std::string                basexml_vodkey_;
		std::string                basexml_urlfilekey_;
		std::string                basexml_filekey_;
	};
}
#endif// _resolve_qiyi_urls_