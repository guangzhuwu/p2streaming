#include "p2s_ppc/request_session_ams.h"
#include "p2s_ppc/channel_req_processor.h"
#include "p2s_ppc/parse_ams_xml.h"
#include "p2s_ppc/rsa_rc4.h"
#include "p2s_ppc/utility.h"
#include <p2engine/p2engine.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/format.hpp>
#include <boost/filesystem.hpp>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/hmac.h>

using namespace p2engine;
namespace ppc{
	const std::string ams_xml_root_key = "xml";
	const std::string ams_account_key = "account_id";
	const std::string ams_user_key = "user_id";
	const std::string ams_apikey_key = "api_key";
	const std::string ams_sharedkey_key = "sig_key";

	static const std::string return_xml_root_key = "datainfo";
	static const std::string return_xml_error_key = "errcode";
	static const std::string return_xml_error_msg_key = "msg";

	namespace{
		static int32_t get_ams_timestamp()
		{
			/*Unix Timestamp, i.e. elapsed secondssince Midnight, Jan 1st 1970, UTC*/
			using namespace boost::gregorian;
			boost::posix_time::ptime time_1970(date(1970, boost::date_time::Jan, 1), 
				                               boost::posix_time::hours(0));

			boost::posix_time::ptime tnow = boost::posix_time::second_clock::universal_time();
			boost::posix_time::time_duration diff = tnow - time_1970;
			return diff.total_seconds();
		}

		static std::string get_ams_authorization_sign(const std::string& api_key, 
			const std::string& content, const std::string& key)
		{
			unsigned char tem_res[1024];
			unsigned int len = 1024;
			HMAC(EVP_sha1(), key.c_str(), key.size(), 
				(const unsigned char*)content.c_str(), content.size(), 
				&tem_res[0], &len);

			std::string strtemp;
			strtemp.append((char*)&tem_res[0], len);
			strtemp = p2common::base64encode(strtemp);

			std::string str_res = "LR-AMS ";
			str_res.append(api_key);
			str_res.append(":");
			str_res.append(strtemp);

			return str_res;
		}

		static std::string merge_ams_result_data(parse_ca_xml::shared_ptr _ca_xml, 
			std::map<int, std::string> _xml_data)
		{
			BOOST_ASSERT(_xml_data.size()>0);

			boost::property_tree::ptree pt;
			std::map<int, std::string>::iterator itr = _xml_data.begin();

			while(itr!=_xml_data.end()){
				boost::property_tree::ptree ptroot = get_string_ptree(itr->second);
				boost::property_tree::ptree ptserver;

				try{
					ptserver.add("id", _ca_xml->get_server(itr->first).server_id_);
					ptserver.add("key", ptroot.get_child("datainfo.key").get_value<std::string>());
					ptserver.add("cas", ptroot.get_child("datainfo.cas").get_value<std::string>());
					ptserver.add_child("channels", ptroot.get_child("datainfo.channellist"));
				}catch(...){}

				pt.add_child("datainfo.servers.server", ptserver);
				itr++;
			}

			return get_ptree_string(pt);
		}
	}

	bool request_session_ams::process(boost::shared_ptr<channel_req_processor> processor)
	{
		if(!processor)
			return false;

		processor_ = processor;

		if(need_cert_ && !processor->is_login())
		{
			send_out(get_error_xml_string(1, "You have not logged in."));
			return true;
		}

		if(processor->is_key_ready())
			return do_connect();

		processor->genkey(1024, boost::bind(&this_type::do_connect, this));
		return true;
	}

	safe_buffer request_session_ams::get_request_buf()
	{
		BOOST_AUTO(processor, processor_.lock());
		if(!processor)
			return safe_buffer();
		std::string request_path = format_url();
		http::request req;
		error_code ec;
		uri host_uri(get_host(), ec);
		int port=host_uri.port()?host_uri.port():80;
		req.host(host_uri.host(), port);

		safe_buffer buf;
		safe_buffer_io io(&buf);

		if(b_request_get_)
		{
			req.method(http::HTTP_METHORD_GET);
			req.url(request_path);

			std::string str_sign_content = "GET";
			str_sign_content.append(request_path);

			req.set("Authorization", get_ams_authorization_sign(
				processor->api_key(), str_sign_content, processor->ams_shared_secret_key()));
			io<<req;
		}
		else
		{
			req.method(http::HTTP_METHORD_POST);
			std::string str_path = request_path.substr(0, request_path.find_first_of('?'));
			std::string str_condition = request_path.substr(request_path.find_first_of('?')+1);
			req.url(str_path);

			std::string str_sign_content = http::HTTP_METHORD_POST;
			str_sign_content.append(str_path);
			str_sign_content.append(str_condition);
			req.set("Authorization", get_ams_authorization_sign(
				processor->api_key(), str_sign_content, processor->ams_shared_secret_key()));
			req.content_length(str_condition.size());
			req.set("Content-Type", "application/x-www-form-urlencoded");
			io<<req<<str_condition;
		}

		return buf;
	}

	std::string request_session_ams::get_host()
	{
		if(need_cert_)
		{
			BOOST_AUTO(processor, processor_.lock());
			if(!processor)
				return "";

			return processor->ams_host();
		}
		else
		{
			return get_ams_host(qmap());
		}
	}

	void request_session_ams::process_data(connection_type* conn, const safe_buffer& buf)
	{
		if(content_.length()!=length_)
			content_.append(buffer_cast<char*>(buf), buffer_size(buf));

		if(content_.length() != length_)
			return;

		send_out(content_);
	}

	void req_ams_login::process_data(connection_type* conn, const safe_buffer& buf)
	{
		content_.append(buffer_cast<char*>(buf), buffer_size(buf));
		if(content_.length() != length_)
			return;

		BOOST_AUTO(processor, processor_.lock());
		if(!processor)
			return;

		try{
			if(is_respones_status_ok(reponse_type_))
			{
				std::string str_username = get_value(qmap(), "username");
				std::string str_account_id = "";
				std::string str_api_key = "";
				std::string str_shared_key = "";
				boost::property_tree::ptree xml_root = get_string_ptree(content_);
				xml_root = xml_root.get_child(ams_xml_root_key);

				if(xml_root.find(ams_user_key) == xml_root.not_found())
					str_account_id = xml_root.get_child(ams_account_key).get_value<std::string>();
				else
					str_account_id = xml_root.get_child(ams_user_key).get_value<std::string>();

				str_api_key = xml_root.get_child(ams_apikey_key).get_value<std::string>();
				str_shared_key = xml_root.get_child(ams_sharedkey_key).get_value<std::string>();

				processor->ams_login_success(str_account_id, str_api_key, str_shared_key);

				//���ص�¼�ɹ�
				send_out(get_error_xml_string(0, "login sucess."));
			}
			else
			{
				send_out(content_);
			}
		}
		catch(...){
			send_out(get_error_xml_string(0, "login failed."));
		}
	}

	std::string req_ams_login::format_url()
	{
		BOOST_AUTO(processor, processor_.lock());
		if(!processor)
			return "";

		std::string device_id = get_value(qmap(), "device_id");
		std::string username = hex_to_string(get_value(qmap(), "username"));
		std::string password = hex_to_string(get_value(qmap(), "password"));
		std::string pub_key = processor->public_key();

		processor->assign_ams_log_info(username, device_id, get_host());
		boost::format str_fmt("/accounts/login.xml?device_id=%s&username=%s&password=%s&pub_key=%s");
		str_fmt%device_id%username%password%p2common::string_to_hex(pub_key);
		return str_fmt.str();
	}

	void req_ams_packages::process_data(connection_type* conn, const safe_buffer& buf)
	{
		request_session_ams::process_data(conn, buf);
	}

	std::string req_ams_packages::format_url()
	{
		BOOST_AUTO(processor, processor_.lock());
		if(!processor)
			return "";

		std::string str_page = get_value(qmap(), "page");

		if(str_page.empty())
			str_page = "1";

		std::string str_status = get_value(qmap(), "status");
		std::string str_type = get_value(qmap(), "type");

		boost::format str_fmt("/content/packages.xml?timestamp=%d&page=%d&type=%s&status=%s");
		str_fmt%get_ams_timestamp()%str_page%str_type%str_status;
		return str_fmt.str();
	}

	void req_ams_logout::process_data(connection_type* conn, const safe_buffer& buf)
	{
		request_session_ams::process_data(conn, buf);
	}

	std::string req_ams_logout::format_url()
	{
		boost::format str_fmt("/accounts/logout.xml?timestamp=%d");
		str_fmt%get_ams_timestamp();
		return str_fmt.str();
	}

	void req_ams_get_ca::process_data(connection_type* conn, const safe_buffer& buf)
	{
		if(content_.length()!=length_)
			content_.append(buffer_cast<char*>(buf), buffer_size(buf));

		if(content_.length()==length_||length_==-1)
		{
			BOOST_AUTO(processor, processor_.lock());
			if(!processor)
				return;

			std::string cms_server = hex_to_string(get_value(qmap(), "cms_host"));
			processor->ams_get_ca_sucess(SHARED_OBJ_FROM_THIS, content_, cms_server);
		}
	}

	std::string req_ams_get_ca::format_url()
	{
		std::string str_server = hex_to_string(get_value(qmap(), "cms_host"));
		std::string str_type = get_value(qmap(), "type");
		str_type = str_type.empty()? "p2s":str_type;

		std::string str_status = get_value(qmap(), "status");
		if(str_status.empty())
		{
			boost::format str_fmt("/content/ca/%s.xml?timestamp=%d&type=%s");
			str_fmt%str_server.c_str()%get_ams_timestamp()%str_type;
			return str_fmt.str();
		}
		else
		{
			boost::format str_fmt("/content/ca/%s.xml?timestamp=%d&type=%s&status=%s");
			str_fmt%str_server.c_str()%get_ams_timestamp()%str_type%str_status;
			return str_fmt.str();
		}
	}

	void req_ams_new_ca::process_data(connection_type* conn, const safe_buffer& buf)
	{
		content_.append(buffer_cast<char*>(buf), buffer_size(buf));
		if(content_.length()!=length_)
			return;

		if(is_respones_status_ok(reponse_type_))
		{
			BOOST_AUTO(processor, processor_.lock());
			BOOST_ASSERT(processor);

			processor->ams_get_ca_sucess(SHARED_OBJ_FROM_THIS, content_);
		}
		else
		{
			send_out(content_);
		}
	}

	std::string req_ams_new_ca::format_url()
	{
		std::string str_type = get_value(qmap(), "type");
		std::string str_package = get_value(qmap(), "package");
		boost::format str_fmt("/content/ca/%s.xml?type=package&timestamp=%s");
		str_fmt%str_package%get_ams_timestamp();
		return str_fmt.str();
	}


	void req_ams_get_package_info::process_data(connection_type* conn, 
		const safe_buffer& buf)
	{
		request_session_ams::process_data(conn, buf);
	}

	std::string req_ams_get_package_info::format_url()
	{
		std::string str_id = get_value(qmap(), "id");
		boost::format str_fmt("/content/packages/%s.xml?timestamp=%d");
		str_fmt%str_id%get_ams_timestamp();
		return str_fmt.str();
	}

	void req_ams_purchase_package::process_data(connection_type* conn, 
		const safe_buffer& buf)
	{
		request_session_ams::process_data(conn, buf);
	}

	std::string req_ams_purchase_package::format_url()
	{
		std::string str_id = get_value(qmap(), "id");
		std::string str_price_id = get_value(qmap(), "price_id");
		boost::format strfmt("/content/purchase.xml?timestamp=%d&id=%s&price_id=%s");
		strfmt%get_ams_timestamp()%str_id%str_price_id;
		return strfmt.str();
	}

	void req_ams_transactions::process_data(connection_type* conn, 
		const safe_buffer& buf)
	{
		request_session_ams::process_data(conn, buf);
	}

	std::string req_ams_transactions::format_url()
	{
		std::string str_page = get_value(qmap(), "page");
		boost::format strfmt("/accounts/transactions.xml?timestamp=%d&page=%s");
		strfmt%get_ams_timestamp()%str_page;
		return strfmt.str();
	}

	void req_ams_billing_info::process_data(connection_type* conn, 
		const safe_buffer& buf)
	{
		request_session_ams::process_data(conn, buf);
	}

	std::string req_ams_billing_info::format_url()
	{
		boost::format strfmt("/accounts/billing_info.xml?timestamp=%d&first_name=%s&last_name=%s&company=%s&address1=%s&address2=%s&city=%s&state=%s&postal_code=%s&country=%s&card_type=%s&card_number=%s&expiration_month=%s&expiration_year=%s");
		strfmt%get_ams_timestamp()
			%get_value(qmap(), "first_name")
			%get_value(qmap(), "last_name")
			%get_value(qmap(), "company")
			%get_value(qmap(), "address1")
			%get_value(qmap(), "address2")
			%get_value(qmap(), "city")
			%get_value(qmap(), "state")
			%get_value(qmap(), "postal_code")
			%get_value(qmap(), "country")
			%get_value(qmap(), "card_type")
			%get_value(qmap(), "card_number")
			%get_value(qmap(), "expiration_month")
			%get_value(qmap(), "expiration_year");
		return strfmt.str();
	}

	void req_ams_registration::process_data(connection_type* conn, 
		const safe_buffer& buf)
	{
		request_session_ams::process_data(conn, buf);
	}

	std::string req_ams_registration::format_url()
	{
		std::string str_username = hex_to_string(get_value(qmap(), "username"));
		std::string str_password = hex_to_string(get_value(qmap(), "password"));
		boost::format strfmt("/accounts/registration.xml?timestamp=%d&username=%s&password=%s");
		strfmt%get_ams_timestamp()%str_username%str_password;
		return strfmt.str();
	}

	std::string req_ams_session_cms::format_url()
	{
		BOOST_AUTO(processor, processor_.lock());

		BOOST_ASSERT(processor);

		boost::format strfmt("/cms/interface.php%s&ca_data=%s&ams_host=%s");
		strfmt%processor->ams_channellist_qmap()%p2common::string_to_hex(processor->ams_ca_data())%processor->ams_host();
		return strfmt.str();
	}

	std::string req_ams_session_cms::get_host()
	{
	    BOOST_AUTO(processor, processor_.lock());
		BOOST_ASSERT(processor);
		return processor->ams_cms_host();
	}

	void req_ams_session_cms::process_data(connection_type* conn, const safe_buffer& buf)
	{
		request_session_base::process_data(conn, buf);
	}

	req_ams_session_cms_coll::req_ams_session_cms_coll(connection_sptr _client, 
		                const std::string& ca_data, uri& _url)
    :request_session_buf_base(_client, _url)
	{
		ca_xml_parser_ = parse_ca_xml::create(ca_data);
	}

	bool req_ams_session_cms_coll::process(boost::shared_ptr<channel_req_processor> processor)
	{
		processor_ = processor;

		for(int i=0; i<ca_xml_parser_->server_count(); i++)
		{
			boost::shared_ptr<req_new_ams_session_cms> session 
				= req_new_ams_session_cms::create(client_, uri_);
			cmssession_serverindex_[session] = i;
			session->process(processor, SHARED_OBJ_FROM_THIS);
		}
		return true;
	}

	std::string req_ams_session_cms_coll::get_cms_host(boost::shared_ptr<req_new_ams_session_cms> session)
	{
		BOOST_ASSERT(cmssession_serverindex_.find(session) != cmssession_serverindex_.end());
		return ca_xml_parser_->get_server(cmssession_serverindex_[session]).server_address_; 
	}

	std::string req_ams_session_cms_coll::get_ca_data(boost::shared_ptr<req_new_ams_session_cms> session)
	{
		BOOST_ASSERT(cmssession_serverindex_.find(session) != cmssession_serverindex_.end());
		return ca_xml_parser_->get_server(cmssession_serverindex_[session]).server_ca;
	}

	void req_ams_session_cms_coll::sucess_get_cms_data(boost::shared_ptr<req_new_ams_session_cms> session, 
		                                               safe_buffer recdata)
	{
		safe_buffer_io buf_io(&recdata);
		std::string str_data;
		buf_io>>str_data;

		cms_results_[cmssession_serverindex_[session]] = str_data;
		cmssession_serverindex_.erase(session);
		if(cmssession_serverindex_.empty())
			send_out(merge_ams_result_data(ca_xml_parser_, cms_results_));
	}

	boost::asio::io_service& req_ams_session_cms_coll::get_io_service()
	{
		BOOST_AUTO(processor, processor_.lock());
		BOOST_ASSERT(processor);
		return processor->get_io_service();
	}

	std::string req_new_ams_session_cms::format_url()
	{
		BOOST_AUTO(processor, processor_.lock());
		BOOST_AUTO(coll, session_coll_.lock());
		BOOST_ASSERT(processor && coll);
		std::string str_type = get_value(qmap(), "type").empty()? "p2s":get_value(qmap(), "type");
		std::string str_lang = get_value(qmap(), "lang").empty()? "en":get_value(qmap(), "lang");
		std::string str_ams_host = processor->ams_host();
		std::string str_cms_pre = "/cms/interface.php?op=ams";
		std::string str_act = "items";
		if(get_value(qmap(), "cmd") == "get_ams_channel_list")
		{
			str_act = "items";
		}

		boost::format strfmt("%s&act=%s&type=%s&lang=%s&ams_host=%s&ca_data=%s");
		strfmt%str_cms_pre%str_act%get_value(qmap(), "type")%str_lang
			%str_ams_host%p2common::string_to_hex(coll->get_ca_data(SHARED_OBJ_FROM_THIS));
		return strfmt.str();
	}

	std::string req_new_ams_session_cms::get_host()
	{
		BOOST_AUTO(coll, session_coll_.lock());
		BOOST_ASSERT(coll);
		return coll->get_cms_host(SHARED_OBJ_FROM_THIS);
	}

	void req_new_ams_session_cms::process_data(connection_type* conn, const safe_buffer& buf)
	{
		process_encyped_data(conn, buf);
	}

	bool req_new_ams_session_cms::process(boost::shared_ptr<channel_req_processor> processor, 
		boost::shared_ptr<req_ams_session_cms_coll> coll)
	{
		processor_ = processor;
		session_coll_ = coll;

		processor->get_io_service().post(
			boost::bind(&this_type::do_connect, SHARED_OBJ_FROM_THIS));

		return true;
	}

	void req_new_ams_session_cms::process_encyped_data(connection_type* conn, const safe_buffer& buf)
	{
		if(content_.length()!=length_)
			content_.append(buffer_cast<char*>(buf), buffer_size(buf));

		if(content_.length()==length_||length_==-1)
		{
			BOOST_AUTO(coll, session_coll_.lock());
			BOOST_AUTO(processor, processor_.lock());
			BOOST_ASSERT(coll);
			if (coll)
				coll->sucess_get_cms_data(SHARED_OBJ_FROM_THIS, 
				       get_decrypt_data(content_, processor->rsa_key()));
		}
	}
}