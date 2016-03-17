//
// config.h
// ~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2008 GuangZhu Wu (GuangZhuWu@gmail.com)
//
// All rights reserved. 
//
#ifndef server_config_h__
#define server_config_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "common/common.h"
#include "simple_server/simple_distributor.h"

namespace p2server{
	using namespace p2engine;
	using namespace p2common;
	using namespace p2simple;
	using namespace asfio;

	class basic_server_object
	{
	public:
		basic_server_object(const server_param_sptr& param)
			:server_param_(param)
		{
		}
		virtual ~basic_server_object()
		{
		}
		server_param_sptr& get_server_param_sptr()
		{
			return server_param_;
		}
		const server_param_sptr& get_server_param_sptr()const
		{
			return server_param_;
		}
	protected:
		server_param_sptr server_param_;
	};
}

#endif//server_typedef_h__
