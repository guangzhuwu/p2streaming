#define  _CRT_SECURE_NO_WARNINGS
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <windows.h>
#include "livestreaming/livestreaming_interface.h"
#include "SimpleIni.h"

int out=false;
namespace{
	static struct just_for_srand
	{
		just_for_srand()
		{
			srand(GetTickCount());
		}
	}__just_for_srand;

	int flv_port=rand()%(65535-1000)+1000;

	void LaunchVlcPlayer(const char *vlcPath)
	{
#	ifdef _WIN32
		char cmd[512];
		sprintf(cmd,
			"%s udp://@127.0.0.1:%u  :udp-caching=%u", 
			vlcPath, flv_port, 1200
			);
		PROCESS_INFORMATION pi;
		STARTUPINFO si;
		ZeroMemory( &si, sizeof(si) );
		si.cb = sizeof(si);
		ZeroMemory( &pi, sizeof(pi) );
		if( !CreateProcess( NULL, cmd, NULL, NULL, FALSE, 
			0, NULL, NULL, &si, &pi))
		{
			system(cmd);
		}
#	endif
	}
}

class ppshell
	:public client_interface
{
	SOCKET webserver_;
public:
	ppshell(const char* vlc=NULL)
	{
		webserver_=::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

		std::string vlcPath="C:\\Program Files\\VideoLAN\\VLC\\vlc.exe";
		FILE *fp=fopen(vlcPath.c_str(),"r");
		if (!fp)
		{
			vlcPath="C:\\Program Files (x86)\\VideoLAN\\VLC\\vlc.exe";
			fp=fopen(vlcPath.c_str(),"r");
			if(!fp)
			{
				vlcPath="vlc\\vlc.exe";
				fp=fopen(vlcPath.c_str(),"r");
			}
		}
		if(fp)
			fclose(fp);
		else if(vlc)
			vlcPath=vlc;
		else
			vlcPath="vlc.exe";
		vlcPath="\""+vlcPath+"\"";
		LaunchVlcPlayer(vlcPath.c_str());

	}
	void start(int vlcPort=0)
	{
		if (vlcPort!=0)
			flv_port=vlcPort;
		
		std::string tHost;
		int tPort=0;
		int delay_guarantee_=0;
		bool hasIni=true;
		std::string channelUUID="0000000000000001";
		CSimpleIni ini_;
		if(ini_.LoadFile("config.ini")>=0)
		{
			const char * pszValue = ini_.GetValue("tracker_address", "ip", NULL /*default*/);
			if (pszValue)
				tHost=std::string(pszValue);
			pszValue = ini_.GetValue("tracker_address", "port", NULL /*default*/);
			if (pszValue)
				tPort=atoi(pszValue);
			pszValue = ini_.GetValue("delay_guarantee", "msec", NULL /*default*/);
			if (pszValue)
			{
				int t=atoi(pszValue);
				if (t>0)
				{
					delay_guarantee_=t;
				}
			}
			pszValue = ini_.GetValue("channel_id", "id", NULL /*default*/);
			if (pszValue)
				channelUUID=pszValue;
		}

		if(tPort==0)
			tPort=49168;
		if(delay_guarantee_==0)
			delay_guarantee_=g_delay_guarantee;
		if (tHost.empty())
			tHost="127.0.0.1";

		g_delay_guarantee=delay_guarantee_;
		if (!hasIni)
		{
			ini_.SetValue("tracker_address", "ip","127.0.0.1");
			ini_.SetLongValue("tracker_address", "port",tPort);
			ini_.SetLongValue("delay_guarantee", "msec",delay_guarantee_);
			ini_.SetLongValue("channel_id", "id",delay_guarantee_);
			ini_.SaveFile("config.ini");
		}

		client_interface::start(channelUUID,tHost,tPort);
	}
public:
	/************************************************************************/
	/*登录与在线状态相关                                                    */
	/************************************************************************/
	//登录失败
	virtual void on_login_failed(const std::string& errorMsg)
	{
		std::cout<<"login_failed: "<<errorMsg<<std::endl;
		out=true;
	}
	//登录成功
	virtual void on_login_success()
	{
		std::cout<<"login_success"<<std::endl;
	}
	//意外掉线
	virtual void on_droped()
	{
		std::cout<<"on_droped"<<std::endl;
		out=true;
	}

	//一个新节点加入系统
	virtual void on_join_new_peer(peer_id_t newPeerID, const std::string& userInfo)
	{
		std::cout<<"on_join_new_peer"<<std::endl;
	}
	//发现了一个早于本节点在线的节点
	virtual void on_known_online_peer(peer_id_t newPeerID, const std::string& userInfo)
	{
		std::cout<<"on_add_new_peer"<<std::endl;
	}
	//节点离开
	virtual void on_known_offline_peer(peer_id_t newPeerID)
	{
		std::cout<<"on_remove_peer"<<std::endl;
	}

	/************************************************************************/
	/* 媒体流相关                                                           */
	/************************************************************************/
	//收到媒体流
	virtual void on_recvd_media(const void*data,size_t len, peer_id_t srcPeerID,
		media_channel_id_t mediaChannelID)
	{
		if (len<1000)
		{
			//std::cout<<"on_recvd_media len="<<len<<std::endl;
		}
		sockaddr_in recvAddr;
		recvAddr.sin_family = AF_INET;
		recvAddr.sin_port = htons(flv_port);
		recvAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

		sendto(webserver_,(const char *)data, len, 0, (sockaddr *) &recvAddr, sizeof(recvAddr));
	}

	virtual void  on_recvd_unordered_media(const void*data,size_t len, 
		peer_id_t srcPeerID, media_channel_id_t mediaChannelID)
	{
	}

};

void main(int argc, char **argv)
{
	//boost::cli::commands_description desc;
	//desc.add_options()
	//	("flood", po::value< std::vector<std::string> >()->notifier(&flood)->multitoken(),"对大家公聊发送消息，如: flood 有GG陪小妹妹聊聊么？")
	//	("floodto", po::value< std::vector<std::string> >()->notifier(&flood_to)->multitoken(),"对xx公聊发送消息，如: floodto 1 陪小妹妹聊聊好么？")
	//	("sendto", po::value< std::vector<std::string> >()->notifier(&send_to)->multitoken(),"对xx私聊发送消息，如: sendto 1 陪小妹妹聊聊好么？")
	//	("nickname", po::value< std::vector<std::string> >()->notifier(&change_nickname)->multitoken(),"改名，如: nickname 五花肉")
	//	("exit", po::value< unsigned int >()->notifier(&exit_))
	//	;
	//std::cout<<"NAT detecting..."<<std::endl;

	////boost::asio::ip::udp::endpoint serveEndpoint(boost::asio::ip::address_v4::loopback(), 433);
	////void start(const std::string& channelUUID,const std::string& trackerHost,std::string& nickName);
	//std::string channelUUID("0000000000000001");
	//std::string trackerHost("219.236.18.13");
	//std::string nickName("9527");
	//chatroom.start(channelUUID,trackerHost,nickName);

	//boost::cli::command_line_interpreter cli(desc, ">");
	//cli.interpret(std::cin);

	try{
		std::string vlcPath;
		ppshell c(NULL);//(argc>1)?argv[1]:NULL);//这是一个线程，非阻塞
		int vlcPort=(argc>1)?atoi(argv[1]):0;
		c.start(vlcPort);//这是一个线程，非阻塞
		for(;!out;)
		{
			Sleep(10);
		}
		//c.stop();
		Sleep(100);
	}
	catch(std::exception e)
	{
		std::cout<<"exception:"<<e.what()<<std::endl;
	}
	catch(...)
	{
	}
	system("pause");

	//Sleep(10000);
	//for (;;)
	//{
	//	std::string s;
	//	std::cout<<">>";
	//	std::cin>> s;
	//	std::string msg=std::string("[")+boost::lexical_cast<std::string>(c.my_id())
	//		+"] 对大家说："+s;
	//	c.broadcast_message(msg.c_str(),msg.length());
	//}
}