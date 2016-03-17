#ifndef mds_cache_service_h__
#define mds_cache_service_h__

#include "p2s_mds_cache/cache.h"
#include "p2engine/p2engine/basic_engine_object.hpp"
#include "common/message.pb.h"
#include "simple_server/simple_distributor.h"
#include "simple_server/peer_connection.h"

using namespace p2engine;
using namespace p2common;
namespace p2cache{

	class mds_cache_service
		: public p2simple::simple_distributor
	{
		typedef mds_cache_service this_type;
		SHARED_ACCESS_DECLARE;
		
		typedef struct _st_key{
			_st_key(){}
			_st_key(const std::string& channelID_hash, seqno_t Seqno)
				:channel_id(channelID_hash), seqno(Seqno){}

			//�ȱȽ�seqno�����
			bool operator==(const _st_key &rhs) const
			{
				return (seqno==rhs.seqno)&&(channel_id==rhs.channel_id);
			}
			bool operator< (const _st_key &rhs) const
			{
				if(seqno!=rhs.seqno)
					return seqno<rhs.seqno;
				return (channel_id<rhs.channel_id);
			}
			std::string	channel_id;
			seqno_t		seqno;
		}st_key;

		friend  size_t hash_value(const st_key &Key)
		{
			size_t seed=0;
			boost::hash_combine(seed, Key.channel_id);
			boost::hash_combine(seed, Key.seqno);
			return seed;
		}

		typedef wfs::cache<st_key, safe_buffer, null_mutex> fast_cache_type;
		typedef p2simple::peer_connection		peer_connection;
		typedef p2simple::peer_connection_sptr	peer_connection_sptr;
		typedef std::vector<seqno_t>			seqno_set;
		typedef boost::unordered_map<std::string, seqno_set> seqno_map;

	public:
		static mds_cache_service::shared_ptr create(io_service& ios, uint64_t mlimit)
		{return this_type::shared_ptr(new this_type(ios, mlimit));}

	public:
		mds_cache_service(io_service& ios, uint64_t mlimit)
			:simple_distributor(ios)
			, lru_cache_(mlimit), hitted_ratio_(0.0)
		{}
		~mds_cache_service(){
			if (log_thread_)
			{
				log_thread_->join();
				log_thread_.reset();
			}
		}

	public:
		virtual void start(peer_info& local_info);
		virtual void read_request_media(peer_connection_sptr conn, const media_request_msg& msg);
	
	protected:
		virtual void register_message_handler(peer_connection_sptr con);
		
	protected:
		void on_recvd_media_packet(peer_connection* conn, safe_buffer buf);
		void report_missing_pieces(peer_connection* conn, const std::string& channelID, 
			const std::string& peerID, const seqno_set& seqSet);
		void write_cache_media_packet(const std::string& channelID, seqno_t seqno, const safe_buffer& buf);
	
	private:
		void write_log();
	private:
		fast_cache_type	lru_cache_;
		seqno_map       miss_map_;
		boost::shared_ptr<boost::thread> log_thread_;
		double			hitted_ratio_;
		std::string     path_;
		fast_mutex		mutex_;
	};

};

#endif // cache_service_h__
