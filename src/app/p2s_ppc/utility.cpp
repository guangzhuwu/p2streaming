#include "p2s_ppc/utility.h"
#include "p2engine/p2engine/macro.hpp"
#include <boost/property_tree/xml_parser.hpp>
NAMESPACE_BEGIN(ppc)
boost::property_tree::ptree get_string_ptree(const std::string& _str_xml_data)
{
	boost::property_tree::ptree res_tree;
	std::stringstream stream_buf;
	stream_buf.str(_str_xml_data);

	try{
		boost::property_tree::xml_parser::read_xml(stream_buf, res_tree);
	}catch(...){}

	return res_tree;
}

std::string get_ptree_string(const boost::property_tree::ptree& pt)
{
	std::stringstream ssm;
	try
	{
		boost::property_tree::xml_parser::write_xml(ssm, pt);
	}catch(...){}

	return ssm.str();
}

std::string get_error_xml_string(const int error_code, const std::string& msg)
{
	const std::string root_key = "datainfo";
	const std::string error_key = "errcode";
	const std::string error_msg_key = "msg";
	boost::property_tree::ptree pt;

	pt.add(root_key + "." + error_key, error_code);
	pt.add(root_key + "." + error_msg_key, msg);

	std::stringstream ssm;
	try
	{
		boost::property_tree::xml_parser::write_xml(ssm, pt);
	}catch(...){}

	return ssm.str();
}

NAMESPACE_END(ppc)