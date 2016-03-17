#include <string>
#include <map>
#include <set>
#include <iostream>
#include <ctype.h> 
#include <time.h>
#include <stdlib.h>

static struct _pause_exit 
{
	~_pause_exit()
	{
		system("pause");
	}
}_pause_exit;

# if 0
#include <Windows.h>
int change_time()
{
	HKEY hKey;

	LPCSTR reg_key = "Interface\\{7FC8EFD9-88F1-4B46-A09D-17EBCA3EFBA7}\\ProxyStubClsid32";

	if ( RegOpenKeyEx (HKEY_CLASSES_ROOT, reg_key, 0, KEY_WRITE, &hKey) )
	{
		return -1;
	}

	SYSTEMTIME time;
	double t;

	// Get Current Local Time
	GetLocalTime(&time);
	SystemTimeToVariantTime(&time, &t);

	// Add 30 Days
	t +=45;

	unsigned char *p = (unsigned char *)&t;

	// Calculate
	unsigned int m0 = *p * *(p+1) * *(p+2) * *(p+3);
	unsigned int m1 = *(p+4) * *(p+5);
	unsigned int m2 = *(p+6) * *(p+7);

	char key[39];

	sprintf(key, "{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", m0, m1, m2, *p, *(p+1), *(p+2), *(p+3), *(p+4), *(p+5), *(p+6), *(p+7));

	RegSetValueEx(hKey, 0, 0, REG_SZ, (unsigned char *)key, sizeof(key));
	RegFlushKey(hKey);
	return RegCloseKey (hKey);
}

void main()
{
	change_time();
}

#else

#include "fssignal.hpp"

using namespace p2engine;
void g()
{
	std::cout<<"g()"<<std::endl;
}

class udp_connection
	:public fssignal::trackable
{
public:
	udp_connection(){
		signal_.bind(&udp_connection::q,this);
		signal_.bind(&udp_connection::f,this);
		signal_.bind(&udp_connection::f,this);
		signal_.bind(&udp_connection::f,this);
		signal_.bind(&g);
	}
	~udp_connection(){
		std::cout<<"~udp_connection()"<<std::endl;
	}
	void q()
	{
		delete this;
	}
	void f()
	{
		signal_();
		std::cout<<"f()"<<std::endl;
	}

private:
	fssignal::signal<void()> signal_;
};
#include <vector>
void main()
{
	char a=1;
	int b=(a<<8);
	std::cout<<b<<std::endl;
}



#endif
