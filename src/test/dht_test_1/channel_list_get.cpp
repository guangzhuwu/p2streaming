#include <stdlib.h>
#include <iostream>

// include libevent's header
#include <libevent/event.h>

// include libcage's header
#include <libcage/cage.hpp>
#include <boost/typeof/typeof.hpp>
#include <boost/foreach.hpp>
static const int g_content_length = 1024;
boost::shared_ptr<libcage::cage> g_cage;
event  g_event;
event  g_delay_event;
std::string g_content_type;
char g_content[g_content_length];
int g_keep_time = 1000;

void nat_state_callback(intptr_t fd, short ev, void *arg)
{
	std::cout<<"nat type:";
	switch(g_cage->get_nat_state())
	{
	case libcage::node_undefined:
		std::cout<<"node_undefined";
		break;
	case libcage::node_nat:
		std::cout<<"node_nat";
		break;
	case libcage::node_cone:
		std::cout<<"node_cone";
		break;
	case libcage::node_symmetric:
		std::cout<<"node_symmetric";
		break;
	case libcage::node_global:
		std::cout<<"node_global";
		break;
	}
	std::cout<<"\n";

	timeval tim;
	tim.tv_sec = 5;
	tim.tv_usec = 0;
	evtimer_add(&g_event, &tim);
	g_cage->print_state();
}


void get_channel_list_callback(p2engine::safe_buffer channellist)
{
	p2engine::safe_buffer_io bufio(&channellist);
	//std::string str;
	//bufio>>str;
	//std::cout<<str<<std::endl;

	//保存到文件
	std::string strFileName = "ppc_chnlst_recv.lst";
	FILE* pFile = fopen(strFileName.c_str(), "wb");
	fwrite(p2engine::buffer_cast<const char*>(channellist), 1, channellist.size(), pFile);
	fflush(pFile);
	fclose(pFile);
	std::cout<<"receive channel list end\n";
}

void delay_get_value_callback(intptr_t fd, short ev, void *arg)
{
	p2engine::safe_buffer contenttype;
	p2engine::safe_buffer_io contenttypeio(&contenttype);
	contenttypeio.write(g_content_type.c_str(), g_content_type.size());

	g_cage->get_channel_list(contenttype, get_channel_list_callback);
}

void join_callback(bool bresult)
{
	if(bresult)
	{
		timeval tim;
		tim.tv_sec = 5;
		tim.tv_usec = 0;

		evtimer_set(&g_delay_event, delay_get_value_callback, NULL);
		evtimer_add(&g_delay_event, &tim);
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

	//g_cage->set_global();

	if(argc >= 4)
	{
		if(!g_cage->open(PF_INET, atoi(argv[1])))
		{
			std::cout<<"open port faild"<<std::endl;
			return -1;
		}
		g_cage->join(boost::lexical_cast<std::string>(argv[2]),
			 atoi(argv[3]), join_callback);
	}

	evtimer_set(&g_event, nat_state_callback, NULL);

	timeval ti;
	ti.tv_sec = 0;
	ti.tv_usec = 0;
	evtimer_add(&g_event, &ti);
	event_dispatch();
	return 0;
}