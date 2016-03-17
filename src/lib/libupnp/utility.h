#ifndef utility_h__
#define utility_h__

#include "p2engine/push_warning_option.hpp"
#include <boost/cstdint.hpp>
#include <boost/tuple/tuple.hpp>
#include "p2engine/pop_warning_option.hpp"

#include <p2engine/typedef.hpp>

namespace libupnp
{
	typedef boost::int64_t  size_type;
	typedef boost::uint64_t unsigned_size_type;

	typedef p2engine::address address;
	typedef p2engine::address_v4 address_v4;
	typedef p2engine::address_v6 address_v6;

	typedef boost::asio::ip::tcp tcp;
	typedef boost::asio::ip::udp udp;
	typedef p2engine::error_code error_code;

}

#endif // utility_h__
