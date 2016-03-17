#ifndef basic_control_base_h__
#define basic_control_base_h__

#include <p2engine/push_warning_option.hpp>
#include <boost/noncopyable.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <p2engine/pop_warning_option.hpp>

#include "common/common.h"
#include "app_common/process_killer.h"
#include "app_common/interprocess.h"
#include "app_common/typedef.h"

namespace p2control{	

	using namespace p2engine;
	using namespace p2common;

	//////////////////////////////////////////////////////////////////////////
	class control_base
		: public basic_engine_object
		, private boost::noncopyable
	{
		typedef control_base this_type;
		SHARED_ACCESS_DECLARE;

	public:
		typedef rough_timer							timer;
		typedef std::map<std::string, std::string>	config_map_type;
		typedef config_map_type::value_type			config_value_type;
		typedef interprocess_server::__id			__id;
		typedef struct 
		{
			config_map_type qmap;
			std::string     session_id;
		}req_session;

		typedef http::basic_http_connection<http::http_connection_base>     http_connection;
		typedef http_connection::shared_ptr									http_connection_sptr;
		typedef http::basic_http_acceptor<http_connection, http_connection>	http_acceptor;
		
		typedef struct{
			db_name_t	dbName;
			host_name_t hostName;
			user_name_t userName;
			password_t	pwds;

			std::string auth_message_;
			std::string child_process_name_;
			uint32_t    max_channel_per_server_;
			int			alive_alarm_port_;
			int			http_cmd_port_;
		} config_param_st;

		enum{CREATE_PROCESS_INTERVAL=20000};
		enum{WILD_CHECK_INTERVAL=10, KEEP_WILD_INTERVAL=25};
		enum{PORT_BEGIN=60000, PORT_END=65000};

	public:
		void start(const db_name_t& dbName, 
			const host_name_t&	hostName, 
			const user_name_t&	userName, 
			const password_t&	pwd, 
			const char*			subAppName, 
			int aliveAlarmPort, int cmdPort=0);

		virtual void on_client_login(message_socket_sptr, const safe_buffer&)=0;
		virtual void on_recvd_alarm_message(const safe_buffer& buf)=0;
		virtual void on_recvd_cmd_reply(const safe_buffer& buf, const std::string& data="");
		virtual void on_recvd_error_message(const std::string& msg){};
		virtual void on_client_dropped(const __id& ID){};
		virtual void on_recvd_auth_message(const safe_buffer& buf);

	public:
		void on_session_time_out(const std::string& sessionID);
		void on_request_time_out();

	protected:
		virtual void recover_from_db()=0;

		virtual void __start();
		virtual void __set_operation_http_port(uint32_t port, std::string& errorMsg)=0;
		virtual void __set_alive_alarm_port(uint32_t port, std::string& errorMsg)=0;

		virtual void on_wild_sub_process_timer()=0;
		virtual void on_sub_process_check_timer(){};
		virtual bool on_request_handler(const req_session& reqSess, 
			std::string& errorMsg){return true;};

	protected:
		void start_cmd_accptor(int& port);
		void start_sub_process_check_timer();
		void start_wild_check_timer();
		void start_time_out_timer();

		void stop_cmd_accptor();
		void stop_sub_process_check_timer();
		void stop_wild_check_timer();
		void stop_time_out_timer();

		void response_cmd_exec_state(http_connection_sptr conn, http::header::status_type state, 
			const req_session& req, const std::string& errorMsg);
		void handle_sentout_and_close(http_connection_sptr sock);
		void try_kill_wild_sub_process(pid_t pid);
		void reply_http_request(const c2s_cmd_reply_msg& msg, const std::string& data="");

		config_param_st& config_param(){return param_;}
		const config_param_st& config_param()const{return param_;}

	private:
		void on_accepted(http_connection_sptr conn, const error_code& ec);
		void on_request(const http::request& req, http_connection* conn);

	protected:
		explicit control_base(io_service& ios);
		virtual ~control_base();

	protected:
		struct session 
		{
			session(int			   _type, 
				http_connection_sptr    _conn, 
				const std::string& link, 
				int				   _expected_reply_cnt=1, 
				timestamp_t        t=timestamp_now())
				:expire_time(t+30000), type(_type), expected_reply_cnt(_expected_reply_cnt), 
				conn(_conn), link_id(link)
			{}
			timestamp_t     expire_time;
			int				type;
			int             expected_reply_cnt;
			http_connection_sptr conn;
			std::string		link_id;
		};
		std::map<std::string, session>	  sessions_;
		interprocess_server::shared_ptr	  interprocess_server_;

	private:
		boost::shared_ptr<http_acceptor>  http_acceptor_;
		timed_keeper_set<http_connection_sptr> cmd_sockets_;
		timed_keeper_set<pid_t>			  wild_sub_process_set_;
		config_param_st					  param_;

		boost::shared_ptr<timer>	sub_process_check_timer_;
		boost::shared_ptr<timer>	wild_sub_process_check_timer_;
		boost::shared_ptr<timer>    time_out_timer;

	};

	struct is_port_usable
	{
		bool operator()(io_service& ios, 
			const std::string& url, error_code& ec)const;
	};


	template<typename value_type>
	bool get_config_value(const std::string& key, value_type& value, std::string& errorMsg)
	{
		//¶Áconfig.ini
		static boost::property_tree::ptree pt;
		try
		{
			if(pt.empty())
			{
				boost::filesystem::path config_path = get_exe_path();
				config_path /="config.ini";
				boost::property_tree::ini_parser::read_ini(config_path.string().c_str(), pt);
			}
			value = pt.get<value_type>(key);
			return true;
		}
		catch(const std::exception& e)
		{
			errorMsg = "get config value " + key + " : " + e.what();
			return false;
		}
		catch(...)
		{
			errorMsg = "get_config_value, unknown error!";
			return false;
		}
		return false;
	}

	typedef control_base::config_map_type config_map_type;
	const std::string& get(const config_map_type& req, const std::string& k);
	int get_int_type(const config_map_type& req, const std::string& k);

};//namespace p2control

#endif // basic_control_base_h__
