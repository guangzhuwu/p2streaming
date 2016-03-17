#ifndef buffer_manager_h__
#define buffer_manager_h__

#include "client/stream/absent_packet_list.h"
#include "asfio/async_dskcache.h"

namespace p2client{

	class buffer_manager
		:public basic_client_object
	{
	public:
		typedef packet_buffer::recent_packet_info recent_packet_info;
		
		buffer_manager(io_service& ios, client_param_sptr param, int64_t film_length=-1);
		virtual ~buffer_manager();

		absent_packet_list& get_absent_packet_list()
		{return absent_packet_list_;}
		const absent_packet_list& get_absent_packet_list()const
		{return absent_packet_list_;}

		packet_buffer& get_memory_packet_cache()
		{return memory_packet_cache_;}
		const packet_buffer& get_memory_packet_cache()const
		{return memory_packet_cache_;}

		boost::shared_ptr<asfio::async_dskcache>& get_disk_packet_cache()
		{return disk_packet_cache_;}
		const boost::shared_ptr<asfio::async_dskcache>& get_disk_packet_cache()const
		{return disk_packet_cache_;}

		const boost::optional<seqno_t>& get_bigest_sqno_i_know()const
		{return bigest_sqno_i_know_;}
		boost::optional<seqno_t>& get_bigest_sqno_i_know()
		{return bigest_sqno_i_know_;}

		const boost::optional<seqno_t>& get_average_current_server_seqno()const
		{return current_server_seqno_;}
		boost::optional<seqno_t>& get_average_current_server_seqno()
		{return current_server_seqno_;}

		int max_bitmap_range_cnt()const
		{return max_bitmap_range_cnt_;}

	public:
		//	重置。在调度器scheduling重置时需要调用本函数。
		void reset();

		//	设置当前所知的最新片段号等。必须在推拉调度开始前设置。
		void set_bigest_seqno(seqno_t current_bigest_seq, int backfetch_cnt, timestamp_t now);

		//	主动插入一些absent seqno。VoD类型的调度过程中需要local主动产生absent seqno，
		//因此，VoD类型需要周期性的调用本函数，比如可以在每次on_pull_timer时候调用。
		void inject_absent_seqno(seqno_t smallest_seqno_i_care, int buffer_size, timestamp_t now);

		void get_absent_seqno_range(seqno_t& absent_first_seq, seqno_t& absent_last_seq, 
			const peer_sptr& per, int intervalTime, 
			const boost::optional<seqno_t>& smallest_seqno_i_care, 
			double src_packet_rate);

		//	获取local buffermap并写到MsgType中。发给remote本地片段信息时调用。
		//当频道使用了硬盘存储时，要传入channel_uuid_for_erased_seqno_on_disk_cache，
		//否则就传入一个NULL。
		template<typename MsgType>
		void get_buffermap(MsgType& msg, int src_packet_rate, 
			bool using_bitset = false, bool using_longbitset = false, 
			const std::string* channel_uuid_for_erased_seqno_on_disk_cache=NULL
			)
		{
			buffermap_info* mutable_buffermap = msg.mutable_buffermap();
			get_buffermap(mutable_buffermap, src_packet_rate, using_bitset, 
				using_longbitset, channel_uuid_for_erased_seqno_on_disk_cache);
		}

		//	获取local buffermap并写到buffermap_info中。发给remote本地片段信息时调用。
		//当频道使用了硬盘存储时，要传入channel_uuid_for_erased_seqno_on_disk_cache，
		//否则就传入一个NULL。
		void get_buffermap(buffermap_info* mutable_buffermap, int src_packet_rate, 
			bool using_bitset = false, bool using_longbitset = false, 
			const std::string* channel_uuid_for_erased_seqno_on_disk_cache=NULL
			);

		//	处理收到的buffermap中所携带的删除seqno。这一般出现在VoD情况下，remote先前
		//通过buffermap告知local其具有某些片段，但一段时间后这些片段删除了。
		void process_erased_buffermap(peer_connection* conn, seqno_t seq_begin, 
			seqno_t seq_end, seqno_t smallest_seqno_i_care, timestamp_t now);

		//	处理收到的buffermap。
		typedef std::vector<boost::weak_ptr<peer_connection> > connection_vector;
		void process_recvd_buffermap(const buffermap_info& bufmap, 
			const connection_vector& in_substream, peer_connection* conn, 
			seqno_t smallest_seqno_i_care, timestamp_t now, bool add_to_owner);

		void process_recvd_buffermap(const std::vector<seqno_t>&seqnomap, 
			const connection_vector& in_substream, peer_connection* conn, 
			seqno_t bigest, seqno_t smallest_seqno_i_care, timestamp_t now, 
			bool add_to_owner, bool recentRecvdSeqno);

		void get_memory_packet_cache_buffermap(buffermap_info* mutableBuffermap, 
			int bitSize);
		void get_disk_packet_cache_buffermap(buffermap_info* mutableBuffermap, 
			const std::string& channel_uuid);

	private:
		int max_inject_count();
		void just_known(seqno_t seqno, const peer_connection_sptr& p, 
			seqno_t smallest_seqno_i_care, timestamp_t now, bool add_to_owner);

	protected:
		//数据调度的起始位置并不是当前服务器的输出片段，而是比当前片段号小backfetch_cnt
		//的片段。
		int backfetch_cnt_;
		int max_backfetch_cnt_;
		int max_bitmap_range_cnt_;
		boost::optional<seqno_t> current_server_seqno_;

		typedef boost::optional<std::pair<seqno_t, seqno_t> > optional_range;
		optional_range				seqno_range_;//点播、BT等的seqno是有范围限定的
		boost::optional<seqno_t>	bigest_sqno_i_know_;

		absent_packet_list absent_packet_list_;
		packet_buffer memory_packet_cache_;
		typedef asfio::async_dskcache async_dskcache;
		boost::shared_ptr<async_dskcache> disk_packet_cache_;

		//////////////////////////////////////////////////////////////////////////
		//为避免频繁构造而将一些临时变量提升为类的成员变量
		std::vector<char> buffermap_;
		std::vector<seqno_t> seqnomap_buffer_;
	};

}

#endif//buffer_manager_h__

