#include "p2s_mds_control/utility.h"
#include <p2engine/utf8.hpp>

namespace utility{
	node_map_type hash_node_map(const int& max_process, channel_vec_type& channels)
	{
		int port_sum=0;
		node_map_type server_node_map;
		for (size_t i=0; i<channels.size(); ++i)
		{
			BOOST_AUTO(&link, channels[i].channel_link);
			node_id_t node = 1+hash_to_node(link.c_str(), link.size(), max_process);
			channel_vec_type & channel_list = server_node_map[node];
			channel_list.push_back(channels[i]);
		}
		return server_node_map;
	}
}