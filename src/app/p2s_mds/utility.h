#ifndef p2s_mds_utility_h__
#define p2s_mds_utility_h__

#include "p2s_mds/auth.h"
#include "p2s_mds/progress_alive_alarm.h"
#include "p2s_mds/media_server.h"

#include <p2engine/push_warning_option.hpp>
#include <string>
#include <map>
#include <vector>
#include <boost/shared_ptr.hpp>
#include <boost/property_tree/ptree.hpp>   
#include <boost/property_tree/xml_parser.hpp>
#include <p2engine/pop_warning_option.hpp>

namespace utility{
	using boost::property_tree::ptree; 
	typedef std::vector<server_param_base> channel_vec_type;

	typedef boost::unordered_map<std::string, boost::shared_ptr<p2s_mds> >  server_map;
	typedef boost::unordered_map<std::string, boost::shared_ptr<progress_alive_alarm> > alarm_map;
	typedef boost::unordered_map<std::string, uint32_t> channel_alive_map;
	typedef channel_alive_map::value_type alive_value_type;
	typedef server_map::value_type server_value_type;
	typedef alarm_map::value_type alarm_value_type;

	std::string zero_port(const std::string& ipport);//bind port 0
};

#endif // utility_h__
