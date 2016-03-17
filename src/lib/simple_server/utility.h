#ifndef simple_server_utility_h__
#define simple_server_utility_h__

#include <string>

#include <p2engine/push_warning_option.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/filesystem/path.hpp>
#include <p2engine/p2engine.hpp>
#include <p2engine/pop_warning_option.hpp>

using namespace p2engine;

namespace p2simple
{
	///////////////////////////////////////////
	typedef boost::tuple<std::string, std::string> tuple_type;

	tuple_type title_match(const std::string& t);

	bool find_assist_file(const boost::filesystem::path& file, 
		std::vector<boost::filesystem::path>& files);
}

#endif // utility_h__
