#include "urlcrack/crack_urls_adapter.hpp"
#include "urlcrack/crack_qiyi_urls.hpp"
#include "urlcrack/crack_letv_urls.hpp"
#include "urlcrack/crack_sohu_urls.hpp"
#include "urlcrack/crack_qq_urls.hpp"
#include "urlcrack/crack_youku_urls.hpp"

#include <p2engine/push_warning_option.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string_regex.hpp>
#include <p2engine/pop_warning_option.hpp>

#include <p2engine/uri.hpp>

namespace urlcrack{

	crack_adapter::crack_adapter(const std::string& _url, io_service& _ios)
		: basic_engine_object(_ios)
	{
		error_code ec;
		std::string strhost = p2engine::uri(_url, ec).host();
		boost::regex urlreg("^[a-zA-Z]*\\.{0, 1}(.+)");
		boost::smatch what;
		if (!boost::regex_match(strhost, what, urlreg))
		{
			return;
		}
		strhost = std::string(what[1]);
		if (boost::icontains(strhost, "qiyi.com"))
		{
			base_resolve_ = qiyi_crack::create(_url, _ios);
		}
		else if (boost::icontains(strhost.c_str(), "letv.com"))
		{
			base_resolve_ = letv_crack::create(_url, _ios);
		}
		else if (boost::icontains(strhost.c_str(), "sohu.com"))
		{
			base_resolve_ = sohu_crack::create(_url, _ios);
		}
		else if (boost::icontains(strhost.c_str(), "qq.com"))
		{
			base_resolve_ = qq_crack::create(_url, _ios);
		}
		else if (boost::icontains(strhost.c_str(), "youku.com"))
		{
			base_resolve_ = youku_crack::create(_url, _ios);
		}
	}

	crack_adapter::call_back_signal& crack_adapter::get_crack_urls()
	{
		BOOST_ASSERT(base_resolve_);

		if (base_resolve_)
		{
			get_io_service().post(
				boost::bind(&basic_crack::get_crack_urls, base_resolve_)
				);

			return base_resolve_->resolved_signal();
		}
		dummy_signal_.clear();
		return dummy_signal_;
	}

}//end namespace urlcrack

