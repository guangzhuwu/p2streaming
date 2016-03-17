#include "p2s_mds/auth.h"
#include "p2s_mds/media_server.h"
#include "p2s_mds/utility.h"

#include <p2engine/push_warning_option.hpp>
#include <string>
#include <map>
#include <vector>
#include <boost/shared_ptr.hpp>
#include <boost/property_tree/ptree.hpp>   
#include <boost/property_tree/xml_parser.hpp>
#include <p2engine/utf8.hpp>
#include <p2engine/pop_warning_option.hpp>

const std::string server_key = "server";
const std::string server_id_key = "id";
const std::string server_prop_key = "prop";
const std::string channel_count_key = "count";

const std::string channel_key = "channel";
const std::string channel_id_key = "uuid";
const std::string channel_name_key = "name";
const std::string channel_prop_key = "prop";

const std::string channel_type_key = "type";
const std::string channel_path_key = "path";
const std::string channel_stream_recv_url_key = "stream_recv_url";
const std::string channel_in_addr_key = "internal_address";
const std::string channel_ex_addr_key = "external_address";
const std::string channel_tracker_addr_key = "tracker_address";
const std::string channel_key_key = "channel_key";
const std::string channel_link_key = "channel_link";

const std::string channel_duration_key = "duration";
const std::string channel_length_key  = "length";

using boost::property_tree::ptree; 
namespace utility{

	//bind port 0
	std::string zero_port(const std::string& ipport)
	{
		std::string::size_type pos=ipport.find(':');
		if (pos==std::string::npos)
			return ipport + ":0";
		return ipport.substr(0, pos) + ":0";
	}
};