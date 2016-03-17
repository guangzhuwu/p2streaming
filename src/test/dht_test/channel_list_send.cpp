#include <stdlib.h>
#include <iostream>

// include libevent's header
#include <libevent/event.h>

// include libcage's header
#include <libcage/cage.hpp>
#include <boost/typeof/typeof.hpp>
#include <boost/foreach.hpp>
#include "bn.hpp"
static const int g_content_length = 1024;
boost::shared_ptr<libcage::cage> g_cage;
event  g_event;
std::string g_content_type;
char g_content[g_content_length];
int g_keep_time = 1000;
void set_cage_value()
{
	//声明内容
	p2engine::safe_buffer content_type;
	p2engine::safe_buffer_io content_typeio(&content_type);
	p2engine::safe_buffer content;
	p2engine::safe_buffer_io contentio(&content);

	//读入文件
	//std::string strFile = "ppc_chnlst.lst";
	//FILE* pFile = fopen(strFile.c_str(), "rb");
	//int read_len = 0;
	//while(true)
	//{
	//	read_len = fread(g_content, 1, 1024, pFile);
	//	contentio.write(g_content, read_len);
	//	if(read_len < 1024)
	//		break;
	//}
	//fclose(pFile);

	std::string strFileName = "ppc_chnlst_recv.lst";
	//pFile = fopen(strFileName.c_str(), "wb");
	//fwrite(p2engine::buffer_cast<const char*>(content), 1, content.size(), pFile);
	//fflush(pFile);
	//fclose(pFile);
	contentio.write(strFileName.c_str(), strFileName.size());

	content_typeio.write(g_content_type.c_str(), g_content_type.size());
	g_cage->put_channel_list(content_type, content);
}

void send_table_state(intptr_t fd, short ev, void *arg)
{
	g_cage->print_state();

	timeval tim;
	tim.tv_sec = 5;
	tim.tv_usec = 0;
	evtimer_add(&g_event, &tim);
}

void join_callback(bool bresult)
{
	if(bresult)
	{
		set_cage_value();
		std::cout<<"join sucess"<<std::endl;
		g_cage->print_state();

		evtimer_set(&g_event, &send_table_state, NULL);
		timeval tim;
		tim.tv_sec = 5;
		tim.tv_usec = 0;
		evtimer_add(&g_event, &tim);
	}
}

int main(int argc, char *argv[])
{
	g_content_type = "contenttype";

#ifdef WIN32
	// initialize winsock
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2,0), &wsaData);
#endif // WIN32

	event_init();

	g_cage = boost::shared_ptr<libcage::cage>(new libcage::cage());

	g_cage->set_global();

	if(argc >= 2)
	{
		if(!g_cage->open(PF_INET, atoi(argv[1])))
		{
			std::cout<<"open port faild"<<std::endl;
			return -1;
		}

		if(argc >= 4)
		{
			g_cage->join(boost::lexical_cast<std::string>(argv[2])
				     , atoi(argv[3]), join_callback);
		}
		else
		{
			set_cage_value();
		}

		std::cout<<"my id: "<<g_cage->get_id_str()<<"\n";
	}

	event_dispatch();
	return 0;
}