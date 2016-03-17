#ifndef win_service_service_h__
#define win_service_service_h__

#define WIN32_LEAN_AND_MEAN //use windows.h avoid error #error :  WinSock.h has already been included

#include <windows.h>
#include <stdlib.h>
#include <tchar.h>
#include <string>
#include <process.h>

enum{MAX_NUM_OF_PROCESS=1};

void service_main_proc();

/** Window Service **/
void install(char* pPath, char* pName);
void uninstall(char* pName);
void write_log(char* pFile, char* pMsg);
bool kill_service(char* pName);
bool run_service(char* pName);
void execute_subprocess();
void proc_monitor_thread();
bool start_process(int ProcessIndex);
void end_process(int ProcessIndex);
void attach_processNames();


void WINAPI service_main(DWORD dwArgc, LPTSTR *lpszArgv);
void WINAPI service_handler(DWORD fdwControl);

#endif //win_service_service_h__
