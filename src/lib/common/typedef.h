//
// typedef.h
// ~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2008 GuangZhu Wu (GuangZhuWu@gmail.com)
//
// All rights reserved. 
//

#ifndef common_typedef_h__
#define common_typedef_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "common/config.h"
#include "common/bignumber.h"

namespace p2common{

	typedef  int16_t						message_t;
	typedef  int8_t							media_channel_id_t;
	typedef  big_number<128>				peer_id_t;
	typedef  big_number<128>				channel_id_t;
	typedef  uint32_t						peer_key_t;

	typedef  uint32_t						seqno_t;
	typedef  int32_t						timestamp_t;

	struct ip_port_t {
		uint32_t ip;
		uint16_t port;
		ip_port_t(uint32_t _ip, uint16_t _port):ip(_ip), port(_port){}
		ip_port_t():ip(0), port(0){}
		bool operator<(const ip_port_t&rhs)const
		{
			if(ip<rhs.ip)
				return true;
			else if(ip>rhs.ip)
				return false;
			return port<rhs.port;
		}
		bool operator==(ip_port_t const& rhs) const
		{
			return (ip==rhs.ip)&&(port==rhs.port);
		}
		bool operator!=(ip_port_t const& rhs) const
		{
			return (ip!=rhs.ip)||(port!=rhs.port);
		}
		template<typename EndpointType>
		EndpointType to_endpoint()const
		{
			return EndpointType(boost::asio::ip::address(boost::asio::ip::address_v4(ip)), port);
		}
		uint64_t to_uint64()const
		{
			return (uint64_t(ip)<<32)|uint64_t(port);
		}
	};

	typedef p2engine::basic_connection   message_socket;
	typedef p2engine::basic_acceptor<message_socket> message_acceptor;
	typedef p2engine::urdp_connection    urdp_message_socket;
	typedef p2engine::trdp_connection    trdp_message_socket;
	typedef p2engine::urdp_acceptor      urdp_message_acceptor;
	typedef p2engine::trdp_acceptor      trdp_message_acceptor;

	PTR_TYPE_DECLARE(message_socket);
	PTR_TYPE_DECLARE(message_acceptor);
	PTR_TYPE_DECLARE(urdp_message_socket);
	PTR_TYPE_DECLARE(trdp_message_socket);
	PTR_TYPE_DECLARE(urdp_message_acceptor);
	PTR_TYPE_DECLARE(trdp_message_acceptor);

	BOOST_STATIC_ASSERT(sizeof(message_socket::message_type)==sizeof(message_t));


#define seqno_greater			wrappable_greater<seqno_t>()
#define seqno_less				wrappable_less<seqno_t>()
#define seqno_greater_equal		wrappable_greater_equal<seqno_t>()
#define seqno_less_equal		wrappable_less_equal<seqno_t>()
#define seqno_minus				wrappable_minus<seqno_t>()

#define time_greater			wrappable_greater<timestamp_t>()
#define time_less				wrappable_less<timestamp_t>()
#define time_greater_equal		wrappable_greater_equal<timestamp_t>()
#define time_less_equal			wrappable_less_equal<timestamp_t>()
#define time_minus				wrappable_minus<timestamp_t>()

}

namespace boost{
	template<>
	struct hash<p2common::peer_id_t> {
		std::size_t operator()(const p2common::peer_id_t&id) const
		{
			BOOST_ASSERT(id.size()%sizeof(uint64_t)==0);
			uint64_t rst=0;
			const uint64_t* p=(uint64_t*)&id[0];
			size_t n=id.size()/sizeof(uint64_t);
			for (size_t i=0;i<n;++i)
				rst^=*p++;
			return hash<uint64_t>()(rst);
		}
	};
};

namespace std{
	template<>
	struct less<p2common::peer_id_t>
		: public binary_function<p2common::peer_id_t, p2common::peer_id_t, bool>
	{	// functor for operator<
		bool operator()(const p2common::peer_id_t& _Left, const p2common::peer_id_t& _Right) const
		{
			typedef boost::uint64_t uint_type;
			BOOST_ASSERT(_Left.size()%sizeof(uint_type)==0);
			const uint_type* p1=(uint_type*)_Left.begin();
			const uint_type* p2=(uint_type*)_Right.begin();
			const uint_type* p1End=(uint_type*)((char*)_Left.begin()+_Left.size());
			for(;p1<p1End;)
			{
				if (*p1!=*p2)
					return *p1<*p2;
				++p1;++p2;
			}
			return false;
		}
	};
}

//BOOST_STATIC_ASSERT(boost::is_unsigned<media_pkt_seq_t::int_type>::value);

#endif
