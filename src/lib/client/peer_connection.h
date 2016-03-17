//
// peer_connection.h
// ~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2008 GuangZhu Wu (GuangZhuWu@gmail.com)
//
// All rights reserved. 
//
#ifndef peer_peer_connection_base_h__
#define peer_peer_connection_base_h__

#include "client/typedef.h"
#include "client/peer.h"

namespace p2client{

	class peer_connection
		:public message_socket
	{
		typedef peer_connection this_type;
		SHARED_ACCESS_DECLARE;

	public:
		typedef peer_connection connection_base_type;

		peer_connection(io_service& ios, bool realTimeUsage, bool isPassive);
		virtual ~peer_connection();

		const peer_sptr& get_peer()const{return peer_;}
		void set_peer(const peer_sptr&  p){peer_=p;peer_->set_connection(SHARED_OBJ_FROM_THIS);}
		bool is_link_local();
		int local_id()const{return local_id_;}
		void local_id(int id){ local_id_=id;}
		int local_uuid()const{return local_uuid_;}
		void local_uuid(int id){ local_uuid_=id;}

	protected:
		peer_sptr  peer_;
		int local_id_;
		int local_uuid_;
		boost::optional<bool> is_link_local_;
		bool natpunched_;
	};

	typedef basic_urdp_connection<peer_connection> urdp_peer_connection;
	typedef basic_trdp_connection<peer_connection> trdp_peer_connection;

	typedef basic_acceptor<peer_connection> peer_acceptor;
	typedef basic_urdp_acceptor<urdp_peer_connection, urdp_peer_connection::connection_base_type> 
		urdp_peer_acceptor;
	typedef basic_trdp_acceptor<trdp_peer_connection, trdp_peer_connection::connection_base_type> 
		trdp_peer_acceptor;

	PTR_TYPE_DECLARE(peer_connection);
	PTR_TYPE_DECLARE(peer_acceptor);
	PTR_TYPE_DECLARE(urdp_peer_connection);
	PTR_TYPE_DECLARE(trdp_peer_connection);
	PTR_TYPE_DECLARE(urdp_peer_acceptor);
	PTR_TYPE_DECLARE(trdp_peer_acceptor);

}

#endif//peer_peer_connection_base_h__