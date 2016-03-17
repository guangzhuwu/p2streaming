//
// tracker_service_logic.h
// ~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2008 GuangZhu Wu (GuangZhuWu@gmail.com)
//
// All rights reserved. 
//
#ifndef tracker_tracker_service_logic_h__
#define tracker_tracker_service_logic_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "tracker/config.h"
#include "tracker/member_table.h"

namespace p2tracker{
	
	class tracker_service;

	class tracker_service_logic_base
		:public basic_engine_object
	{
		typedef tracker_service_logic_base this_type;
		SHARED_ACCESS_DECLARE;
	public:
		typedef variant_endpoint endpoint;
		typedef member_table::peer peer;
		typedef rough_timer timer;

	protected:
		tracker_service_logic_base(io_service& svc);
		virtual ~tracker_service_logic_base();

	public:
		void start(const tracker_param_base& param);
		void stop(){__stop();}

	public:
		//网络消息处理
		virtual void register_message_handler(message_socket*)=0;
		virtual void known_offline(peer*)=0;
		virtual bool permit_relay(peer*, const relay_msg&)=0;
		virtual void recvd_peer_info_report(peer* , const p2ts_quality_report_msg&)=0;
	protected:
		void __stop();

	protected:
		boost::shared_ptr<tracker_service> tracker_service_;
	};
}

#endif//tracker_member_service_h__