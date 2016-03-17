#ifndef viewing_state_processor_h__
#define viewing_state_processor_h__

#include "p2s_ppc/typedef.h"
#include "p2s_ppc/server.hpp"


NAMESPACE_BEGIN(ppc)

class viewing_state_processor
	:public basic_engine_object
	, public session_processor_base
{
	typedef viewing_state_processor this_type;
	SHARED_ACCESS_DECLARE;

	typedef http::http_connection_base   connection_type;
	typedef boost::shared_ptr<connection_type> connection_sptr;


protected:
	viewing_state_processor(boost::shared_ptr<p2sppc_server> svr);
	virtual ~viewing_state_processor();

public:
	static shared_ptr create(boost::shared_ptr<p2sppc_server> svr)
	{
		return shared_ptr(new this_type( svr), 
			shared_access_destroy<this_type>()
			);
	}

	bool process(const uri& u, const http::request& req, const connection_sptr& sock);

protected:
	static void handle_sentout_and_close(connection_sptr sock);

private:
	boost::weak_ptr<p2sppc_server> p2sppc_server_;
};


NAMESPACE_END(ppc)


#endif
