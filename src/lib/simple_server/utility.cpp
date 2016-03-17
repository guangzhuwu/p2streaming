#include "simple_server/utility.h"
#include "common/utility.h"
#include <p2engine/push_warning_option.hpp>
#include <boost/regex.hpp>
#include <boost/filesystem.hpp>
#include <p2engine/pop_warning_option.hpp>
#include "p2engine/p2engine.hpp"


using namespace p2engine;
using namespace p2common;

NAMESPACE_BEGIN(p2simple);

tuple_type title_match(const std::string& title)
{
	typedef boost::filesystem::path path;
	BOOST_ASSERT(!boost::filesystem::is_directory(path(title)));
	BOOST_ASSERT(!boost::filesystem::path(title).extension().empty());

	std::string regStr("(\\d+)\\.(.+)");
	const boost::regex titleReg(regStr);

	boost::smatch what;
	if(!boost::regex_match(title, what, titleReg))
		return tuple_type("", "");

	return tuple_type(what[1], what[2]);
}

bool find_assist_file(const boost::filesystem::path& file, 
							 std::vector<boost::filesystem::path>& files)
{
#ifdef WINDOWS_OS
	const std::string& title = file.filename().string(); 
	std::string stem=file.stem().string();

	tuple_type title_info = title_match(title);
	if(stem!=boost::tuples::get<0>(title_info)) //不是正确的命名
		return false;

	std::string ext = boost::tuples::get<1>(title_info);

	std::vector<boost::filesystem::path> channel_files;
	search_files(file.parent_path(), ext, channel_files);

	files.reserve(channel_files.size());
	for (BOOST_AUTO(itr, channel_files.begin()); 
		itr!=channel_files.end(); ++itr)
	{
		stem=(*itr).stem().string();
		title_info = title_match((*itr).filename().string());

		if(stem!=boost::tuples::get<0>(title_info) 
			|| file == *itr)
			continue;

		files.push_back(*itr);
	}
	return true;
#endif
	return false;
}


NAMESPACE_END(p2simple);
