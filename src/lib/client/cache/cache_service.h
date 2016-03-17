#ifndef cache_service_handler_h__
#define cache_service_handler_h__

#include "client/typedef.h"
#include "simple_server/distributor_Impl.hpp"
#include "asfio/async_dskcache.h"

namespace p2simple{

	class simple_client_distributor
		: public dsk_cache_distributor 
	{
		typedef simple_client_distributor this_type;
		SHARED_ACCESS_DECLARE;

	public:
		static shared_ptr create(io_service& net_svc, const filecache_sptr& ins, 
			const boost::filesystem::path& media_pathname = boost::filesystem::path("")
			)
		{
			return shared_ptr(new this_type(net_svc, ins, media_pathname), 
				shared_access_destroy<this_type>()
				);
		}

		virtual void on_recvd_buffermap_request(peer_connection*conn, safe_buffer buf);

	protected:
		simple_client_distributor(
			io_service& ios, const filecache_sptr& ins, 
			const boost::filesystem::path& media_pathname
			)
			:dsk_cache_distributor(ios, ins, media_pathname)
			, smallest_seq_cached_(-1)
		{
		}

	private:
		int smallest_seq_cached_;
	};
}

namespace p2client
{	
	class cache_service
		: public basic_engine_object
		, public basic_client_object
	{
		typedef cache_service this_type;
		SINGLETON_ACCESS_DECLARE;

		typedef p2client::tracker_manager tracker_manager;
		typedef p2simple::simple_distributor_interface   simple_distributor_interface;
		typedef asfio::async_dskcache     async_dskcache;
		typedef p2simple::peer_connection peer_connection;
		typedef p2engine::rough_timer timer;

	public:
		cache_service(io_service&ios, client_param_sptr localParam);
		~cache_service();

		void start(int64_t cache_file_size=2*1024*1024*1024LL);
		bool is_running(){return running_;}

		boost::shared_ptr<tracker_manager> get_tracker_handler(){return tracker_handler_;};
		boost::shared_ptr<async_dskcache> get_catch_manager(){return async_dskcache_;}
		void update_cached_channel();
		void start_update_cache_timer(const std::string& channelID);
		void stop_update_cache_timer();

	protected:
		void on_dskcache_opened(const error_code&ec);
		void on_tracker_login_finished(int cache_file_size);
		void update_changed_cache(const std::string& channelID);

	private:
		//peer_info cache_tracker_info_;
		//client_param_sptr local_param_;
		boost::shared_ptr<tracker_manager> tracker_handler_;
		boost::shared_ptr<simple_distributor_interface> distributor_;
		boost::shared_ptr<async_dskcache> async_dskcache_;
		boost::shared_ptr<timer> update_cache_to_tracker_timer_;
		bool running_;
		boost::unordered_set<std::string> last_cache_channels_;
	};


	boost::shared_ptr<cache_service>& get_cache_service(io_service& ios);
	boost::shared_ptr<p2client::tracker_manager> get_cache_tracker_handler(io_service& ios);
	boost::shared_ptr<asfio::async_dskcache> get_cache_manager(io_service& ios);
	boost::shared_ptr<cache_service> init_cache_service(io_service& ios, 
		client_param_sptr localParam, size_t cache_file_size=1024*1024*1024);
}

#endif // cache_service_handler_h__

