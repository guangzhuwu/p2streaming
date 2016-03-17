#ifndef _resolve_sohu_urls_
#define _resolve_sohu_urls_

#include "urlcrack/crack_urls_base.hpp"

namespace urlcrack
{
	class sohu_crack:public basic_crack
	{
		typedef sohu_crack this_type;
		SHARED_ACCESS_DECLARE;
	public:
		static shared_ptr create(const std::string& _url, io_service& _ios)
		{
			return shared_ptr(new sohu_crack(_url, _ios), 
				shared_access_destroy<this_type>());
		}
		virtual void get_crack_urls();
	protected:
		sohu_crack(const std::string& _url, io_service& _ios);
		~sohu_crack(){}
	private:
		void get_content_url(const std::string& _content);
		void process_content(std::string& _content);
		void get_vod_mid_urls(const std::string& _content);
		void get_vod_url(const std::string& _content, int _index);
	private:
		std::string               basejson_allot_key_;
		std::string               basejson_prot_key_;
		std::string               basejson_data_key_;
		std::string               basejson_su_key_;
		std::string               basejson_clipurl_key_;
		std::string               basejson_st_key_;
		std::string               save_parameter_;
		std::vector<std::string>  save_su_urls;

	};
}
#endif //_resolve_sohu_urls_