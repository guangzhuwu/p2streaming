#ifndef cmd_receiver_h__
#define cmd_receiver_h__

#include <iostream>
#include <boost/lexical_cast.hpp>
//#include <boost/interprocess/shared_memory_object.hpp>
//#include <boost/interprocess/managed_shared_memory.hpp>
//#include <boost/interprocess/containers/map.hpp>

#include "common/message.pb.h"
#include "common/parameter.h"
#include "common/typedef.h"
#include "app_common/app_common.h"
#include "p2s_mds/media_server.h"
#include "p2s_mds/progress_alive_alarm.h"

class progress_alive_alarm;
class auth;

namespace p2control{

	using namespace p2engine;

	class mds_cmd_receiver
		: public basic_engine_object
	{
		typedef mds_cmd_receiver this_type;

		struct  server_elm;
		typedef std::pair<std::string, server_elm> pair_type;
		typedef p2engine::rough_timer timer;
		typedef p2common::server_param_base		server_param_base;
		typedef std::vector<server_param_base>  channel_vec_type;
		typedef p2common::distribution_type		distribution_type;
		typedef p2common::message_socket_sptr	message_socket_sptr;

		SHARED_ACCESS_DECLARE;
	public:
		static shared_ptr create(io_service& ios)
		{
			return shared_ptr( new this_type(ios), 
				shared_access_destroy<this_type>()
				);
		}	
		void start(int server_id, int type);
		boost::shared_ptr<p2s_mds> create_and_start_mds(const server_param_base& param, std::string& errorMsg);
		void set_alive_alarm(int port, const std::string& reg_code);
		void post_error_msg(const std::string& errorMsg);
		void report_auth_message(const std::string& message);

	protected:
		void process_cmd(message_socket*, const safe_buffer& buf);
		bool channel_handler(const mds_cmd_msg& msg, std::string& errorMsg);
		bool config_handler(const mds_cmd_msg& msg, std::string& errorMsg);

		bool add_channel(const ctrl2m_create_channel_msg&, std::string& errorMsg);
		bool del_channel(const std::string& channel_link, std::string& errorMsg);
		bool reset_channel(const std::string& channel_link, std::string& errorMsg);
		bool start_channel(const std::string& channel_link, std::string& errorMsg);
		bool stop_channel(const std::string& channel_link, std::string& errorMsg);
		bool add_tracker(const std::string& tracker_ipport, const std::vector<std::string>& links, std::string& errorMsg);
		bool del_tracker(const std::string& tracker_ipport, const std::vector<std::string>& links, std::string& errorMsg);
		bool change_tracker(const std::string& tracker_ipport, const std::vector<std::string>& links, std::string& errorMsg);
		bool reset_regist_code(const std::string& reg_code, std::string& errorMsg);
	
	protected:
		mds_cmd_receiver(io_service& ios);
		~mds_cmd_receiver();

	private:
		struct server_elm{
			server_elm(){}
			server_elm(interprocess_client::shared_ptr Proxy, boost::shared_ptr<p2s_mds>mds_,
				boost::shared_ptr<progress_alive_alarm> alarm_
				):proxy_(Proxy), mds(mds_), alarm(alarm_)
			{}

			void stop()
			{
				error_code ec;
				if (mds) mds->stop(ec);
				if (alarm)alarm->stop();
			}
			void start(error_code& ec)
			{
				if (mds) 
					mds->start(ec);
				if (!ec)
					if (alarm)alarm->start(proxy_.lock(), ec);
			}
			void reset(error_code& ec)
			{
				if (mds) mds->reset(ec);
				if (!ec)
					if (alarm)alarm->reset(ec);
			}
			bool is_stopped()const
			{
				return !mds||!alarm||mds->is_stoped()||alarm->is_stoped();
			}

			boost::shared_ptr<p2s_mds> mds;
			boost::shared_ptr<progress_alive_alarm> alarm;
		private:
			interprocess_client::weak_ptr proxy_;
		};

		template<typename HandlerType>
		bool tracker_handlers(const std::vector<std::string>&links, 
			boost::unordered_map<std::string, mds_cmd_receiver::server_elm>& servers, 
			HandlerType& do_work_handler)
		{
			if(links.empty()) 
			{
				BOOST_FOREACH(pair_type itr, servers)
				{
					do_work_handler(itr);
				}
			}
			else //ֻ�ı����Ƶ��
			{
				BOOST_FOREACH(pair_type itr, servers)
				{
					if(links.end()==std::find(links.begin(), links.end(), itr.first))
						continue;

					do_work_handler(itr);
				}
			}
			return true;
		}

		boost::unordered_map<std::string, server_elm> servers_;
		interprocess_client::shared_ptr	interprocess_client_;
		boost::shared_ptr<auth>			auth_;
		int			server_id_;
		int			type_;
		int			alive_alarm_port_;
		bool		init_state_;
		std::string regist_code_;
		timestamp_t last_check_alive_time_;
		timestamp_t last_process_time_;
		boost::unordered_set<std::string> suspended_channels_; //ֹͣ����û��ɾ����Ƶ��
	};

};//namespace p2control

#endif // cmd_receiver_h__
