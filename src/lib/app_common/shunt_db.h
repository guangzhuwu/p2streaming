#ifndef p2s_shunt_control_db_h__
#define p2s_shunt_control_db_h__

#include <memory>
#include <string>
#include <vector>

#include <boost/noncopyable.hpp>
#include <boost/scoped_ptr.hpp>

#include "common/utility.h"
#include "common/parameter.h"
#include "common/typedef.h"

#include "app_common/app_common.h"

namespace cppdb
{
	class session;
}

namespace p2control{

	using namespace p2engine;

	class shunt_db
		:boost::noncopyable
	{
		enum{ LOG_CACHE_SIZE = 10 };
	public:
		typedef std::vector<shunt_xml_param>			param_vector;
		typedef alive::shunt_Alive						shunt_Alive;
		typedef p2common::timestamp_t					timestamp_t;

	public:
		shunt_db(const host_name_t& hostName,
			const user_name_t&		userName,
			const password_t&		pwd,
			const db_name_t&		dbName);
		~shunt_db();

		const std::string& db_name()const{ return db_name_; }

		bool get_alive_alarm_port(int& port, std::string& errorMsg);
		bool set_alive_alarm_port(const int port, std::string& errorMsg);

		bool get_operation_http_port(int& port, std::string& errorMsg);
		bool set_operation_http_port(const int port, std::string& errorMsg);

		bool get_channels(param_vector& params, std::string& errorMsg);
		bool get_channel(shunt_xml_param& param, std::string& errorMsg);

		bool set_channel(const shunt_xml_param& param
			, const shunt_Alive&msg, std::string& errorMsg);

		bool del_channel(const std::string& channel_link, std::string& errorMsg);
		bool log(const std::string& msg, const std::string& level = "Info");

	private:
		void __check_config_table();
		void __check_shunt_table();
		void __check_sender_table();
		void __check_log_table();

		template<typename ItemType>
		bool __get_config(ItemType& result, std::string& errorMsg,
			const std::string& id, const std::string& functionName);
		template<typename ItemType>
		bool __set_config(const ItemType& result, std::string& errorMsg,
			const std::string& id, const std::string& functionName);

		bool __get_channels(param_vector& params, std::string& errorMsg
			, const std::string& functionName);
		bool __get_channel(shunt_xml_param& param, std::string& errorMsg
			, const std::string& functionName);
		bool __get_send_urls(shunt_xml_param& param, std::string& errorMsg
			, const std::string& functionName);

		bool __update_or_insert_channel(const shunt_xml_param& param, const shunt_Alive& msg,
			std::string& errorMsg, const std::string& functionName);
		bool __insert_sender(const shunt_xml_param& config_param,
			std::string& errorMsg,
			const std::string& functionName);
		bool __insert_sender(const std::string& channel_link,
			const std::string& edp, std::string& errorMsg, const std::string& functionName);

		bool __del_channel(const std::string& channel_link,
			std::string& errorMsg, const std::string& functionName);
		bool __del_channel_in_sender_table(const std::string& channel_link,
			std::string& errorMsg, const std::string& functionName);

	private://make uncopyable
		shunt_db(const shunt_db&);
		shunt_db& operator=(const shunt_db&);

	private:
		boost::scoped_ptr<cppdb::session>	db_;
		std::string							db_name_;
	};

};//namespace p2control

#endif // p2s_shunt_control_db_h__
