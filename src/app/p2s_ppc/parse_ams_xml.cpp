#include "p2s_ppc/parse_ams_xml.h"
#include "p2engine/p2engine/macro.hpp"
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

NAMESPACE_BEGIN(ppc)
parse_ca_xml::parse_ca_xml(const std::string& str_xml_content)
:b_parsed_(false), 
 xml_content_(str_xml_content)
{
	init_parse_xml();
}

void parse_ca_xml::init_parse_xml()
{
	BOOST_ASSERT(!b_parsed_);

	std::stringstream strstream(xml_content_);
	try{
		typedef boost::property_tree::ptree ptree;
		ptree pt;
		boost::property_tree::xml_parser::read_xml(strstream, pt);
		ptree server_node = pt.get_child("xml.servers");

		BOOST_FOREACH(ptree::value_type &v, server_node){
			cms_server_info tem_server;
			tem_server.server_id_ = v.second.get_child("id").get_value<int>();
			tem_server.server_type_ = v.second.get_child("type").get_value<int>();
			tem_server.server_address_ = v.second.get_child("address").get_value<std::string>();
			tem_server.server_ca = v.second.get_child("ca").get_value<std::string>();
			servers_.push_back(tem_server);
		}
	}catch(...){}
		

	b_parsed_ = true;
}

void parse_ca_xml::content_reset(const std::string& str_xml_content)
{
	b_parsed_ = false;
	xml_content_ = str_xml_content;
	servers_.clear();

	init_parse_xml();
}
NAMESPACE_END(ppc);