#ifndef p2sppc_typedef_h__
#define p2sppc_typedef_h__

#include <p2engine/p2engine.hpp>
#include <p2engine/push_warning_option.hpp>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <p2engine/pop_warning_option.hpp>
#include "common/common.h"
#include "client/client_service_logic.h"

namespace ppc{
	using namespace p2engine;
	using namespace p2client;

	typedef http::basic_http_connection<http::http_connection_base> http_connection;
	typedef http::basic_http_acceptor<http_connection, http_connection>	http_acceptor;
}



#endif //p2sppc_typedef_h__