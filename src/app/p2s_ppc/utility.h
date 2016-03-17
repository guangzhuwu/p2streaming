#ifndef _P2S_PPC_UTILITY_
#define _P2S_PPC_UTILITY_
#include <boost/property_tree/ptree.hpp>
namespace ppc
{
	boost::property_tree::ptree get_string_ptree(const std::string& _str_xml_data);

	std::string get_ptree_string(const boost::property_tree::ptree& pt);

	std::string get_error_xml_string(const int error_code, const std::string& msg);
}
#endif //_P2S_PPC_UTILITY_