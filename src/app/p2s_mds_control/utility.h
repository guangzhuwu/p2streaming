#ifndef p2s_mds_control_utility_h__
#define p2s_mds_control_utility_h__

#include <p2engine/push_warning_option.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/property_tree/ptree.hpp>   
#include <boost/property_tree/xml_parser.hpp>  
#include <boost/property_tree/detail/xml_parser_writer_settings.hpp>
#include <p2engine/pop_warning_option.hpp>

#include "common/utility.h"
#include "common/md5.h"
#include "common/parameter.h"

namespace utility{

	using namespace p2engine;
	using namespace p2common;
	using boost::property_tree::ptree;
	namespace cplxml = boost::property_tree::xml_parser ;
	//namespace pp{
	//	using namespace boost::interprocess::detail; 
	//	using namespace boost::interprocess::ipcdetail; 
	//};

	typedef std::vector<server_param_base> channel_vec_type;

	typedef http::basic_http_connection<http::http_connection_base> http_connection;
	typedef http::basic_http_acceptor<http_connection, http_connection>	http_acceptor;
	typedef p2engine::rough_timer timer;
	typedef boost::uint16_t  node_id_t;

	typedef std::map<node_id_t,  channel_vec_type> node_map_type;
	typedef struct 
	{
		node_map_type node_map;
		std::string   session_id;
	}node_map_t;

	static const char* const CONFIG_LIVE_XML = "live_mds_xml.xml";
	static const char* const CONFIG_VOD_XML = "vod_mds_xml.xml";
	enum{MAX_CHANNEL_NUM_PER_SERVER=100};
	enum{MDS_RESUME_THRESH=2, INTERVAL=5};
	enum CMS_CMD{INVALID_CMD=0, ADD_CHANNEL, DEL_CHANNEL, START_CHANNEL, STOP_CHANNEL, RESET_CHANNEL};

	static const std::string root_key = "p2s_mds";
	static const std::string server_key = "server";
	static const std::string server_id_key = "id";
	static const std::string server_prop_key = "prop";
	static const std::string channel_count_key = "count";

	static const std::string channel_key = "channel";
	static const std::string channel_type_key = "type";
	static const std::string channel_path_key = "path";
	static const std::string channel_id_key = "uuid";
	static const std::string channel_name_key = "name";
	static const std::string channel_prop_key = "prop";

	static const std::string channel_stream_recv_port_key = "stream_recv_port";
	static const std::string channel_stream_recv_url_key = "stream_recv_url";
	static const std::string channel_in_addr_key = "internal_address";
	static const std::string channel_ex_addr_key = "external_address";
	static const std::string channel_tracker_addr_key = "tracker_address";
	static const std::string channel_key_key = "channel_key";
	static const std::string channel_link_key = "channel_link";

	static const std::string channel_duration_key = "duration";
	static const std::string channel_length_key  = "length";

	static const std::string OPER_NEW = "new";
	static const std::string OPER_RUN = "run";
	static const std::string OPER_DEL = "del";
	static const std::string OPER_ERROR = "error";
	static const std::string OPER_STOP = "stop";
	static const std::string OPER_RESET = "reset";
	static const std::string OPER_INVALID = "invalid";

	typedef std::map<std::string, std::string> config_map_type;
	typedef config_map_type::value_type config_value_type;


	/*//�������·������hash�����䵽��Ӧ��NODE*/
	inline node_id_t hash_to_node(const char* psz_channel_url, std::size_t url_len, 
		int max_channel_per_server = MAX_CHANNEL_NUM_PER_SERVER)
	{
		node_id_t h = MurmurHash(psz_channel_url, url_len);
		return (h % max_channel_per_server);
	}

	node_map_type hash_node_map(const int& max_process, channel_vec_type& channels);

	//void parse_request(const std::string& req, config_map_type& config_map);
	inline boost::filesystem::path current_path()
	{
#ifdef _WIN32

		return p2engine::get_exe_path();
		/*
		std::vector<char> szPath;
		szPath.resize(MAX_PATH+1);
		int nCount = GetModuleFileName(NULL, &szPath[0], MAX_PATH);
		return boost::filesystem::path(std::string(&szPath[0], nCount)).parent_path();
		*/
#else
		return boost::filesystem::current_path();
#endif
	}
};

#endif // p2s_mds_control_utility_h__
