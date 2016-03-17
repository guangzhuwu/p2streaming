#ifndef UPNP_AUTO_MAPPING_H__
#define UPNP_AUTO_MAPPING_H__

#include <p2engine/p2engine.hpp>

#include <p2engine/push_warning_option.hpp>
#include <utility>
#include <p2engine/pop_warning_option.hpp>

namespace natpunch {
	
	void start_auto_mapping(p2engine::io_service&);
	std::pair<int, int> get_udp_mapping();
	std::pair<int, int> get_tcp_mapping();

}  // namespace natpunch

#endif  // UPNP_AUTO_MAPPING_H__
