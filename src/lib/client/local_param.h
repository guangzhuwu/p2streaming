//
// local_param.h
// ~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2008 GuangZhu Wu (GuangZhuWu@gmail.com)
//
// All rights reserved. 
//
#ifndef client_client_local_param_h__
#define client_client_local_param_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "client/typedef.h"
#include "client/nat.h"
#include "common/utility.h"

namespace p2client{

	inline peer_id_t create_random_peer_id()
	{
		char buf[128];
		for (size_t i=0; i<sizeof(buf); ++i)
			buf[i] = random(0, 256);
		std::string md5Str=p2common::md5(&buf[0], sizeof(buf));
		peer_id_t id;
		size_t j=0;
		for (size_t i=0; i<id.size()&&i<md5Str.size();++i)
			id[j++]=md5Str[i];
		for (;j<id.size();)
			id[j++]=random(0, 256);
		return id;
	}

	inline const peer_id_t& static_local_peer_id()
	{
		static peer_id_t id=create_random_peer_id();
		return id;
	};

	inline const peer_id_t& change_static_local_peer_id()
	{
		const_cast<peer_id_t&>(static_local_peer_id())=create_random_peer_id();
		return static_local_peer_id();
	}

	inline bool init_local_peer_info(peer_info&info, const std::string& userInfo="")
	{
		const peer_id_t& local_peer_id=static_local_peer_id();

		if(!userInfo.empty())
			info.set_user_info(userInfo.c_str(), userInfo.length());
		info.set_info_version(0);
		info.set_peer_id(&local_peer_id[0], local_peer_id.size());
		info.set_nat_type((p2message::peer_nat_type)get_local_nat_type());
		info.set_peer_type(NORMAL_PEER);
		info.set_external_ip(0);
		info.set_internal_ip(0);
		info.set_external_udp_port(0);
		info.set_internal_udp_port(0);
		info.set_external_tcp_port(0);
		info.set_internal_tcp_port(0);
		info.set_upload_capacity(64*1024);
		info.set_join_time(0);
		info.set_relative_playing_point(0);
		info.set_version(P2P_VERSION);
		return true;
	}

	inline peer_info& static_local_peer_info()
	{
		static peer_info s_info;
		static bool dummy_=init_local_peer_info(s_info);

		return s_info;
	}

	inline peer_info&  init_static_local_peer_info(const std::string& userInfo="")
	{
		init_local_peer_info(static_local_peer_info(), userInfo);
		return static_local_peer_info();
	}

	inline boost::optional<variant_endpoint>& local_optional_endpoint()
	{
		static boost::optional<variant_endpoint> optional_edp;
		return optional_edp;
	}

}

#endif//tracker_client_service_h__