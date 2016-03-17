#ifndef _resolve_letv_urls_
#define _resolve_letv_urls_

#include "urlcrack/crack_urls_base.hpp"
#include <string>
namespace urlcrack
{
	class letv_crack: public basic_crack
	{
		typedef letv_crack this_type;
		SHARED_ACCESS_DECLARE;
	public:
		static shared_ptr create(const std::string& _url, io_service& _ios)
		{
			return shared_ptr(new this_type(_url, _ios), 
				shared_access_destroy<this_type>());
		}
		virtual void get_crack_urls();
	protected:
		letv_crack(const std::string& _url, io_service& _ios);
		~letv_crack(){};
		void get_vid(const std::string& _content);
		void get_crack_urls(const std::string& _strvid);
		void get_containurl_url(const std::string& _content);
		void get_vod_url(const std::string& _content, int iIndex);
	private:
		std::string basexml_root_key_;
		std::string basexml_containurl_key_;
		std::string basejson_root_key_;
		std::string basejson_contain_key_;
		std::string basejson_url_key_;
	};
};

#endif //_resolve_letv_urls_