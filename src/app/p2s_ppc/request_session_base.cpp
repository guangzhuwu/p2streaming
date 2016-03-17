#include "request_session_base.h"
#include "channel_req_processor.h"
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/format.hpp>
#include <boost/filesystem.hpp>
#include <boost/scope_exit.hpp>

#include <string>
#include <openssl/rsa.h>
#include <openssl/rc4.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include "rsa_rc4.h"

#define OLD_PROTOCAL 1
//#undef  OLD_PROTOCAL 


#if !defined(_DEBUG_SCOPE) && defined(NDEBUG)
#	define  REQUEST_SESSION_DBG(x) x
#else 
#	define  REQUEST_SESSION_DBG(x) x
#endif

#ifdef BOOST_MSVC
#	pragma comment(lib, "libeay32MT.lib")
#	pragma comment(lib, "ssleay32MT.lib")
#	pragma comment(lib, "Gdi32.lib")
#	pragma comment(lib, "User32.lib")
#endif

using namespace  p2engine;

namespace ppc{
	std::string g_orig_mac_id;

	const char* pszUserAgent = "P2SP2V "P2S_PPC_VERSION;
	const char* xml_code_typd = "UTF-8";
	const char* auth_ca_prefix = "/ca/auth/?";
	const char* cms_url_prefix = "/cms/interface.php?";
	const char* PACKAGE_HASH = "package_hash";
	const char* HTTP_CERT_SIGN = "sign";
	const char* HTTP_PUBLIC_KEY="pkey";
	const char* HTTPS_PROTOCOL="https";
	const int   HTTPS_PORT=443;

	namespace cplxml = boost::property_tree::xml_parser ;
	namespace{
		std::string format_cms_prefix(const uri& u)
		{
#ifdef OLD_PROTOCAL
			const qmap_type& qmap=u.query_map();
			std::string url=u.to_string();

			boost::replace_first(url, "/?", 				cms_url_prefix); 

			TODO("�����ֶΣ��������ĳɼ��ݺ� ��������ȥ��");
			boost::replace_first(url, "language", 		"lang");
			boost::replace_first(url, "channel_uuid", 	"itemid");
			boost::replace_first(url, "child_uuid", 		"childrenid");
			TODO("END");

			//Ĭ�ϲ�������
			std::string area=get_value(qmap, "area");
			if (area.empty())
				area="0";
			url.append("&area=");
			url.append(area.c_str(), area.length());
			return url;
#else
			std::string url=u.to_string();
			boost::replace_first(url, "/?", 	cms_url_prefix); 
			return url;
#endif
		}

		inline boost::format key_format(const std::string& cmdKey, 
			const std::string& dataKey)
		{
			return boost::format("ppc.%s.%s")%cmdKey%dataKey;
		}

		//д�뵥��session����, ��ͬsession������д����ͬ���ļ������ж�����key
		static void save_to_local(const std::string& cmdKey, 
			const request_session_buf_base::cache& _Cache)
		{
			using boost::property_tree::ptree;
			ptree pt;
			try{
				std::string hexCmdKey=string_to_hex(cmdKey);
				if(boost::filesystem::exists(boost::filesystem::path(hexCmdKey)))
					boost::filesystem::remove(hexCmdKey);

				//�������key_pair
				std::string random_key=md5(boost::lexical_cast<std::string>(random(1, 0x7fffffff)));
				pt.put("ppc.key", string_to_hex(random_key)); //������

				std::pair<std::string, std::string> shared_key_pair = security_policy::generate_key_pair(); //<pub, priv>
				std::string rc4_seed=security_policy::generate_shared_key(shared_key_pair.second, random_key);

				rc4 rc4_encoder;
				rc4_encoder.rc4_init((unsigned char*)rc4_seed.c_str(), rc4_seed.length());

				std::string etag=_Cache.etag;
				std::string data=_Cache.data;
				rc4_encoder.rc4_crypt(const_cast<char*>(etag.c_str()), etag.length());
				rc4_encoder.rc4_crypt(const_cast<char*>(data.c_str()), data.length());

				//rc4����	
				pt.put(key_format(cmdKey, "etag").str(), string_to_hex(etag));
				pt.put(key_format(cmdKey, "data").str(), string_to_hex(data));

				cplxml::write_xml(hexCmdKey, pt, std::locale(), 
					cplxml::xml_writer_make_settings<char>('*', 0, "UTF-8")
					);  
			}
			catch(...){
			}
		}

		static bool load_from_local(const std::string& cmdKey, 
			request_session_buf_base::cache& _Cache, std::string& errorMsg)
		{
			using boost::property_tree::ptree;
			try{
				if (!boost::filesystem::exists(string_to_hex(cmdKey)))
				{
					return false;
				}
				ptree pt;
				read_xml(string_to_hex(cmdKey), pt);

				boost::optional<std::string> op_key=pt.get_optional<std::string>("ppc.key");
				if(!op_key)
					return false;
				std::string random_key=hex_to_string(*op_key);
				std::pair<std::string, std::string> shared_key_pair = security_policy::generate_key_pair(); //<pub, priv>
				std::string rc4_seed = security_policy::generate_shared_key(shared_key_pair.second, random_key);

				boost::optional<std::string> op_etag=pt.get_optional<std::string>(key_format(cmdKey, "etag").str());
				boost::optional<std::string> op_data=pt.get_optional<std::string>(key_format(cmdKey, "data").str());

				rc4 rc4_decoder;
				rc4_decoder.rc4_init((unsigned char*)rc4_seed.c_str(), rc4_seed.length());

				if(op_etag)
				{
					_Cache.etag=hex_to_string(*op_etag);
					rc4_decoder.rc4_decrypt(const_cast<char*>(_Cache.etag.c_str()), _Cache.etag.length());//����
				}
				if(op_data)
				{
					_Cache.data=hex_to_string(*op_data);
					rc4_decoder.rc4_decrypt(const_cast<char*>(_Cache.data.c_str()), _Cache.data.length());//����
				}
				return true;
			}
			catch(std::exception& e){
				errorMsg=e.what();
				return false;
			}
			catch(...){
				return false;
			}
		}
	}

	request_session_buf_base::request_session_buf_base(connection_sptr _client, const uri& u)
	:client_(_client), uri_(u)
	{
		BOOST_ASSERT(!get_value(qmap(), "cmd").empty());
		session_id_=get_value(qmap(), "cmd");
#ifdef OLD_PROTOCAL
		session_cmd_=session_id_;
#endif
		std::string op=get_value(qmap(), "op");
		if(!op.empty())
		{
			session_id_.append("_");
			session_id_.append(op.c_str(), op.length());
		}

		//��ȡ������б��etag
		std::string errorMsg;

		load_from_local(session_id_, cache_, errorMsg);
		cache_.is_modified=true;
	}

	void request_session_buf_base::send_out(const safe_buffer& buf)
	{
		if(!client_||!client_->is_open())
		{
			request_session_buf_base::shared_ptr sess=SHARED_OBJ_FROM_THIS;
			BOOST_SCOPE_EXIT((&sess))
			{
				sess->erase();
			}
			BOOST_SCOPE_EXIT_END
				return;
		}

		http::response res;
		res.status(http::header::HTTP_OK);
		res.content_type("application/xml");
		res.content_length(buf.length());

		safe_buffer send_buf;
		safe_buffer_io bio(&send_buf);
		bio<<res;
		client_->async_send(send_buf);//����ͷ
		client_->async_send(buf);//��������

		client_->register_writable_handler(boost::bind(
			&this_type::handle_sentout_and_close, 
			this, client_.get()
			));

		if(cache_.is_modified)
		{
			BOOST_AUTO(processor, processor_.lock());
			if (processor)
			{
				cache_.data.assign(buffer_cast<char*>(buf), buf.length());
				processor->get_io_service().post(
					boost::bind(&save_to_local, session_id_, cache_)
					);
			}
		}

		REQUEST_SESSION_DBG(
			std::cout<<std::string(buffer_cast<char*>(buf), buf.length())<<std::endl;
		);
	}

	void request_session_buf_base::send_out(const std::string& buf)
	{
		safe_buffer data;
		safe_buffer_io io(&data);
		io<<buf;
		send_out(data);
	}

	void request_session_buf_base::handle_sentout_and_close(connection_type* conn)
	{
		conn->unregister_writable_handler();
		conn->close();

		request_session_buf_base::shared_ptr sess=SHARED_OBJ_FROM_THIS;
		BOOST_SCOPE_EXIT((&sess))
		{
			sess->erase();
		}
		BOOST_SCOPE_EXIT_END
	}

	void request_session_buf_base::erase()
	{
		BOOST_AUTO(processor, processor_.lock());
		if (processor)
			processor->post_erase(SHARED_OBJ_FROM_THIS);
	}

	void req_cbms_xml::process_unencyped_data(connection_type* conn, const safe_buffer& buf)
	{
		if(content_.length()!=length_)
			content_.append(buffer_cast<char*>(buf), buffer_size(buf));

		if(content_.length()==length_||length_<0)
		{
			send_out(content_);
		}
	}

	void req_cas_challenge::process_unencyped_data(connection_type* conn, const safe_buffer& buf)
	{
		if(content_.length()!=length_)
			content_.append(buffer_cast<char*>(buf), buffer_size(buf));

		if(content_.length()==length_)
		{
			try{
				using boost::property_tree::ptree;  
				ptree pt; 
				std::stringstream sstrm;
				sstrm.str(content_);
				read_xml(sstrm, pt);
#ifdef OLD_PROTOCAL
				boost::optional<std::string> cas_challenge = pt.get_optional<std::string>("auth.ca.challenge");
#else
				boost::optional<std::string> cas_challenge = pt.get_optional<std::string>("auth.ca");
#endif
				BOOST_AUTO(processor, processor_.lock());
				if (processor&&cas_challenge)
					processor->set_cas_challenge(SHARED_OBJ_FROM_THIS, *cas_challenge);
				else
					do_request();

				REQUEST_SESSION_DBG(
					if(cas_challenge)
						std::cout<<"received ca challenge: "<<*cas_challenge<<std::endl;
				);
			}
			catch(...){
				std::cout<<"challenge request failed!\n";
				do_request();
			}
			std::cout<<"req_cas_challenge sucess"<<std::endl;
		}
	}

	void req_cert::process_unencyped_data(connection_type* conn, const safe_buffer& buf)
	{
		OBJ_PROTECTOR(this_protector);

		if(content_.length()!=length_)
			content_.append(buffer_cast<char*>(buf), buffer_size(buf));

		if(content_.length()==length_||length_<0)
		{
			BOOST_AUTO(processor, processor_.lock());
			if (processor&&!content_.empty())
				processor->set_package_hash(SHARED_OBJ_FROM_THIS, content_);
			else
				do_request();
			REQUEST_SESSION_DBG(
				std::cout<<"received cert: "<<content_<<std::endl;
			);

			std::cout<<"req_cert"<<std::endl;
		}
	}

	std::string req_cbms_xml::format_url()
	{
		BOOST_AUTO(processor, processor_.lock());
		if (!processor)
			return "";

#ifdef OLD_PROTOCAL
		std::string mac_id = get_value(qmap(), "auth_key");
#else
		std::string mac_id = get_value(qmap(), "authkey");
#endif
		g_orig_mac_id=mac_id;
		std::string orig_mac_id=mac_id;
		if(!mac_id.empty())
		{
			BOOST_FOREACH(char&c, mac_id)
			{
				c=::tolower(c);
			}
			processor->set_mac_id(mac_id);
		}

		std::string mac_md5=string_to_hex(md5(mac_id));
		std::string u=url().to_string();
		boost::replace_first(u, orig_mac_id, mac_md5);
#ifdef OLD_PROTOCAL
		boost::replace_first(u, "/?", "/cbms/interface.php?op=cbms&");
		u.append("&authkey=");
		u.append(mac_md5.c_str(), mac_md5.length());
#else
		boost::replace_first(u, "/?", "/cbms/interface.php?");
#endif
		return  u;
	}
	std::string req_cas_challenge::format_url()
	{
		BOOST_AUTO(processor, processor_.lock());
		if (!processor)
			return "";

		std::string mac_md5 = string_to_hex(md5(processor->mac_id()));

		boost::format reqFmt("%schallenge_request=&id=%s");
		reqFmt%auth_ca_prefix%mac_md5;
		return reqFmt.str();
	}

	std::string req_cert::format_url()
	{
		channel_req_processor::shared_ptr processor=processor_.lock();
		if (!processor)
			return "";

		std::string chanllenge = string_to_hex(
			md5(processor->mac_id()+processor->cas_challenge())
			);
		std::string mac_id = string_to_hex(md5(processor->mac_id()));
#ifdef OLD_PROTOCAL //xml��ʽpubkey
		boost::format reqFmt("%schallenge_response=%s&pubkey_mod=%s&pubkey_exp=%s&id=%s");
		reqFmt%auth_ca_prefix
			%chanllenge
			%BN_bn2dec(processor->rsa_key()->n)
			%BN_bn2dec(processor->rsa_key()->e)
			%mac_id;
#else //pem��ʽcert_request
		std::string package_hash=get_value(qmap(), PACKAGE_HASH);
		std::string packages=get_value(qmap(), "packages");
		std::string app_key=get_value(qmap(), "appkey");
		boost::format reqFmt("%schallenge_response=%s&id=%s&package_hash=%s&packages=%s&appkey=%s");
		reqFmt%auth_ca_prefix
			%chanllenge
			%mac_id
			%package_hash
			%packages
			%app_key
			;
#endif
		return reqFmt.str();
	}

	request_session_base::request_session_base(
		connection_sptr _client, const uri& u, bool needCert)
		:request_session_buf_base(_client, u)
		, need_cert_(needCert)
	{
	}

	void request_session_base::stop()
	{
		if(server_)
		{
			server_->close();
			server_.reset();
		}
		if(request_timer_)
			request_timer_->cancel();
	}

	std::string request_session_base::format_url()
	{
#ifdef OLD_PROTOCAL
		std::string u=format_cms_prefix(url());
		if(get_value(qmap(), "act").empty())
		{
			if(session_cmd_=="get_category")
				u.append("&act=area");
			else if(session_cmd_=="get_channel_list")
				u.append("&act=items");
			else if(session_cmd_=="get_volumn_list"||session_cmd_=="get_channel_detail")
				u.append("&act=detail");
			else if(session_cmd_=="get_volumn_detail")
				u.append("&act=children");
		}

		if(get_value(qmap(), "op").empty())
		{
			if("get_pvse_url" == session_cmd_)
				u.append("&op=pvse");
			if("get_bt_url"==session_cmd_)
				u.append("&op=bt");
		}

		if(get_value(qmap(), "area").empty())
			u.append("&area=0");
		return u;
#else
		BOOST_AUTO(processor, processor_.lock());
		if(!processor)
			return "";

		std::string u=format_cms_prefix(url());
		std::string orig_package_hash=get_value(qmap(), PACKAGE_HASH);
		boost::replace_first(u, orig_package_hash, processor->package_hash()); //ʹ��ca���ص�package_hash
		return u;
#endif
	}

	safe_buffer request_session_base::get_request_buf()
	{
		//std::string str_url = get_host()+"/"+format_url();
		std::string str_url = format_url();
		BOOST_AUTO(processor, processor_.lock());
		if(!processor)
			return safe_buffer();

		error_code ec;
		uri cms_uri(get_host(), ec);
		int port=cms_uri.port()?cms_uri.port():80;

		//request
		http::request req("GET", uri::escape(str_url));
		req.host(cms_uri.host(), port);

		req.set(http::HTTP_ATOM_User_Agent, pszUserAgent);
		req.set(http::HTTP_ATOM_Accept_Charset, xml_code_typd);
		req.set(http::HTTP_ATOM_If_None_Match, cache_.etag);

		//OLD_PROTOCAL �յ�֤�飬�����֤��
		//NEW_PROTOCAL �յ�package_hash�������pub_key
#ifdef OLD_PROTOCAL
		req.set(HTTP_CERT_SIGN, string_to_hex(processor->package_hash()));//cert
#else
		if(need_cert_&&!processor->public_key().empty())//
			req.set(HTTP_PUBLIC_KEY, string_to_hex(processor->public_key()));//public_key
#endif

		safe_buffer buf;
		safe_buffer_io io(&buf);
		io<<req;
		return buf;
	}

	bool request_session_base::process(channel_req_processor::shared_ptr processor)
	{
		if(!processor)
			return false;

		processor_ = processor;

		if(need_cert_&&!is_cert_ready())
		{
			suspend_session();
			return true;
		}

		if(processor->is_key_ready())
			return do_connect();

		processor->genkey(1024, boost::bind(&this_type::do_connect, this));
		return true;
	}

	bool request_session_base::do_connect()
	{
		if (processor_.expired())
			return false;
		try{
			error_code ec;
			uri url(get_host(), ec); //host��ʽ������׳��쳣
			int port = url.port();
			bool enable_ssl=false;
			if(url.protocol()==HTTPS_PROTOCOL
				||(url.protocol().empty()&&HTTPS_PORT==port)) //���ݴ���Ĳ�������
			{
				enable_ssl=true;
			}
			if(server_)
			{
				server_->close();
				server_.reset();
			}

			server_ = http_connection::create(processor_.lock()->get_io_service(), enable_ssl); 

			server_->open(endpoint(), ec, seconds(60));
			server_->register_connected_handler(boost::bind(
				&this_type::on_connected, this, server_.get(), _1
				));
			port=(0==port)?80:port;
			server_->async_connect(url.host(), port, seconds(60));

			REQUEST_SESSION_DBG(
				std::cout<<"connecting "<<url.host()<<":"<<port<<std::endl;
			);

			start_reconnect_timer();
			return true;
		}
		catch(...){
			return false;
		}
	}

	void request_session_base::on_connected(connection_type* conn, const error_code& ec)
	{
		if(request_timer_)
			request_timer_->cancel();

		if(conn!=server_.get())
			return;

		REQUEST_SESSION_DBG(
			std::cout<<"****************connect "<<get_host()<<" OK"<<std::endl;
		);

		conn->register_response_handler(boost::bind(&this_type::on_header, this, conn, _1));
		conn->register_data_handler(boost::bind(&this_type::on_data, this, conn, _1));
		do_request();
	}

	void request_session_base::do_request()
	{
		safe_buffer buf = get_request_buf();

		server_->async_send(buf); //request

		start_reconnect_timer();

		REQUEST_SESSION_DBG(
			std::cout<<"requset: "<<std::string(buffer_cast<char*>(buf), buf.length())<<std::endl;
		);
	}

	void request_session_base::start_reconnect_timer()
	{
		BOOST_AUTO(processor, processor_.lock());
		if(!processor)
			return;

		if(!request_timer_)
		{
			request_timer_=rough_timer::create(processor->get_io_service());
			request_timer_->set_obj_desc("ppc::request_session_base::request_timer_");
			request_timer_->register_time_handler(boost::bind(&this_type::reconnect, this));
		}
		request_timer_->cancel();
		request_timer_->async_wait(seconds(20));//20sû�յ���������
	}

	void request_session_base::reconnect()
	{
		stop();

		do_connect();
	}

	void request_session_base::on_header(connection_type* conn, 
		const http::response& _response)
	{
		reponse_type_ = _response.status();

		if(!conn||!conn->is_open())
			return;

		if(request_timer_)
			request_timer_->cancel();

		if(!is_respones_status_ok(reponse_type_)
			&&_response.content_length()<=0)
		{
			safe_buffer buf;
			safe_buffer_io io(&buf);
			io<<_response;

			send_out(buf);
			return;
		}

		//update etag
		if(cache_.is_modified)
		{
			length_ = _response.content_length();
			cache_.etag=_response.get(http::HTTP_ATOM_ETag);
			if(length_<=0)
			{
				safe_buffer buf;
				safe_buffer_io io(&buf);
				io<<_response;

				send_out(buf);
			}
		}
		else //no modify, send cache data
		{
			length_=cache_.data.length();
			send_out(cache_.data);
		}
	}

	void request_session_base::on_data(connection_type* conn, 
		const safe_buffer& buf)
	{
		BOOST_ASSERT(cache_.is_modified);
		if(!cache_.is_modified)
		{
			if(conn&&conn->is_open())
				conn->close();
			return;
		}
		process_data(conn, buf);
	}


	bool request_session_base::is_respones_status_ok(http::header::status_type status)
	{
		cache_.is_modified=true;
		if(http::header::HTTP_OK == status ||
			http::header::HTTP_PARTIAL_CONTENT == status)
		{
			return true;
		}
		else if(http::header::HTTP_NOT_MODIFIED == status)
		{
			cache_.is_modified=false;
			return true;
		}

		return false;
	}

	void request_session_base::process_encyped_data(connection_type* conn, 
		const safe_buffer& buf)
	{

		if(content_.length()!=length_)
			content_.append(buffer_cast<char*>(buf), buffer_size(buf));

		if(content_.length()==length_||length_==-1)
		{
			BOOST_AUTO(processor, processor_.lock());
			if (processor)
				send_out(get_decrypt_data(content_, processor->rsa_key()));
		}
	}

	bool request_session_base::is_cert_ready()
	{
		BOOST_AUTO(processor, processor_.lock());
		return processor&&processor->is_cert_ready();
	}

	void request_session_base::suspend_session()
	{
		BOOST_AUTO(processor, processor_.lock());
		if (processor)
			processor->suspend_session(SHARED_OBJ_FROM_THIS);
	}
}