#include "common/utility.h"
#include "app_common/process_killer.h"
#include <p2engine/p2engine.hpp>

#ifdef BOOST_WINDOWS_API
#	include <windows.h>    
#	include <tlhelp32.h>
#else
#	define  _getpid getpid
#endif 

NAMESPACE_BEGIN(p2control);

using namespace  p2engine;

void kill_all_process(const std::string& exeName)
{
	pid_set outProcessIDsContainer;      
	find_process_ids(outProcessIDsContainer, exeName);   

	for(BOOST_AUTO(itr, outProcessIDsContainer.begin());
		itr!=outProcessIDsContainer.end();++itr)
	{
		kill_process_by_id(*itr);
	}
}

void find_process_ids(pid_set& outProcessIDsContainer, const std::string& in_processName)      
{  
#if defined(BOOST_WINDOWS_API)
	PROCESSENTRY32 processInfo;      
	processInfo.dwSize = sizeof(processInfo);      

	HANDLE processesSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);      
	if (processesSnapshot == INVALID_HANDLE_VALUE)
		return;      

	//Process First in snapshot and traverse the list.      
	Process32First(processesSnapshot, &processInfo);      
	if (!in_processName.compare(processInfo.szExeFile))
		outProcessIDsContainer.insert(processInfo.th32ProcessID);      

	while ( Process32Next(processesSnapshot, &processInfo) )      
	{      
		if ( !in_processName.compare(processInfo.szExeFile) ) 
			outProcessIDsContainer.insert(processInfo.th32ProcessID);         
	}      
	CloseHandle(processesSnapshot);    

#elif defined(BOOST_POSIX_API) 
	enum{READ_BUF_SIZE=1024};
	DIR* dir = opendir("/proc");
	if (!dir)
	{
		//fprintf(stderr, "Cannot open /proc\n");
		return ;
	}
	struct dirent *next=NULL;
	while ((next = readdir(dir)) != NULL)
	{

		char filename[READ_BUF_SIZE];
		char buffer[READ_BUF_SIZE];
		char name[READ_BUF_SIZE];

		/* Must skip ".." since that is outside /proc */
		if (strcmp(next->d_name, "..") == 0)
			continue;

		/* If it isn't a number, we don't want it */
		if (!isdigit(*next->d_name))
			continue;

		sprintf(filename, "/proc/%s/status", next->d_name);

		FILE *status=NULL;
		if (! (status = fopen(filename, "r")) )
			continue;
		//Read first line in /proc/?pid?/status
		if (fgets(buffer, READ_BUF_SIZE-1, status) == NULL)
		{
			fclose(status);
			continue;
		}
		fclose(status);


		// Buffer should contain a string like "Name: binary_name"a
		sscanf(buffer, "%*s %s", name);
		if ( name != NULL && name[0] != '\0')
		{
			if (in_processName==name)
				outProcessIDsContainer.insert(strtol(next->d_name, NULL, 0));
		}
	}
	closedir(dir);
#endif
} 

bool  kill_process_by_id(pid_t processID, bool force)   
{   
#if defined(BOOST_POSIX_API) 
	if (::kill(processID, force ? SIGKILL : SIGTERM) == -1) 
		return false;

#elif defined(BOOST_WINDOWS_API) 
	HANDLE h = ::OpenProcess(PROCESS_TERMINATE, FALSE, processID); 
	if (h == NULL) 
		return false;

	if (!::TerminateProcess(h, EXIT_FAILURE)) 
	{ 
		::CloseHandle(h); 
		return false;
	} 

	if (!::CloseHandle(h)) 
		return false;
#endif 
	system_time::sleep_millisec(100);   
	return true;
} 

const std::string get_current_process_name()
{
	enum{max_full_name_len=1024};
	char app_name[max_full_name_len+1] = " ";
	bool good=false;
#ifdef BOOST_WINDOWS_API
	GetModuleFileName(NULL, app_name, max_full_name_len);
	return boost::filesystem::path(app_name).filename().string();
#elif defined(__linux)
	good=(readlink ("/proc/self/exe", app_name, max_full_name_len)>0);
#elif defined(MAC_OS)
	unsigned long size = max_full_name_len;
	good=(_NSGetExecutablePath (&result [0], &size)==0);
#else 
#	error "how to get_current_process_name?"
#endif
	if( good )
		return std::string( app_name );
	return std::string("");
}

void clear_instance_exist()
{
	//interprocess 在后来版本已经修改了detail这个namespace的名称
	//pid_t pid = boost::interprocess::detail::get_current_process_id();
	pid_t pid = _getpid();

	const std::string exeName = get_current_process_name(); 
	BOOST_ASSERT( !exeName.empty() );
	pid_set outProcessIDsContainer;   
	find_process_ids(outProcessIDsContainer, exeName);
	outProcessIDsContainer.erase(pid);

	BOOST_AUTO(itr, outProcessIDsContainer.begin());
	for (;itr!=outProcessIDsContainer.end();++itr)
	{
		kill_process_by_id(*itr);
	}
}

//通过为true，否则false
bool instance_check(pid_t pid)
{
	int selfID=_getpid();
	if(pid==selfID)
		return true;  //就是自己

	pid_set pids;
	find_process_ids(pids, get_current_process_name());
	if(pids.end()!=pids.find(pid))
	{
		DEBUG_SCOPE(
			std::cout<<"EEEEEEEEEEEEEEEEEE----confict pid "<<pid<<" with "<<selfID<<std::endl;
			);
		return false;
	}

	return true;
}

NAMESPACE_END(p2control);

