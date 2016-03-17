#include "p2s_ppc/url_crack_processor.h"
#include "p2s_ppc/server.hpp"

#include <p2engine/push_warning_option.hpp>
#include <p2engine/config.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <p2engine/pop_warning_option.hpp>

NAMESPACE_BEGIN(ppc);

#if !defined(_DEBUG_SCOPE) && defined(NDEBUG)
#	define  URL_CRACK_DBG(x)/* x*/
#else 
#	define  URL_CRACK_DBG(x) x
#endif

url_crack_processor::url_crack_processor(ppc_server_sptr svr)
	:basic_engine_object(svr->get_io_service())
{
	BOOST_ASSERT(svr);
	ppc_server_ = svr;
}
url_crack_processor::~url_crack_processor()
{
}

bool url_crack_processor::process(const uri& u, const http::request& req, 
	const connection_sptr& sock)
{
	std::map<std::string, std::string> qmap=u.query_map();
	const std::string& cmd=qmap["cmd"];
	if (cmd == "url_crack")
	{
		std::string url = qmap["uri"];
		url = hex_to_string(url);
		crack_adapter_sptr cracker = crack_adapter::create(url, get_io_service());
		crack_adapter_wptr cracker_wptr(cracker);
		connection_wptr sock_wsptr(sock);
		cracker->get_crack_urls()=boost::bind(&this_type::write_urls, this, _1, cracker_wptr);
		crack_adapters_[cracker] = sock;
		return true;
	}

	return false;
}

void url_crack_processor::write_urls(const std::vector<std::string>& _urls, 
	crack_adapter_wptr _adapter)
{
	BOOST_ASSERT(_adapter.lock() && crack_adapters_[_adapter.lock()]);

	static const std::string root_key = "datainfo";
	static const std::string error_key = "errcode";
	static const std::string error_msg_key = "msg";
	static const std::string url_lst_key = "urllist";
	static const std::string url_key = "url";
	static const std::string cont_key = "totalcount";

	boost::property_tree::ptree pt;
	if (_urls.size() == 0)
	{
		pt.add(root_key + "." + error_key, "1");
		pt.add(root_key + "." + error_msg_key, "cann't get video urls");
	}
	else
	{
		pt.add(root_key + "." + error_key, "0");
		BOOST_FOREACH(const std::string& str, _urls)
		{
			pt.add(root_key + "." + url_lst_key + "." + url_key, str);
		}
		pt.add(root_key + "." + cont_key, boost::lexical_cast<std::string>(_urls.size()));

	}

	std::stringstream ssm;
	std::string str_xml;
	boost::property_tree::write_xml(ssm, pt);
	str_xml = ssm.str();

	http::response res;
	res.status(http::header::HTTP_OK);
	res.content_length(str_xml.size());

	safe_buffer buf;
	safe_buffer_io bio(&buf);
	bio<<res<<str_xml;
	ppc_server_->close_connection(crack_adapters_[_adapter.lock()], buf);

	adapters_delay_destroy_.try_keep(_adapter.lock(), seconds(5));

	crack_adapters_.erase(_adapter.lock());

	URL_CRACK_DBG(
	std::cout<<"--------------url crack result start------------"<<std::endl;
	std::cout<<str_xml<<std::endl;
	std::cout<<"--------------url crack result end------------"<<std::endl;
	std::cout<<"-----has url crackcount----"<<crack_adapters_.size()<<std::endl;
	);
}


NAMESPACE_END(ppc);
