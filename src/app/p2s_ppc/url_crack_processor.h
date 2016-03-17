#ifndef url_crack_processor_h__
#define url_crack_processor_h__

#include "p2s_ppc/typedef.h"
#include "p2s_ppc/server.hpp"
#include "urlcrack/crack_urls_adapter.hpp"

NAMESPACE_BEGIN(ppc);
class p2sppc_server;

class url_crack_processor
	:public basic_engine_object
	, public session_processor_base
{
	typedef url_crack_processor this_type;
	typedef boost::asio::ip::tcp tcp;
	SHARED_ACCESS_DECLARE;
	typedef http::http_connection_base   connection_type;
	typedef boost::shared_ptr<connection_type> connection_sptr;
	typedef boost::weak_ptr<connection_type> connection_wptr;

	typedef  urlcrack::crack_adapter crack_adapter;
	typedef  boost::shared_ptr<crack_adapter> crack_adapter_sptr;
	typedef  boost::weak_ptr<crack_adapter> crack_adapter_wptr;
	typedef boost::shared_ptr<p2sppc_server> ppc_server_sptr;

	typedef std::map<crack_adapter_sptr, connection_sptr> map_adapter_socket;

protected:
	url_crack_processor(ppc_server_sptr svr);
	virtual ~url_crack_processor();

public:
	static shared_ptr create(ppc_server_sptr svr)
	{
		return shared_ptr(new this_type( svr), 
			shared_access_destroy<this_type>()
			);
	}
	virtual bool process(const uri& u, const http::request& req, 
		const connection_sptr& sock);

private:
	void write_urls(const std::vector<std::string>& _urls, crack_adapter_wptr _adapter);

private:
	map_adapter_socket                    crack_adapters_;
	timed_keeper_set<crack_adapter_sptr>  adapters_delay_destroy_;
	ppc_server_sptr                       ppc_server_;
};

NAMESPACE_END(ppc);

#endif // url_crack_processor_h__
