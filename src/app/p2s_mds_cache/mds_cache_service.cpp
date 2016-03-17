#include "p2s_mds_cache/mds_cache_service.h"
#include <time.h>
#include <boost/filesystem.hpp>

using namespace p2cache;

void mds_cache_service::register_message_handler(peer_connection_sptr conn)
{
	simple_distributor::register_message_handler(conn);
#define REGISTER_HANDLER(msgType, handler)\
	BOOST_ASSERT(conn->received_signal(msgType).size()==0);\
	conn->register_message_handler(msgType, boost::bind(&this_type::handler, this, conn.get(), _1);

	REGISTER_HANDLER(global_msg::cache_media, on_recvd_media_packet);

#undef REGISTER_HANDLER
}

void mds_cache_service::start(peer_info& local_info)
{
	std::string domain=cache_server_demain+"/MEMORY_CACHE";
	__start(domain, local_info);

	log_thread_.reset(new boost::thread(boost::bind(&this_type::write_log, this)));

	{
		std::vector<char> szPath;
		szPath.resize(1024);
		int nCount = GetModuleFileName(NULL, &szPath[0], MAX_PATH);
		boost::filesystem::path dir=boost::filesystem::path(std::string(&szPath[0], nCount)).parent_path();
		dir /="hitted_ratio.log";
		path_=dir.string();
	}
}

void mds_cache_service::write_log()
{
	while(1)
	{	//����ģʽ�£���־�ļ�û�д�����������߳��м�¼
		boost::this_thread::sleep(boost::posix_time::milliseconds(60000));
		{
			static const int BUFFER_SIZE=2048;
			FILE* fp=fopen(path_.c_str(), "a+");
			if (fp)
			{
				p2engine::scoped_lock<fast_mutex> lock(mutex_);

				time_t t=time(NULL);
				tm* lt=localtime(&t);
				const char* st=asctime(lt);
				int pid=getpid();
				char log[BUFFER_SIZE];
				memset(log, 0, BUFFER_SIZE);
				
				std::string messge="[mds_cache_service][pid:%d][time:%s]\n";
				fprintf(fp, messge.c_str(), pid, st);

				messge="---------hitted_ratio=%f, load_factor=%f--------------\n";
				memset(log, 0, BUFFER_SIZE);
				fprintf(fp, messge.c_str(), hitted_ratio_, lru_cache_.load_factor());
				
				fflush(fp);
				fclose(fp);
			}
		}
	}
}

void mds_cache_service::read_request_media(
peer_connection_sptr conn, const media_request_msg& msg)
{
	error_code ec;
	safe_buffer buf;
	std::string channelID=msg.channel_id();
	std::string peerID=msg.peer_id();
	
	seqno_set& missSet=miss_map_[channelID];
	missSet.reserve(missSet.size()+msg.seqno_size());
	st_key seqKey(channelID, 0);
	DEBUG_SCOPE(bool hasPiece=false;);

	//�����ۼ�������
	static uint64_t     hit_cnt=0;
	static uint64_t     hitted_cnt=0;
	static timestamp_t  last_log_time=-1;
	for (int i=0; i<msg.seqno_size(); ++i)
	{
		seqno_t seqno= msg.seqno(i);
		seqKey.seqno=seqno;
		hit_cnt++;

		if(lru_cache_.read(seqKey, &buf))
		{
			//send to media server
			__send_cache_media(conn, peerID, seqno, buf);
			hitted_cnt++;

			DEBUG_SCOPE(hasPiece=true;);
		}
		else
			missSet.push_back(seqno);
	}
	
	timestamp_t now=timestamp_now();
	if(is_time_passed(60000, last_log_time, now))
	{
		p2engine::scoped_lock<fast_mutex> lock(mutex_);

		last_log_time=now;
		hitted_ratio_=double(hitted_cnt)/hit_cnt;
		LogInfo("-----hitted ratio=%f\n", hitted_ratio_);
	}

	DEBUG_SCOPE(
		if(hasPiece&&in_probability(0.005))
		{
			std::cout<<"SSSSSSSSSSSSSSSSSS----send cached seqmin="
			<<msg.seqno(0)
			<<" seqmax="
			<<msg.seqno(msg.seqno_size()-1)
			<<"------------\n";
		}
	);

	if(!missSet.empty())
	{	
		report_missing_pieces(conn.get(), channelID, msg.peer_id(), missSet);
		missSet.clear();
	}
}

void mds_cache_service::on_recvd_media_packet(
	peer_connection* conn, safe_buffer buf)
{
	__on_recvd_cached_media(buf, boost::bind(
		&this_type::write_cache_media_packet, SHARED_OBJ_FROM_THIS, _1, _2, _3)
		);
}

void mds_cache_service::write_cache_media_packet(
	const std::string& channelID, seqno_t seqno, const safe_buffer& buf)
{
	st_key Key(channelID, seqno);
	if(!lru_cache_.find(Key))
	{
		lru_cache_.write(Key, buf);
		DEBUG_SCOPE(
			if(in_probability(0.001))
				std::cout<<"~~~~~~~~~~~~~~~~~~~~known new seno="<<Key.seqno<<", size="<<buf.length()<<"-----------------\n";
		);
	}
}

void mds_cache_service::report_missing_pieces(peer_connection* conn, 
const std::string& channelID, const std::string& peerID, const seqno_set& seqSet)
{
	media_request_msg msg;
	msg.set_direct_request(true);
	msg.set_peer_id(peerID);
	msg.set_channel_id(channelID);

	for (BOOST_AUTO(itr, seqSet.begin());
		itr!=seqSet.end();++itr)
	{
		msg.add_seqno(*itr);
	}
	conn->async_send_reliable(serialize(msg), global_msg::no_piece);
	DEBUG_SCOPE(
		if(msg.seqno_size()&&in_probability(0.001))
			std::cout<<"xxxxxxxxxxxxxxxxxxxxxx--report no piece seqmin="
			<<msg.seqno(0)
			<<"seqmax="<<msg.seqno(msg.seqno_size()-1)
			<<"------------\n";
	);
}