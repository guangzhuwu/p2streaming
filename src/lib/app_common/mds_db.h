#ifndef p2s_mds_control_db_h__
#define p2s_mds_control_db_h__

#include <string>
#include <p2engine/push_warning_option.hpp>
#include <p2engine/utf8.hpp>
#include <boost/format.hpp>
#include <boost/scoped_ptr.hpp>

#include <p2engine/pop_warning_option.hpp>
#include "common/utility.h"
#include "common/parameter.h"

#include <cppdb/cppdb/frontend.h>
#include "app_common/mds.pb.h"
#include "app_common/interprocess.h"
#include "app_common/typedef.h"

namespace cppdb
{
	class session;
}

namespace p2control{

	using namespace p2engine;
	const char* get_distribution_string(int type);
	int get_distribution_type(const std::string& typestr);
	class mds_db
	{
		typedef alive::mds_Alive			mds_Alive;
		typedef p2common::distribution_type distribution_type;
		typedef p2common::server_param_base server_param_base;
		typedef p2common::timestamp_t		timestamp_t;
		enum{LOG_CACHE_SIZE=10};

	public:
		mds_db(const host_name_t&	hostName, 
			const user_name_t&		userName, 
			const password_t&			pwd, 
			const db_name_t&		dbName);
		virtual ~mds_db();

		const std::string& db_name()const{return db_name_;	}

		bool get_regist_code(std::string& registCode, std::string* errorMsg=NULL);
		bool set_regist_code(const std::string& registCode, std::string* errorMsg=NULL);


		bool get_operation_http_port(int& port, std::string* errorMsg=NULL);
		bool set_operation_http_port(const int port, std::string* errorMsg=NULL);

		bool get_max_channel_per_server(int& cnt, std::string* errorMsg=NULL);
		bool set_max_channel_per_server(int cnt, std::string* errorMsg=NULL);

		bool get_channels(distribution_type type, 
			std::vector<server_param_base>& params, std::string* errorMsg=NULL);
		bool set_tracker_endpoint(distribution_type type, const std::string& val, 
			std::string* errorMsg=NULL);

		bool add_tracker_endpoint(const server_param_base& param, std::string* errorMsg=NULL);
		bool get_tracker_endpoint(const server_param_base& param, 
			std::vector<std::string>& edps, std::string* errorMsg=NULL);
		bool del_tracker_endpoint(const server_param_base& param, std::string* errorMsg=NULL);

		bool add_hashed_channel(int, const std::vector<server_param_base>& links, 
			std::string* errorMsg=NULL);

		bool get_hashed_channels(distribution_type type, std::map<std::string, int>& mds_chnl_map, 
			std::string* errorMsg=NULL);

		bool get_channel(server_param_base& param, std::string* errorMsg=NULL);
		bool set_channel(const server_param_base& param, std::string* errorMsg=NULL);
		bool set_channel(distribution_type type, const mds_Alive&msg, 
			std::string* errorMsg=NULL);
		bool report_channel(distribution_type type, const mds_Alive&msg, 
			std::string* errorMsg=NULL);
		bool report_channels(distribution_type type, const std::list<mds_Alive>&msg, 
			std::string* errorMsg=NULL);
		bool add_channel(const server_param_base& param, std::string* errorMsg=NULL);
		bool del_channel(const server_param_base& param, std::string* errorMsg=NULL);
		bool del_channels(const std::vector<server_param_base>& params, std::string* errorMsg=NULL);

		bool start_channel(const server_param_base& param, std::string* errorMsg=NULL);
		bool stop_channel(const server_param_base& param, std::string* errorMsg=NULL);

		bool log(const std::string& msg, const std::string& level="Info");

		//enum channel_status{RUNNING, STOPED, ERROR};
		//channel_status get_channel_stat(distribution_type type, 
		//	const std::string& channelLink, std::string* errorMsg=NULL);

	private:
		void __check_config_table();
		void __check_server_table(distribution_type type);
		void __check_shitf_live_server_table();
		void __check_report_table();
		void __check_tracker_table();
		void __check_mds_chn_hash_table();
		void __check_log_table();

		template<typename ItemType>
		bool __get_config(ItemType& result, const std::string& id, 
			const std::string& functionName, std::string* errorMsg=NULL);
		template<typename Type>
		bool __set_config(const Type& result, const std::string& id, 
			const std::string& functionName, std::string* errorMsg=NULL);

		bool __update_or_insert_channel(const server_param_base& param, 
			const std::string& functionName, std::string* errorMsg=NULL);
		bool __update_or_insert_shift_channel(const server_param_base& param, 
			const std::string& functionName, std::string* errorMsg=NULL);
		bool __update_channel(distribution_type type, const mds_Alive& param, 
			const std::string& functionName, std::string* errorMsg=NULL);
		bool __insert_report(distribution_type type, const mds_Alive& param, 
			const std::string& functionName, std::string* errorMsg=NULL);
		bool __insert_report(distribution_type type, const std::list<mds_Alive>& params, 
			const std::string& functionName, std::string* errorMsg=NULL);
		bool __start_or_stop_channel(const server_param_base& param, 
			const std::string& functionName, bool start, std::string* errorMsg=NULL);
		bool __get_shift_channel(server_param_base& param, 
			const std::string& functionName, std::string* errorMsg=NULL);
		bool __get_shift_channels(std::vector<server_param_base>& params, 
			const std::string& functionName, std::string* errorMsg=NULL);
		bool __del_shift_channel(const server_param_base& param, 
			const std::string& functionName, std::string* errorMsg=NULL);
		bool __del_hashed_channel(const server_param_base&param, 
			const std::string& functionName, std::string* errorMsg=NULL);

		bool __add_hashed_channel(int, const server_param_base& param, 
			const std::string& functionName, std::string* errorMsg=NULL);
		bool __get_channel(server_param_base& param, 
			const std::string& functionName, std::string* errorMsg=NULL);

		void set_and_log_message(const std::string &functionName, 
			const std::exception* e, std::string* errorMsg);
		inline cppdb::result get_channel_compatible_sql( const std::string &stream_recv_param, 
														const std::string &table_name, 
														const std::string &condition = "")
		{
			boost::format querySql("SELECT channel_link, "					\
											"channel_uuid, "					\
											"channel_key, "						\
											"name, "							\
											"path, "							\
											"duration, "						\
											"length, "							\
											"%1%, "								\
											"external_address, "				\
											"internal_address, "				\
											"tracker_endpoint FROM %2% %3%" );							
			querySql%stream_recv_param%table_name%condition;
			return (*db_<<querySql.str() );
		}

		inline bool insert_or_update_channel_compatible_sql( const int action, // 0 : insert, > 0 : update
															const std::string &stream_recv_param, 
															const std::string &fmt, 
															const server_param_base &param)
		{
			if( action == 0 )	// insert sql 
			{
				boost::format insertSql("INSERT INTO %s " 
											"(channel_link, channel_uuid, channel_key, "
											"name, path, duration, length, "
											+ stream_recv_param + ", external_address, "
											"internal_address, tracker_endpoint) "
											" values('%s', '%s', '%s', '%s', '%s', %s, %s, "
											+ fmt + ", '%s', '%s', '%s')" );
				insertSql%get_distribution_string(param.type)
						 %string_to_hex(param.channel_link)
						 %string_to_hex(param.channel_uuid)
						 %param.channel_key					
						 %string_to_hex(convert_from_wstring(param.name.wstring()))
						 %string_to_hex(convert_from_wstring(param.media_directory.wstring()))
						 %param.film_duration
						 %param.film_length
						 %param.stream_recv_url
						 %param.external_ipport	
						 %param.internal_ipport	
						 %param.tracker_ipport;
				DEBUG_SCOPE(std::cout<<insertSql<<std::endl;);
				cppdb::statement stmt=*db_<<insertSql.str();
				stmt.exec();
				return ( 1==stmt.affected() );
			}
			else	// update sql
			{
				boost::format updateSql("UPDATE %s SET "		\
					"channel_link = '%s', "					\
					"channel_uuid = '%s', "					\
					"channel_key = '%s', "					\
					"name = '%s', "							\
					"path = '%s', "							\
					"duration = %s, "						\
					"length = %s, "							\
					+stream_recv_param+"="+fmt+", "			\
					"external_address = '%s', "				\
					"internal_address = '%s', "				\
					"tracker_endpoint = '%s' "				\
					"WHERE channel_link = '%s'"				\
					);										\
					updateSql%get_distribution_string(param.type)
					%string_to_hex(param.channel_link)
					%string_to_hex(param.channel_uuid)
					%param.channel_key			
					%string_to_hex(convert_from_wstring(param.name.wstring()))
					%string_to_hex(convert_from_wstring(param.media_directory.wstring()))
					%param.film_duration	
					%param.film_length		
					%param.stream_recv_url	
					%param.external_ipport	
					%param.internal_ipport	
					%param.tracker_ipport	
					%string_to_hex(param.channel_link);
				DEBUG_SCOPE(std::cout<<updateSql<<std::endl;);
				cppdb::statement stmt=*db_<<updateSql.str();
				stmt.exec();
				return ( stmt.affected() >= 0 );
			}
		}
		
	private://make uncopyable
		mds_db(const mds_db&);
		mds_db& operator=(const mds_db&);

	private:
		std::string						db_name_;
		std::auto_ptr<cppdb::session>	db_;
		std::list<std::string>			log_cache_;
		timestamp_t						last_log_time_;
		bool							chn_hash_table_ok_;
		bool							tracker_table_ok_;
		bool							report_table_ok_;
		bool							config_table_ok_;
	};

};//namespace p2control

#endif // p2s_mds_control_utility_h__
