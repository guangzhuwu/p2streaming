#ifndef process_killer_h__
#define process_killer_h__

#include <p2engine/config.hpp>

#include <p2engine/push_warning_option.hpp>
#include <boost/unordered_set.hpp>
#include <p2engine/pop_warning_option.hpp>

namespace p2control{

#if defined(BOOST_WINDOWS_API) 
	typedef DWORD  pid_t;
#endif 
	typedef boost::unordered_set<pid_t> pid_set;

	void kill_all_process(const std::string& name);
	bool kill_process_by_id(pid_t processID, bool force=true); 
	void find_process_ids(pid_set&, const std::string&);

	const std::string get_current_process_name();
	void clear_instance_exist();
	void close_process(const std::string& app_name);
	std::string string_native(const std::string&);
	bool instance_check(pid_t pid);
};

#endif // process_killer_h__
