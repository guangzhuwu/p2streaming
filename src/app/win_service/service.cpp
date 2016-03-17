//  Service.cpp : Defines the entry point for the console application.
//
#define  _CRT_SECURE_NO_WARNINGS 1

#include "service.h"
#include "version.h"
#include "p2engine/push_warning_option.hpp"
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/format.hpp>
#include <boost/filesystem/path.hpp>
#include "p2engine/pop_warning_option.hpp"

#include "p2engine/typedef.hpp"
#include "p2engine/timer.hpp"


/** Window Service **/
const int nBufferSize = 500;
CHAR pServiceName[nBufferSize+1];
CHAR pExeFile[nBufferSize+1];
CHAR lpCmdLineData[nBufferSize+1];
CHAR pLogFile[nBufferSize+1];
bool ProcessStarted = true;

CRITICAL_SECTION		myCS;
SERVICE_TABLE_ENTRY		lpServiceStartTable[] = 
{
	{pServiceName, service_main}, 
	{NULL, NULL}
};

/*LPTSTR ProcessNames[] = { "C:\\n.exe", 
						 "C:\\a.exe", 
						 "C:\\b.exe", 
						 "C:\\c.exe" };*/
std::vector<std::string> ProcessNames;

SERVICE_STATUS_HANDLE   hServiceStatusHandle; 
SERVICE_STATUS          ServiceStatus; 
PROCESS_INFORMATION	pProcInfo[MAX_NUM_OF_PROCESS];

template<typename value_type>
static bool get_config_value(const std::string& key, value_type& value)
{
	//��config.ini
	boost::property_tree::ptree pt;
	try{
		std::vector<char> szPath;
		szPath.resize(1024);
		int nCount = GetModuleFileName(NULL, &szPath[0], MAX_PATH);
		boost::filesystem::path config_path = 
			boost::filesystem::path(std::string(&szPath[0], nCount)).parent_path();
		config_path /="config.ini";

		boost::property_tree::ini_parser::read_ini(config_path.string().c_str(), pt);
		value = pt.get<value_type>(key);
	}
	catch(const std::exception& e)
	{
		//std::cout<<e.what()<<std::endl;
		write_log(pLogFile, (char*)e.what());
		return false;
	}
	catch(...)
	{
		return false;
	}
	return true;
}

void service_main_proc()
{
	::InitializeCriticalSection(&myCS);
	// initialize variables for .exe and .log file names
	char pModuleFile[nBufferSize+1];
	DWORD dwSize = GetModuleFileName(NULL, pModuleFile, nBufferSize);
	pModuleFile[dwSize] = 0;
	if(dwSize>4 && pModuleFile[dwSize-4] == '.')
	{
		sprintf(pExeFile, "%s", pModuleFile);
		pModuleFile[dwSize-4] = 0;
		sprintf(pLogFile, "%s.log", pModuleFile);
	}
	std::string default_service_name = "p2s_Service";
	get_config_value("service.name", default_service_name);

	strcpy(pServiceName, default_service_name.c_str());

	if(_stricmp("-i", lpCmdLineData) == 0)
		install(pExeFile, pServiceName);
	else if(_stricmp("-k", lpCmdLineData) == 0)
		kill_service(pServiceName);
	else if(_stricmp("-u", lpCmdLineData) == 0)
		uninstall(pServiceName);
	else if(_stricmp("-s", lpCmdLineData) == 0)
		run_service(pServiceName);
	else
		execute_subprocess();
}

void install(char* pPath, char* pName)
{  
	SC_HANDLE schSCManager = OpenSCManager( NULL, NULL, SC_MANAGER_CREATE_SERVICE); 
	if (schSCManager==0) 
	{
		long nError = GetLastError();
		char pTemp[121];
		sprintf(pTemp, "OpenSCManager failed, error code = %d\n", nError);
		write_log(pLogFile, pTemp);
	}
	else
	{
		SC_HANDLE schService = CreateService
		( 
			schSCManager, 	/* SCManager database      */ 
			pName, 			/* name of service         */ 
			pName, 			/* service name to display */ 
			SERVICE_ALL_ACCESS,        /* desired access          */ 
			SERVICE_WIN32_OWN_PROCESS|SERVICE_INTERACTIVE_PROCESS , /* service type            */ 
			SERVICE_AUTO_START,      /* start type              */ 
			SERVICE_ERROR_NORMAL,      /* error control type      */ 
			pPath, 			/* service's binary        */ 
			NULL,                      /* no load ordering group  */ 
			NULL,                      /* no tag identifier       */ 
			NULL,                      /* no dependencies         */ 
			NULL,                      /* LocalSystem account     */ 
			NULL
		);                     /* no password             */ 
		if (schService==0) 
		{
			long nError =  GetLastError();
			char pTemp[121];
			sprintf(pTemp, "Failed to create service %s, error code = %d\n", pName, nError);
			write_log(pLogFile, pTemp);
		}
		else
		{
			char pTemp[121];
			sprintf(pTemp, "Service %s installed\n", pName);
			write_log(pLogFile, pTemp);
			CloseServiceHandle(schService); 
		}
		CloseServiceHandle(schSCManager);
	}	
}

void uninstall(char* pName)
{
	SC_HANDLE schSCManager = OpenSCManager( NULL, NULL, SC_MANAGER_ALL_ACCESS); 
	if (schSCManager==0) 
	{
		long nError = GetLastError();
		char pTemp[121];
		sprintf(pTemp, "OpenSCManager failed, error code = %d\n", nError);
		write_log(pLogFile, pTemp);
	}
	else
	{
		SC_HANDLE schService = OpenService( schSCManager, pName, SERVICE_ALL_ACCESS);
		if (schService==0) 
		{
			long nError = GetLastError();
			char pTemp[121];
			sprintf(pTemp, "OpenService failed, error code = %d\n", nError);
			write_log(pLogFile, pTemp);
		}
		else
		{
			if(!DeleteService(schService)) 
			{
				char pTemp[121];
				sprintf(pTemp, "Failed to delete service %s\n", pName);
				write_log(pLogFile, pTemp);
			}
			else 
			{
				char pTemp[121];
				sprintf(pTemp, "Service %s removed\n", pName);
				write_log(pLogFile, pTemp);
			}
			CloseServiceHandle(schService); 
		}
		CloseServiceHandle(schSCManager);	
	}
	DeleteFile(pLogFile);
}

void write_log(char* pFile, char* pMsg)
{
	// write error or other information into log file
	::EnterCriticalSection(&myCS);
	try
	{
		SYSTEMTIME oT;
		::GetLocalTime(&oT);
		FILE* pLog = fopen(pFile, "a");
		fprintf(pLog, "%02d/%02d/%04d, %02d:%02d:%02d\n    %s", oT.wMonth, oT.wDay, oT.wYear, oT.wHour, oT.wMinute, oT.wSecond, pMsg); 
		fclose(pLog);
	} catch(...) {}
	::LeaveCriticalSection(&myCS);
}

bool kill_service(char* pName) 
{ 
	// kill service with given name
	SC_HANDLE schSCManager = OpenSCManager( NULL, NULL, SC_MANAGER_ALL_ACCESS); 
	if (schSCManager==0) 
	{
		long nError = GetLastError();
		char pTemp[121];
		sprintf(pTemp, "OpenSCManager failed, error code = %d\n", nError);
		write_log(pLogFile, pTemp);
	}
	else
	{
		// open the service
		SC_HANDLE schService = OpenService( schSCManager, pName, SERVICE_ALL_ACCESS);
		if (schService==0) 
		{
			long nError = GetLastError();
			char pTemp[121];
			sprintf(pTemp, "OpenService failed, error code = %d\n", nError);
			write_log(pLogFile, pTemp);
		}
		else
		{
			// call ControlService to kill the given service
			SERVICE_STATUS status;
			if(ControlService(schService, SERVICE_CONTROL_STOP, &status))
			{
				CloseServiceHandle(schService); 
				CloseServiceHandle(schSCManager); 
				return true;
			}
			else
			{
				long nError = GetLastError();
				char pTemp[121];
				sprintf(pTemp, "ControlService failed, error code = %d\n", nError);
				write_log(pLogFile, pTemp);
			}
			CloseServiceHandle(schService); 
		}
		CloseServiceHandle(schSCManager); 
	}
	return false;
}

bool run_service(char* pName) 
{ 
	// run service with given name
	SC_HANDLE schSCManager = OpenSCManager( NULL, NULL, SC_MANAGER_ALL_ACCESS); 
	if (schSCManager==0) 
	{
		long nError = GetLastError();
		char pTemp[121];
		sprintf(pTemp, "OpenSCManager failed, error code = %d\n", nError);
		write_log(pLogFile, pTemp);
	}
	else
	{
		// open the service
		SC_HANDLE schService = OpenService( schSCManager, pName, SERVICE_ALL_ACCESS);
		if (schService==0) 
		{
			long nError = GetLastError();
			char pTemp[121];
			sprintf(pTemp, "OpenService failed, error code = %d\n", nError);
			write_log(pLogFile, pTemp);
		}
		else
		{
			// call StartService to run the service
			if(StartService(schService, 0, (const char**)NULL))
			{
				CloseServiceHandle(schService); 
				CloseServiceHandle(schSCManager); 
				return true;
			}
			else
			{
				long nError = GetLastError();
				char pTemp[121];
				sprintf(pTemp, "StartService failed, error code = %d\n", nError);
				write_log(pLogFile, pTemp);
			}
			CloseServiceHandle(schService); 
		}
		CloseServiceHandle(schSCManager); 
	}
	return false;
}


void start_check_service(void*)
{
	using namespace p2engine;
	io_service ios;
	static boost::shared_ptr<rough_timer> check_timer;
	if(!check_timer)
	{
		check_timer = rough_timer::create(ios);
		check_timer->set_obj_desc("win_service::check_timer");
		check_timer->register_time_handler(boost::bind(&proc_monitor_thread));
		check_timer->async_keep_waiting(seconds(1), seconds(1));
	}
	ios.run();
}

void execute_subprocess()
{
	if(_beginthread(start_check_service, 0, NULL) == -1)
	{
		long nError = GetLastError();
		char pTemp[121];
		sprintf(pTemp, "StartService failed, error code = %d\n", nError);
		write_log(pLogFile, pTemp);
	}
	if(!StartServiceCtrlDispatcher(lpServiceStartTable))
	{
		long nError = GetLastError();
		char pTemp[121];
		sprintf(pTemp, "StartServiceCtrlDispatcher failed, error code = %d\n", nError);
		write_log(pLogFile, pTemp);
	}
	::DeleteCriticalSection(&myCS);
}

void WINAPI service_main(DWORD dwArgc, LPTSTR *lpszArgv)
{
	DWORD   status = 0; 
    DWORD   specificError = 0xfffffff; 
 
    ServiceStatus.dwServiceType        = SERVICE_WIN32; 
    ServiceStatus.dwCurrentState       = SERVICE_START_PENDING; 
    ServiceStatus.dwControlsAccepted   = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN | SERVICE_ACCEPT_PAUSE_CONTINUE; 
    ServiceStatus.dwWin32ExitCode      = 0; 
    ServiceStatus.dwServiceSpecificExitCode = 0; 
    ServiceStatus.dwCheckPoint         = 0; 
    ServiceStatus.dwWaitHint           = 0; 
 
    hServiceStatusHandle = RegisterServiceCtrlHandler(pServiceName, service_handler); 
    if (hServiceStatusHandle==0) 
    {
		long nError = GetLastError();
		char pTemp[121];
		sprintf(pTemp, "RegisterServiceCtrlHandler failed, error code = %d\n", nError);
		write_log(pLogFile, pTemp);
        return; 
    } 
 
    // Initialization complete - report running status 
    ServiceStatus.dwCurrentState       = SERVICE_RUNNING; 
    ServiceStatus.dwCheckPoint         = 0; 
    ServiceStatus.dwWaitHint           = 0;  
    if(!SetServiceStatus(hServiceStatusHandle, &ServiceStatus)) 
    { 
		long nError = GetLastError();
		char pTemp[121];
		sprintf(pTemp, "SetServiceStatus failed, error code = %d\n", nError);
		write_log(pLogFile, pTemp);
    } 

	attach_processNames();
	for(int iLoop = 0; iLoop < MAX_NUM_OF_PROCESS; iLoop++)
	{
		pProcInfo[iLoop].hProcess = 0;
		start_process(iLoop);
	}
}

void attach_processNames()
{
	char lpszpath[nBufferSize];
	memset(lpszpath, 0x00, sizeof(lpszpath));
	DWORD dwSize = GetModuleFileName(NULL, lpszpath, nBufferSize);
	lpszpath[dwSize] = 0;
	while(lpszpath[dwSize] != '\\' && dwSize != 0)
	{
		lpszpath[dwSize] = 0; dwSize--;
	}

	ProcessNames.resize(MAX_NUM_OF_PROCESS);
	for(int iLoop = 0; iLoop < MAX_NUM_OF_PROCESS; iLoop++)
	{
		ProcessNames[iLoop].append(&lpszpath[0], strlen(lpszpath));
	}
	std::string appName;
	get_config_value("app.control_name", appName);

	BOOST_ASSERT(!appName.empty());
	ProcessNames[0].append(appName.c_str(), appName.length());
}

void WINAPI service_handler(DWORD fdwControl)
{
	switch(fdwControl) 
	{
		case SERVICE_CONTROL_STOP:
		case SERVICE_CONTROL_SHUTDOWN:
			ProcessStarted = false;
			ServiceStatus.dwWin32ExitCode = 0; 
			ServiceStatus.dwCurrentState  = SERVICE_STOPPED; 
			ServiceStatus.dwCheckPoint    = 0; 
			ServiceStatus.dwWaitHint      = 0;
			// terminate all processes started by this service before shutdown
			{
				for(int i = MAX_NUM_OF_PROCESS - 1 ; i >= 0; i--)
				{
					end_process(i);
				}			
			}
			break; 
		case SERVICE_CONTROL_PAUSE:
			ServiceStatus.dwCurrentState = SERVICE_PAUSED; 
			break;
		case SERVICE_CONTROL_CONTINUE:
			ServiceStatus.dwCurrentState = SERVICE_RUNNING; 
			break;
		case SERVICE_CONTROL_INTERROGATE:
			break;
		default:
			if(fdwControl>=128&&fdwControl<256)
			{
				int nIndex = fdwControl&0x7F;
				// bounce a single process
				if(nIndex >= 0 && nIndex < MAX_NUM_OF_PROCESS)
				{
					end_process(nIndex);
					start_process(nIndex);
				}
				// bounce all processes
				else if(nIndex==127)
				{
					for(int i = MAX_NUM_OF_PROCESS-1; i >= 0; i--)
					{
						end_process(i);
					}
					for(int i = 0; i < MAX_NUM_OF_PROCESS; i++)
					{
						start_process(i);
					}
				}
			}
			else
			{
				char pTemp[121];
				sprintf(pTemp,  "Unrecognized opcode %d\n", fdwControl);
				write_log(pLogFile, pTemp);
			}
	};
    if (!SetServiceStatus(hServiceStatusHandle,  &ServiceStatus)) 
	{ 
		long nError = GetLastError();
		char pTemp[121];
		sprintf(pTemp, "SetServiceStatus failed, error code = %d\n", nError);
		write_log(pLogFile, pTemp);
    } 
}
bool start_process(int ProcessIndex)
{
	//[service]
	//name=xxxx
	//[app]
	//control_name=xxxx
	//name=xxxx
	//param_cnt=xxxx
	//[param_1]
	//name=xxxx
	//value=xxxx
	//[param_2]
	//name=xxxx
	//value=xxxx

	int param_cnt=0;
	get_config_value("app.param_cnt", param_cnt);
	std::map<std::string, std::string> param_map;
	std::vector<char> DstBuf;
	DstBuf.resize(10, 0);

	std::string tparam("param_");
	std::string name_key, name_val;
	std::string val_key, val_val;
	for (int i=0;i<param_cnt;++i)
	{
		std::string key=tparam+itoa(i+1, &DstBuf[0], 10);
		name_key=key+".name";
		val_key=key+".value";
		get_config_value(name_key, name_val);
		get_config_value(val_key, val_val);
		param_map.insert(std::make_pair(name_val, val_val));
	}
	std::string param;
	std::vector<char> tmp;
	tmp.resize(nBufferSize);

	for (BOOST_AUTO(itr, param_map.begin());
		itr!=param_map.end();++itr)
	{
		memset(&tmp[0], 0, nBufferSize);
		sprintf(&tmp[0], " --%s=%s", itr->first.c_str(), itr->second.c_str());
		param.append(&tmp[0], strlen(&tmp[0]));
	}

	STARTUPINFO startUpInfo = { sizeof(STARTUPINFO), NULL, "", NULL, 0, 0, 0, 0, 0, 0, 0, STARTF_USESHOWWINDOW, 0, 0, NULL, 0, 0, 0};  
	startUpInfo.wShowWindow = SW_HIDE;
	startUpInfo.lpDesktop = NULL;
	std::string AppParam = ProcessNames[ProcessIndex];
	AppParam.append(param.c_str(), param.length());

	if(CreateProcess(NULL, (LPSTR)AppParam.c_str(), NULL, NULL, false, NORMAL_PRIORITY_CLASS, \
		NULL, NULL, &startUpInfo, &pProcInfo[ProcessIndex]))
	{
		Sleep(1000);
		return true;
	}
	else
	{
		long nError = GetLastError();
		char pTemp[256];
		sprintf(pTemp, "Failed to start program '%s', error code = %d\n", ProcessNames[ProcessIndex], nError); 
		write_log(pLogFile, pTemp);
		return false;
	}
}


void end_process(int ProcessIndex)
{
	if(ProcessIndex >=0 && ProcessIndex <= MAX_NUM_OF_PROCESS)
	{
		if(pProcInfo[ProcessIndex].hProcess)
		{
			// post a WM_QUIT message first
			PostThreadMessage(pProcInfo[ProcessIndex].dwThreadId, WM_QUIT, 0, 0);
			Sleep(1000);
			// terminate the process by force
			TerminateProcess(pProcInfo[ProcessIndex].hProcess, 0);
		}
	}
}

void proc_monitor_thread()
{
	if(!ProcessStarted)
		return;

	DWORD dwCode;
	for(int iLoop = 0; iLoop < MAX_NUM_OF_PROCESS; iLoop++)
	{
		if(::GetExitCodeProcess(pProcInfo[iLoop].hProcess, &dwCode) && pProcInfo[iLoop].hProcess != NULL)
		{
			if(dwCode != STILL_ACTIVE)
			{
				if(start_process(iLoop))
				{
					char pTemp[121];
					sprintf(pTemp, "Restarted process %d\n", iLoop);
					write_log(pLogFile, pTemp);
				}
			}
		}
	}
}


int _tmain(int argc, _TCHAR* argv[])
{
	if(argc >= 2)
		strcpy(lpCmdLineData, argv[1]);

	if(_stricmp("-version", lpCmdLineData) == 0 
		|| _stricmp("-v", lpCmdLineData) == 0)
	{
		printf("win_service, version "WIN_SERVICE_VERSION);
		return 0;
	}

	service_main_proc();
	return 0;
}