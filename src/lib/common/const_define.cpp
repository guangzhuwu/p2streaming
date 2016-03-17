#include "common/const_define.h"
#include "common/utility.h"
#include <p2engine/push_warning_option.hpp>
#include <boost/lexical_cast.hpp>
#include <p2engine/pop_warning_option.hpp>

NAMESPACE_BEGIN(p2common);

int get_max_neighbor_cnt()
{
#ifdef _MSC_VER
	return 40;//windows
#else
	static int cpu_capability=bogo_mips();
	std::cout<<"CPU bogomips: "<<cpu_capability<<" MHz\n";
	if (cpu_capability>0)
	{
		return bound(5, (cpu_capability/50)*2, 20)+1;
	}
	else
	{
#	ifdef POOR_CPU
		return 6;
#	else
		return 14;
#	endif
	}
	return 6;
#endif
}

const float P2P_VERSION = 1.1f;

const int SMOOTH_TIME = 200;
const int UNSUBCRIP_DELAY = 600;

const int SUPERVISOR_CNT = 3;//�ֲ�ʽ����̽�����������
const int MAX_PEER_CNT_PER_ROOM = 3000;
const int MAX_CLINET_UPLOAD_SPEED = 16 * 1024 * 1024 / 8;//16Mbitps
const int MIN_CLINET_UPLOAD_SPEED = 128 * 1024 / 8;

const int SUBSTREAM_CNT = 64;//��������
const int GROUP_CNT = 16;
const int GROUP_LEN = SUBSTREAM_CNT*GROUP_CNT;//group length

const int PIECE_SIZE = 1350;//(1350/188)*188;//piece��С
const int CHUNK_SIZE = ((4 * 1024 * 1024) / PIECE_SIZE)*PIECE_SIZE;//chunk��С
const int PIECE_COUNT_PER_CHUNK = CHUNK_SIZE / PIECE_SIZE;

const int PLAYING_POINT_SLOT_DURATION = 15000;//��̬ʱ������ϵ�£��ڵ㶨λ�۳�

const int FLOOD_OUT_EDGE = 3;//flood ��Ϣ�ĳ���

const int MAX_DELAY_GUARANTEE = 60000;//msec
const int MIN_DELAY_GUARANTEE = 500;//��С�ӳٱ�֤
const int MAX_BACKFETCH_CNT = 2000;

const int MAX_SUPPORT_BPS = 4 * 1024 * 1024;//Ԥ��֧�ֵ�������ʣ�4Mbit
const int MAX_PKT_BUFFER_SIZE = 32 * 1024;//packet buffer�洢�����packet����

const time_duration HUB_PEER_PEER_PING_INTERVAL = seconds(65);

//////////////////////////////////////////////////////////////////////////
//
const time_duration TRACKER_PEER_PING_INTERVAL = seconds(600);
const time_duration MEMBER_TABLE_EXCHANGE_INTERVAL = seconds(60);
const time_duration SERVER_SEED_PING_INTERVAL = seconds(6);

const int SEED_PEER_CNT = 10000;
const int MIN_SUPER_SEED_PEER_CNT = 5;
const int STREAM_NEIGHTBOR_PEER_CNT = get_max_neighbor_cnt();
const int HUB_NEIGHTBOR_PEER_CNT = 3 + SUPERVISOR_CNT;

const int DEFAULT_BACK_FETCH_DURATION = 25 * 1000;
const int DEFAULT_DELAY_GUARANTEE = 5000;//Ĭ���ӳٱ�֤
const int MAX_PUSH_DELAY = 3500;//Ĭ����������ӳ�
const int MAX_LIVE_DELAY_GUARANTEE = 30000; //ֱ��������������������ӳ����
//
//////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////
//
const time_duration TRACKER_PEER_PING_INTERVAL__INTERACTIVE = seconds(75);
const time_duration MEMBER_TABLE_EXCHANGE_INTERVAL__INTERACTIVE = seconds(20);
const time_duration SERVER_SEED_PING_INTERVAL__INTERACTIVE = seconds(5);
const int SEED_PEER_CNT__INTERACTIVE = 1000;
const int MIN_SUPER_SEED_PEER_CNT__INTERACTIVE = 5;
const int STREAM_NEIGHTBOR_PEER_CNT__INTERACTIVE = 20;
const int HUB_NEIGHTBOR_PEER_CNT__INTERACTIVE = 100 + SUPERVISOR_CNT;
const int DEFAULT_DELAY_GUARANTEE__INTERACTIVE = 2500;//Ĭ���ӳٱ�֤
const int DEFAULT_BACK_FETCH_DURATION__INTERACTIVE = 5000;
const int MAX_PUSH_DELAY__INTERACTIVE = 1000;
//
//////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////
//
const int DEFAULT_DELAY_GUARANTEE__VOD = 10000;
//
//////////////////////////////////////////////////////////////////////////


const time_duration SIMPLE_DISTRBUTOR_PING_INTERVAL = seconds(1000);
const time_duration VOD_TRACKER_SERVER_PING_INTERVAL = seconds(100);
const time_duration LIVE_TRACKER_SERVER_PING_INTERVAL = seconds(5);
const int server_super_seed_ping_interval_msec = 300;
#ifdef POOR_CPU
const int buffermap_exchange_interval_msec=800;
#else
const int buffermap_exchange_interval_msec = 500;
#endif
const int stream_peer_peer_ping_interval_msec = buffermap_exchange_interval_msec + 50;

BOOST_STATIC_ASSERT(buffermap_exchange_interval_msec <= stream_peer_peer_ping_interval_msec);
const time_duration SERVER_SUPER_SEED_PING_INTERVAL = milliseconds(server_super_seed_ping_interval_msec);
const time_duration STREAM_PEER_PEER_PING_INTERVAL = milliseconds(stream_peer_peer_ping_interval_msec);
const time_duration BUFFERMAP_EXCHANGE_INTERVAL = milliseconds(buffermap_exchange_interval_msec);

const double ALIVE_DROP_PROBABILITY = 1.0 / (3.0*3.0);//3������
const double ALIVE_GOOD_PROBABILITY = 1.0 / (2.0*2.0);//2������
const double ALIVE_VERY_GOOD_PROBABILITY = 1.0 / (1.0*1.0 + 0.2);//1������

//������Ⱦ�����á����ַ����������壬����Ҫ�޸�!!
const std::string ANTI_POLLUTION_CODE = boost::lexical_cast<std::string>(893748192398482455ULL);

const std::string AUTH_SERVER_HOST = "your_auth_server_host.com";
const std::string TRACKER_SERVER_HOST = "your_tracker_host.com";



//������
boost::optional<int>  g_delay_guarantee;//ms
boost::optional<int>  g_back_fetch_duration;//ms

boost::optional<bool> g_b_fast_push_to_player;//bool
boost::optional<bool> g_b_streaming_using_rtcp = true;
boost::optional<bool> g_b_streaming_using_rudp = true;
boost::optional<bool> g_b_tracker_using_rtcp = true;
boost::optional<bool> g_b_tracker_using_rudp = true;


//domain define
const std::string tracker_and_peer_demain = "tp";
const std::string server_and_peer_demain = "sp";
const std::string tracker_and_server_demain = "ts";
const std::string cache_tracker_demain = "dht";
const std::string cache_server_demain = "cs";
const std::string tracker_time_domain = "tts";

NAMESPACE_END(p2common)
