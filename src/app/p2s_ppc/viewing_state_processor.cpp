#include "p2s_ppc/viewing_state_processor.h"
#include "p2s_ppc/server.hpp"
#include "common/utility.h"

#if !defined(_DEBUG_SCOPE) && defined(NDEBUG)
#define  VIEWING_STATE_DBG(x)
#else 
#define  VIEWING_STATE_DBG(x) //x
#endif

NAMESPACE_BEGIN(ppc);

using namespace p2common;

viewing_state_processor::viewing_state_processor(boost::shared_ptr<p2sppc_server> svr)
	:basic_engine_object(svr->get_io_service())
	, p2sppc_server_(svr)
{

}

viewing_state_processor::~viewing_state_processor()
{

}

bool viewing_state_processor::process(const uri& u, const http::request& req, 
	const connection_sptr& sock)
{
	struct get_opt_type{
		p2client::opt_type operator()(const std::string& opt)const
		{
			if(boost::iequals(opt, "channel_start"))
				return p2client::OPT_CHANNEL_START;
			else if(boost::iequals(opt, "channel_stop"))
				return p2client::OPT_CHANNEL_STOP;
			else
				return p2client::OPT_UNKNOW;
		}
	};

	VIEWING_STATE_DBG(
		std::cout<<">>>>>>>> >>>>>>>> viewing_state_processor process: "<<req<<std::endl;	
	);

	std::map<std::string, std::string> qmap=u.query_map();
	const std::string&	s_opt=qmap["opt"];

	//{
	//std::ostringstream urlStrm;
	//urlStrm<<"http://"<<req.host()<<req.url();
	//printf("ppc receive http request  %s", urlStrm.str().c_str());
	//LOGD("ppc receive http request  %s", urlStrm.str().c_str());
	//}

	p2client::opt_type  opt = get_opt_type()(s_opt);
	if(!(opt == p2client::OPT_CHANNEL_START || opt == p2client::OPT_CHANNEL_STOP)){
		return false;
	}

	std::string	hex_channel_name;
	if( opt == p2client::OPT_CHANNEL_START)
	{
		hex_channel_name = qmap["channel_name"];
	}

	VIEWING_STATE_DBG(
		std::cout<<"opt: "<<s_opt<<std::endl;
	if(opt == p2client::OPT_CHANNEL_START){
		std::cout<<"channel_name: "<<hex_channel_name<<std::endl;
	}	  
	);

	std::string hex_link_string;
	if(opt == p2client::OPT_CHANNEL_START)
	{
		std::string hex_channel_name_md5=md5(hex_channel_name);
		hex_link_string=string_to_hex(hex_channel_name_md5);

		VIEWING_STATE_DBG(
		std::cout<<"hex_channel_link: "<<hex_link_string<<std::endl;
		std::cout<<"hex_channel_name: "<<hex_channel_name<<std::endl;
		)
	}


	if(!p2sppc_server_.expired())
	{
		if(opt == p2client::OPT_CHANNEL_START)
		{
			p2sppc_server_.lock()->report_viewing_info(opt, "dvb", hex_link_string, hex_channel_name);
		}
		else
		{
			p2sppc_server_.lock()->report_viewing_info(opt, "dvb");
		}
	}

	http::response res;
	res.status(http::header::HTTP_OK);
	safe_buffer buf;
	safe_buffer_io bio(&buf);
	bio<<res;
	sock->async_send(buf);
	sock->register_writable_handler(boost::bind(&this_type::handle_sentout_and_close, sock));

	return true;
}

void viewing_state_processor::handle_sentout_and_close(connection_sptr sock)
{
	if (!sock)
	{
		return;
	}
	sock->unregister_all_dispatch_handler();
	sock->close(true);
	std::cout<<"handle_sentout_and_close"<<std::endl;
}

NAMESPACE_END(ppc);
