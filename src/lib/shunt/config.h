//
// config.h
// ~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2008 GuangZhu Wu (GuangZhuWu@gmail.com)
//
// All rights reserved. 
//
#ifndef shunt_config_h__
#define shunt_config_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "common/common.h"
#include "client/client_service_logic.h"
#include "server/server_service_logic.h"
#include "tracker/tracker_service_logic.h"

namespace p2shunt{
	using namespace p2engine;

	extern const std::string default_channel_key;
	extern const std::string default_channel_uuid;
}

#endif//shunt_config_h__
