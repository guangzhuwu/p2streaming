//
// overlay.h
// ~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2008 GuangZhu Wu (GuangZhuWu@gmail.com)
//
// All rights reserved. 
#ifndef peer_scheduling_base_h__
#define peer_scheduling_base_h__

#include "client/typedef.h"

namespace p2client{

	class scheduling_base
		: public basic_engine_object
		, public basic_client_object
	{
		typedef scheduling_base this_type;
		SHARED_ACCESS_DECLARE;

		friend class overlay;

	protected:
		scheduling_base(io_service& ios, const client_param_sptr& param)
			:basic_engine_object(ios)
			, basic_client_object(param)
		{}
		virtual ~scheduling_base(){}

	public:
		virtual void start()=0;
		virtual	void stop(bool flush=false)=0;
		virtual void set_play_offset(int64_t offset)=0;
		virtual void process_recvd_buffermap(const buffermap_info& bufmap, peer_connection* p)=0;
		virtual void send_handshake_to(peer_connection* conn)=0;
		virtual void register_message_handler(peer_connection* conn)=0;
		virtual void neighbor_erased(const peer_id_t& id)=0;
	};

}
#endif//peer_topology_base_h__
