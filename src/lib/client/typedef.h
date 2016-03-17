//
// typedef.h
// ~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2008 GuangZhu Wu (GuangZhuWu@gmail.com)
//
// All rights reserved. 
//
#ifndef peer_typedef_h__
#define peer_typedef_h__

#include "common/common.h"

namespace boost{
	namespace multi_index{}
}

namespace natpunch{

}

namespace p2server{
	class server;
}

namespace p2client{

	enum{INVALID_ID=-1};

	using namespace p2engine;
	using namespace p2common;
	using namespace natpunch;
	namespace multi_index=boost::multi_index;

	class peer;
	class client;
	class client_service;
	class client_service_logic_base;
	class overlay;
	class stream_topology;
	class hub_topology;
	class stream_scheduling;
	class hub_scheduling;
	class peer_connection;
	class cache_topology;
	class cache_scheduling;
	class tracker_manager;

	PTR_TYPE_DECLARE(peer);
	PTR_TYPE_DECLARE(client);
	PTR_TYPE_DECLARE(client_service);
	PTR_TYPE_DECLARE(client_service_logic_base);
	PTR_TYPE_DECLARE(overlay);
	PTR_TYPE_DECLARE(stream_topology);
	PTR_TYPE_DECLARE(hub_topology);
	PTR_TYPE_DECLARE(peer_connection);
	PTR_TYPE_DECLARE(stream_scheduling);
	PTR_TYPE_DECLARE(hub_scheduling);
	PTR_TYPE_DECLARE(cache_topology);
	PTR_TYPE_DECLARE(cache_scheduling);
	PTR_TYPE_DECLARE(tracker_manager);

	class basic_client_object
	{
	public:
		basic_client_object(const client_param_sptr& param)
			:client_param_(param)
		{
		}
		virtual ~basic_client_object()
		{
		}
		client_param_sptr& get_client_param_sptr()
		{
			return client_param_;
		}
		const client_param_sptr& get_client_param_sptr()const
		{
			return client_param_;
		}
		bool is_vod()const
		{
			return p2common::is_vod_category(client_param_->type);
		}
		bool is_live()const
		{
			return p2common::is_live_category(client_param_->type);
		}
		bool is_shift_live()const
		{
			return p2common::is_shift_live_category(client_param_->type);
		}
	protected:
		client_param_sptr client_param_;
	};

}

#endif//peer_typedef_h__
