#ifndef _resolve_urls_adapter_
#define _resolve_urls_adapter_

#include <p2engine/push_warning_option.hpp>
#include <boost/shared_ptr.hpp>
#include <string>
#include <vector>
#include <p2engine/pop_warning_option.hpp>

#include <p2engine/operation_mark.hpp>

#include "urlcrack/crack_urls_base.hpp"

namespace urlcrack
{
	class crack_adapter
		: public basic_engine_object
	{
		typedef crack_adapter this_type;
		SHARED_ACCESS_DECLARE;

	public:
		typedef urlcrack::basic_crack basic_crack;
		typedef basic_crack::call_back_signal call_back_signal;

		static shared_ptr create(const std::string& _url, io_service& _ios)
		{
			return shared_ptr(new this_type(_url, _ios), 
				shared_access_destroy<this_type>());
		}
		call_back_signal& get_crack_urls();

	protected:
		template<typename Handler>
		void dispatch_handler(Handler handler, const std::vector<std::string>& _url)const
		{
			handler(_url);
		}

		crack_adapter(const std::string& strUrl, io_service& ios);
		~crack_adapter(){};
	private:
		basic_crack::shared_ptr base_resolve_;
		call_back_signal dummy_signal_;
	};
}
#endif //_resolve_urls_adapter_