#ifndef _resolve_youku_urls_
#define _resolve_youku_urls_
#include "urlcrack/crack_urls_base.hpp"
namespace urlcrack{
	class youku_crack:public basic_crack
	{
		typedef youku_crack this_type;
		SHARED_ACCESS_DECLARE;
	public:
		static shared_ptr create(const std::string& _url, io_service& _ios)
		{
			return shared_ptr(new youku_crack(_url, _ios)
				, shared_access_destroy<this_type>());
		}
		virtual void get_crack_urls();
	protected:
		youku_crack(const std::string& _url, io_service& _ios):basic_crack(_url, _ios){};
		~youku_crack(){};
		std::string get_file_idmix_string(const std::string& _seed);
		std::string get_file_id(const std::string& _file_id, const std::string& _seed);
		std::string generate_key(const std::string& _key1, const std::string& _key2);
		std::string generate_sid();
		void get_vod_urls(const std::string& _content);
	};
}
#endif //_resolve_youku_urls_