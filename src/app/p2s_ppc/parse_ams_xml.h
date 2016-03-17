#ifndef _PASE_AMS_XML_H_
#define _PASE_AMS_XML_H_
#include "p2s_ppc/typedef.h"
#include <string>
#include <vector>
namespace ppc{
	struct cms_server_info{
		int server_id_;
		int server_type_;
		std::string server_address_;
		std::string server_ca;
	};

	class parse_ca_xml{
		typedef parse_ca_xml this_type;
		SHARED_ACCESS_DECLARE;
	public:
		static shared_ptr create(const std::string& str_xml_content)
		{
			return shared_ptr(new parse_ca_xml(str_xml_content), 
				shared_access_destroy<this_type>());
		}
	protected:
		parse_ca_xml(const std::string& str_xml_content);
		~parse_ca_xml(){}
	public:
		void content_reset(const std::string& str_xml_content);
	public:
		int server_count(){
			BOOST_ASSERT(b_parsed_);
			return servers_.size();
		}

		cms_server_info& get_server(const int index){
			BOOST_ASSERT(index>=0 && index<servers_.size());
			return servers_[index];
		}
	private:
		void init_parse_xml();
	private:
		bool b_parsed_;
		std::string xml_content_;
		std::vector<cms_server_info> servers_;
	};

}
#endif //_PASE_AMS_XML_H_