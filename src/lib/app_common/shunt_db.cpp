#include "app_common/shunt_db.h"
#include "cppdb/cppdb/frontend.h"
#include "common/utility.h"

#include <p2engine/push_warning_option.hpp>
#include <p2engine/p2engine.hpp>
#include <boost/format.hpp>
#include <p2engine/utf8.hpp>
#include <p2engine/pop_warning_option.hpp>


NAMESPACE_BEGIN(p2control);
using namespace p2engine;
using namespace p2common;

const char* const alive_alarm_port = "alive_alarm_port";
const char* const operation_http_port = "operation_http_port";
const char* const table_config = "sys_config";
const char* const table_shunt = "shunt";
const char* const table_sender = "sender";

shunt_db::shunt_db(const host_name_t& hostName,
	const user_name_t& userName,
	const password_t& pwd,
	const db_name_t& dbName)
	:db_name_(dbName.value)
{
	try
	{
		boost::format connection_string(
			"mysql:host=%s;"
			"database=%s;"
			"user=%s;"
			"password=%s;"
			"set_charset_name=utf8"
			);
		connection_string
			%hostName.value
			%dbName.value
			%userName.value
			%pwd.value;

		db_.reset(new cppdb::session(connection_string.str()));

		__check_config_table();
		__check_shunt_table();
		__check_sender_table();
	}
	catch (std::exception& e)
	{
		std::cout << e.what() << std::endl;
		log(e.what());
	}
}
shunt_db::~shunt_db()
{
	log("shunt_db::exit");
}

template<typename ItemType>
inline bool shunt_db::__get_config(ItemType& result, std::string& errorMsg,
	const std::string& id, const std::string& functionName)
{
	errorMsg.clear();
	try
	{
		boost::format sql("SELECT %s FROM %s");
		sql%id%table_config;

		cppdb::result res = *db_ << sql.str();
		if (res.next())
		{
			result = res.get<ItemType>(id);
			return true;
		}
		return false;

	}
	catch (std::exception& e)
	{
		errorMsg = functionName + " error! " + e.what();
		log(errorMsg);
		return false;
	}
	catch (...)
	{
		errorMsg = functionName + " error! unknown error.";
		log(errorMsg);
		return false;
	}
}

template<typename ItemType>
inline bool shunt_db::__set_config(const ItemType& result, std::string& errorMsg,
	const std::string& id, const std::string& functionName)
{
	errorMsg.clear();
	try
	{
		//__check_config_table();
		//update
		cppdb::statement stat = *db_ << (boost::format("UPDATE %s SET %s=%s")
			% table_config%id%result).str();
		stat.exec();

		return stat.affected() >= 0;
	}
	catch (std::exception& e)
	{
		errorMsg = functionName + " error! " + e.what();
		log(errorMsg);
		return false;
	}
	catch (...)
	{
		errorMsg = functionName + " error! unknown error.";
		log(errorMsg);
		return false;
	}
}

bool shunt_db::get_alive_alarm_port(int& port, std::string& errorMsg)
{
	return __get_config(port, errorMsg, alive_alarm_port, "get_alive_alarm_port");
}
bool shunt_db::set_alive_alarm_port(const int port, std::string& errorMsg)
{
	return __set_config(port, errorMsg, alive_alarm_port, "set_alive_alarm_port");
}

bool shunt_db::get_operation_http_port(int& port, std::string& errorMsg)
{
	return __get_config(port, errorMsg, operation_http_port, "get_operation_http_port");
}
bool shunt_db::set_operation_http_port(const int port, std::string& errorMsg)
{
	return __set_config(port, errorMsg, operation_http_port, "set_operation_http_port");
}

bool shunt_db::set_channel(const shunt_xml_param& param,
	const shunt_Alive&msg, std::string& errorMsg)
{
	return __update_or_insert_channel(param, msg, errorMsg, "set_channel");
}

bool shunt_db::get_channels(param_vector& params, std::string& errorMsg)
{
	return __get_channels(params, errorMsg, "get_channels");
}

bool shunt_db::get_channel(shunt_xml_param& param, std::string& errorMsg)
{
	return __get_channel(param, errorMsg, "get_channel");
}

bool shunt_db::del_channel(const std::string& channel_link, std::string& errorMsg)
{
	return __del_channel(channel_link, errorMsg, "del_channel");
}

bool shunt_db::__update_or_insert_channel(const shunt_xml_param& param,
	const shunt_Alive& msg,
	std::string& errorMsg,
	const std::string& functionName)
{
	errorMsg.clear();

	try
	{
		__check_shunt_table();
		__check_sender_table();
		const char* tableName = table_shunt;

		std::string channel_link = msg.id();
		std::string name = string_to_hex(convert_from_wstring(convert_to_wstring(param.name)));

		//检查是不是正确updata
		cppdb::result res = *db_ << (boost::format("SELECT * FROM %s WHERE channel_link='%s'")
			% tableName%channel_link).str();

		if (!res.next())
		{
			cppdb::statement stat = *db_ << (boost::format("INSERT INTO %s (name, channel_link, receive_url, "
				"pid, is_connected, channel_bitrate, update_time)"
				"values('%s', '%s', '%s', '%s', %s, %s, %s)")
				% tableName%name%channel_link%param.receive_url
				%msg.pid() % msg.is_connected() % msg.kbps() % time(NULL)).str();

			stat.exec();

			bool retSend = __insert_sender(param, errorMsg, functionName);

			return (stat.affected() == 1) && retSend;
		}
		else
		{
			cppdb::statement stat = *db_ << (boost::format("UPDATE %s SET "
				"pid = '%s', "
				"is_connected = %s, "
				"channel_bitrate = %s, "
				"update_time = %s "
				"WHERE channel_link = '%s'"
				) % tableName
				%msg.pid()
				% msg.is_connected()
				% msg.kbps()
				% time(NULL)
				% channel_link).str();
			stat.exec();

			return stat.affected() >= 0;
		}
	}
	catch (std::exception& e)
	{
		errorMsg = functionName + " error! " + e.what();
		std::cout << "\n" << errorMsg << "\n";
		log(errorMsg);
		return false;
	}
	catch (...)
	{
		errorMsg = functionName + " error! unknown error.";
		return false;
	}
	return true;
}

bool shunt_db::__insert_sender(const shunt_xml_param& param,
	std::string& errorMsg,
	const std::string& functionName)
{
	errorMsg.clear();

	try
	{
		const char* tableName = table_sender;
		std::string channel_link = param.id;

		BOOST_FOREACH(const std::string& url, param.send_urls)
		{
			__insert_sender(channel_link, url, errorMsg, functionName);
		}
	}
	catch (std::exception& e)
	{
		errorMsg = functionName + " error! " + e.what();
		std::cout << "\n" << errorMsg << "\n";
		log(errorMsg);
		return false;
	}
	catch (...)
	{
		errorMsg = functionName + " error! unknown error.";
		log(errorMsg);
		return false;
	}
	return true;
}

bool shunt_db::__insert_sender(const std::string& channel_link,
	const std::string& url, std::string& errorMsg, const std::string& functionName)
{
	errorMsg.clear();

	try
	{
		const char* tableName = table_sender;
		cppdb::statement stat = *db_ << (boost::format(
			"INSERT INTO %s (channel_link, url)values('%s', '%s')")
			% tableName%channel_link%url).str();
		stat.exec();

		return stat.affected() == 1;
	}
	catch (std::exception& e)
	{
		errorMsg = functionName + " error! " + e.what();
		std::cout << "\n" << errorMsg << "\n";
		log(errorMsg);
		return false;
	}
	catch (...)
	{
		errorMsg = functionName + " error! unknown error.";
		log(errorMsg);
		return false;
	}
	return true;
}

bool shunt_db::__get_channels(param_vector& params, std::string& errorMsg
	, const std::string& functionName)
{
	try
	{
		const char* sendTableName = table_sender;
		cppdb::result sendRes = *db_ << (boost::format("SELECT url, channel_link FROM %s") % sendTableName).str();

		typedef std::set<std::string>			urls_t;
		typedef std::map<std::string, urls_t>	link_urls_map_t;
		link_urls_map_t senders;

		std::string channel_link;
		while (sendRes.next())
		{
			channel_link = sendRes.get<std::string>("channel_link");
			urls_t& urls = senders[channel_link];
			urls.insert(sendRes.get<std::string>("url"));
		}

		const char* tableName = table_shunt;
		cppdb::result res = *db_ << (boost::format("SELECT name, channel_link, receive_url FROM %s") % tableName).str();

		shunt_xml_param param;
		std::string Name;

		while (res.next())
		{
			Name = res.get<std::string>("name");
			param.name = boost::filesystem::path(convert_to_wstring(hex_to_string(Name))).string();
			param.id = res.get<std::string>("channel_link");
			param.receive_url = res.get<std::string>("receive_url");
			param.send_urls = senders[param.id];
			params.push_back(param);
		}

		return true;

	}
	catch (const std::exception& e)
	{
		errorMsg = functionName + e.what();
		log(errorMsg);
		return false;
	}
	catch (...)
	{
		errorMsg = functionName;
		errorMsg += " unknown error";
		log(errorMsg);
		return false;
	}
}

bool shunt_db::__get_channel(shunt_xml_param& param, std::string& errorMsg
	, const std::string& functionName)
{

	try
	{
		const char* tableName = table_shunt;

		cppdb::result res = *db_ << (boost::format("SELECT name, receive_url FROM %s WHERE channel_link='%s'")
			% tableName%param.id).str();

		std::string Name;
		while (res.next())
		{
			Name = res.get<std::string>("name");
			param.name = boost::filesystem::path(convert_to_wstring(hex_to_string(Name))).string();
			param.receive_url = res.get<std::string>("receive_url");
		}
		return __get_send_urls(param, errorMsg, functionName);

	}
	catch (const std::exception& e)
	{
		errorMsg = functionName + e.what();
		log(errorMsg);
		return false;
	}
	catch (...)
	{
		errorMsg = functionName;
		errorMsg += " unknown error";
		log(errorMsg);
		return false;
	}

}

bool shunt_db::__get_send_urls(shunt_xml_param& param, std::string& errorMsg
	, const std::string& functionName)
{

	errorMsg.clear();
	try
	{
		const char* tableName = table_sender;
		std::string url;

		cppdb::result res = *db_ << (boost::format("SELECT url FROM %s WHERE channel_link='%s'")
			% tableName%param.id).str();

		while (res.next())
		{
			url = res.get<std::string>("url");
			param.send_urls.insert(url);
		}
		return true;
	}
	catch (const std::exception& e)
	{
		errorMsg = functionName + e.what();
		log(errorMsg);
		return false;
	}
	catch (...)
	{
		errorMsg = functionName;
		errorMsg += " unknown error";
		log(errorMsg);
		return false;
	}
}

bool shunt_db::__del_channel(const std::string& channel_link,
	std::string& errorMsg, const std::string& functionName)
{
	try
	{
		__check_shunt_table();

		cppdb::statement stat = *db_ << (boost::format("DELETE FROM %s where channel_link='%s'")
			% table_shunt%channel_link).str();
		stat.exec();

		return (stat.affected() == 1) && __del_channel_in_sender_table(channel_link, errorMsg, functionName);
	}
	catch (const std::exception& e)
	{
		errorMsg = functionName + " error: " + e.what();
		log(errorMsg);
		return false;
	}
	catch (...)
	{
		return false;
	}
}

bool shunt_db::__del_channel_in_sender_table(const std::string& channel_link,
	std::string& errorMsg, const std::string& functionName)
{
	try
	{
		__check_sender_table();

		cppdb::statement stat = *db_ << (boost::format("DELETE FROM %s where channel_link='%s'")
			% table_sender%channel_link).str();
		stat.exec();
		return 1 == stat.affected();

	}
	catch (const std::exception& e)
	{
		errorMsg = functionName + " error: " + e.what();
		log(errorMsg);
		return false;
	}
	catch (...)
	{
		return false;
	}
}

bool shunt_db::log(const std::string& msg, const std::string& level)
{
	try{
		__check_log_table();

		const char* tableName = "runlog";
		cppdb::statement stmt;

		boost::format insertSql("INSERT INTO %s "
			"(level, content, time) "
			" values('%s', '%s', %s)"
			);

		insertSql%tableName
			%level
			%msg
			%time(NULL);

		stmt = *db_ << insertSql.str();
		stmt.exec();

	}
	catch (const std::exception& e)
	{
		std::cout << e.what();
	}
	catch (...)
	{
	}
	return true;
}


void  shunt_db::__check_config_table()
{
	//创建table
	*db_ << (boost::format("CREATE table IF NOT EXISTS sys_config("
		"id int not null auto_increment, "
		"%s int not null default 0, "
		"%s int not null default 0, "
		"primary key(id)"
		")ENGINE=InnoDB DEFAULT CHARSET=utf8")
		% alive_alarm_port
		% operation_http_port).str() << cppdb::exec;

	cppdb::result res = *db_ << (boost::format("SELECT * FROM sys_config")).str();
	if (!res.next())
	{
		*db_ << (boost::format("INSERT INTO sys_config"
			"(id, %s, %s)"
			"values(0, 0, 0)")
			% alive_alarm_port % operation_http_port
			).str() << cppdb::exec;
	}
}

void shunt_db::__check_shunt_table()
{
	//创建table
	*db_ << ("CREATE table IF NOT EXISTS shunt("
		"id int not null auto_increment, "
		"name varchar(128) not null, "
		"channel_link varchar(128) unique not null, "
		"receive_url varchar(64) not null, "
		"pid CHAR(16) not null default 'stopped', "
		"is_connected tinyint(1) not null default 0, "
		"channel_bitrate int not null default 0, "
		"update_time long not null, "
		"primary key(id)"
		")ENGINE=InnoDB DEFAULT CHARSET=utf8") << cppdb::exec;
}

void shunt_db::__check_sender_table()
{
	*db_ << ("CREATE table IF NOT EXISTS sender("
		"id int not null auto_increment, "
		"channel_link varchar(128) not null, "
		"url varchar(128) unique not null, "
		"primary key(id)"
		")ENGINE=InnoDB DEFAULT CHARSET=utf8") << cppdb::exec;
}

void shunt_db::__check_log_table()
{
	//创建table
	std::string cmd = std::string("CREATE TABLE IF NOT EXISTS runlog(")
		+ "id int auto_increment, "
		+ "level varchar(34) not null, "
		+ "content varchar(255) not null, "
		+ "time long not null, "
		+ "primary key(id)"
		+ ")ENGINE=InnoDB DEFAULT CHARSET=utf8";

	*db_ << cmd << cppdb::exec;
}

NAMESPACE_END(p2control);
