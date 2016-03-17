#ifndef _resolve_qq_urls_
#define _resolve_qq_urls_

#include "urlcrack/crack_urls_base.hpp"

namespace urlcrack{
	class qq_crack:public basic_crack
	{
		typedef qq_crack this_type;
		SHARED_ACCESS_DECLARE;
	public:
		static shared_ptr create(const std::string& _url, io_service& _ios)
		{
			return shared_ptr(new qq_crack(_url, _ios), 
				shared_access_destroy<this_type>());
		}
		virtual  void get_crack_urls();
	protected:
		qq_crack(const std::string& _url, io_service& _ios):basic_crack(_url, _ios){};
		~qq_crack(){};
		void get_mid_urls(const std::string& _content);
		void get_vod_url(const std::string& _content, int _index);
	};
}
#endif //_resolve_qq_urls_