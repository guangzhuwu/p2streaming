#ifndef p2p_vod_simple_server_distributor_scheduling_h__
#define p2p_vod_simple_server_distributor_scheduling_h__

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/member.hpp>
#include <p2engine/p2engine.hpp>
#include "simple_server/simple_distributor.h"

namespace p2simple
{
	class simple_distributor_interface;

	class distributor_scheduling
		: public basic_engine_object
	{		
		typedef distributor_scheduling  this_type;
		typedef p2engine::rough_timer   timer;
		typedef boost::filesystem::path path_type;
		typedef std::vector<boost::filesystem::path> paths_type;
		typedef boost::shared_ptr<simple_distributor_interface> simple_distributor_interface_sptr;
		SHARED_ACCESS_DECLARE;

		struct channel_set 
			: public multi_index::multi_index_container<channel_session, 
			multi_index::indexed_by<
			multi_index::ordered_unique<multi_index::member<channel_session, uint32_t, &channel_session::id_> >, 
			multi_index::ordered_unique<multi_index::member<channel_session, timestamp_t, &channel_session::stamp_started_> >
			> 
			>
		{
			typedef nth_index<0>::type id_index_type;
			typedef nth_index<1>::type stamp_index_type;

			id_index_type& id_index(){return multi_index::get<0>(*this);}
			stamp_index_type& stamp_index(){return multi_index::get<1>(*this);}
			const id_index_type& id_index () const {return multi_index::get<0>(*this);} 
			const stamp_index_type& stamp_index() const {return multi_index::get<1>(*this);} 
		};


	public:
		static this_type::shared_ptr create(io_service& ios)
		{
			return this_type::shared_ptr(new this_type(ios), 
				shared_access_destroy<this_type>()
				);
		}

	public:
		simple_distributor_interface_sptr start(peer_info& srv_info, const path_type&);
		void stop();
		rough_speed_meter& out_speed_meter(){return media_packet_rate_;};
		int out_kbps()const;

	private:
		void start_assist_distributor(const path_type&);
		void start_shift_timer();
		void shift_file(channel_session* channel_info, seqno_t max_of_last_cyc);

	private:
		bool find_channel_files(const path_type& dskFile, 
			std::string& file_name, paths_type& files);
		void static_channel_session(const paths_type& channels, 
			const std::string& fileName, int64_t& DurationOfAllFile);

		void create_assist_distributor_and_start();
		void update_channel_info_to_admin(int64_t filmDuration);

	private:
		void on_shift();

	protected:
		explicit distributor_scheduling(io_service& ios)
			: basic_engine_object(ios)
			, seq_num_(0)
			, seq_num_per_file_(0)
			, media_packet_rate_(millisec(1000))
		{}
		~distributor_scheduling(){stop();}

	private:
		boost::shared_ptr<async_filecache>    async_filecache_;
		simple_distributor_interface_sptr     simple_distributor_;

		channel_set					channel_file_set_;
		boost::optional<peer_info&> srv_info_;
		boost::shared_ptr<timer>	shift_timer_; 	
		mutable boost::timed_mutex	mutex_;
		seqno_t seq_num_,  seq_num_per_file_;
		rough_speed_meter  media_packet_rate_;
	};

}

#endif // p2p_vod_simple_server_distributor_scheduling_h__
