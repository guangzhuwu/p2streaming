#include "p2s_ppc/version.h"
#include "p2s_ppc/channel_req_processor.h"
#include "p2s_ppc/rsa_rc4.h"
#include "p2s_ppc/request_session_ams.h"
#include "common/utility.h"

#include <p2engine/push_warning_option.hpp>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/scope_exit.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <openssl/bio.h>
#include <openssl/pem.h>
#include <p2engine/pop_warning_option.hpp>

#if !defined(_DEBUG_SCOPE) && defined(NDEBUG)
#	define  CHANNEL_REQUEST_DBG(x)
#else 
#	define  CHANNEL_REQUEST_DBG(x) x
#endif

NAMESPACE_BEGIN(ppc);

channel_req_processor::channel_req_processor(io_service& ios)
: basic_engine_object(ios)
, mac_id_("")
, state_(init)
, rsa_key_(NULL)
, cas_challenge_("")
, package_hash_("")
{
	register_session();
}
void channel_req_processor::register_session()
{
#define REGISTER_SESSION(key, req) \
	factory_.Register(key, boost::bind(req::create, _1, _2));

	REGISTER_SESSION("get_cbms", 			    req_cbms_xml);
	REGISTER_SESSION("get_cas_challenge", 	    req_cas_challenge);
	REGISTER_SESSION("get_cert", 			    req_cert);

	REGISTER_SESSION("ams_login",               req_ams_login);
	REGISTER_SESSION("get_ams_packages",        req_ams_packages);
	REGISTER_SESSION("ams_logout",              req_ams_logout);
	REGISTER_SESSION("get_ams_package_info",    req_ams_get_package_info);
	REGISTER_SESSION("ams_purchase_package",    req_ams_purchase_package);
	REGISTER_SESSION("ams_transactions",        req_ams_transactions);
//	REGISTER_SESSION("ams_billing_info",        req_ams_billing_info);
//	REGISTER_SESSION("ams_registration",        req_ams_registration);
	REGISTER_SESSION("get_ams_channel_list",    req_ams_new_ca);
	REGISTER_SESSION("get_ams_channel_detail", 	req_ams_get_ca);
	REGISTER_SESSION("get_ams_volumn_list", 		req_ams_get_ca);
	REGISTER_SESSION("get_ams_volumn_detail", 	req_ams_get_ca);
	REGISTER_SESSION("get_ams_category", 		req_ams_get_ca);
	REGISTER_SESSION("ams_cms_request",         req_ams_session_cms);

	REGISTER_SESSION("get_category", 		    request_session_base);
	REGISTER_SESSION("get_channel_list", 	    request_session_base);
	REGISTER_SESSION("get_channel_detail", 	    request_session_base);
	REGISTER_SESSION("get_volumn_list", 		    request_session_base);
	REGISTER_SESSION("get_volumn_detail", 	    request_session_base);
	REGISTER_SESSION("get_bt_url", 			    request_session_base);
	REGISTER_SESSION("get_pvse_url", 		    request_session_base);
#undef  REGISTER_SESSION
}


channel_req_processor::~channel_req_processor(void)
{
	stop();

	if(rsa_key_)
	{
		RSA_free(rsa_key_);
		rsa_key_ = NULL;
	}
}

void channel_req_processor::stop()
{
	session_.clear();
	waiting_session_.clear();
}

void channel_req_processor::gen_ssl_rsa(int bits)
{
	if(rsa_key_)
		return;

	BIGNUM* bne = BN_new();
	unsigned long e = RSA_F4;
	int ret = BN_set_word(bne, e);

	if(rsa_key_)
	{
		RSA_free(rsa_key_);
		rsa_key_ = NULL;
	}

	rsa_key_ = RSA_new();
	ret = RSA_generate_key_ex(rsa_key_, bits, bne, NULL);

	if(1==ret)
	{
		BIO* b=BIO_new(BIO_s_mem());
		ret=PEM_write_bio_RSA_PUBKEY(b, rsa_key_);//PEM_write_bio_RSAPublicKey(b, rsa_key_);

		int len=BIO_ctrl_pending(b);
		char* out=(char *)OPENSSL_malloc(len);
		len=BIO_read(b, out, len);
		public_key_.assign(out, len);
		DEBUG_SCOPE(
			if(len)
			{
				std::cout<<std::string(out, len)<<std::endl;
			}
			);
			OPENSSL_free(out);
			BIO_free(b);
	}
	CHANNEL_REQUEST_DBG(
		if(ret != 1)
			std::cout<<("RSA_generate_key_ex err!\n");
	);
}

void channel_req_processor::process_suspend_session()
{
	BOOST_FOREACH(request_session_buf_base::shared_ptr wait_sess, waiting_session_)
	{
		wait_sess->process(SHARED_OBJ_FROM_THIS);
	}
	waiting_session_.clear();
}
void channel_req_processor::assign_ams_log_info(const std::string& _username, 
	const std::string& _device_id, const std::string& _ams_host)
{
	username_ = _username;
	device_id_ = _device_id;
	ams_host_ = _ams_host;
}

void channel_req_processor::ams_login_success(const std::string& _account_id, 
	const std::string& _api_key, const std::string& _shared_key)
{
	state_ = ams_sucess_login;
	account_id_ = _account_id;
	api_key_ = _api_key;
	shared_secret_key_ = _shared_key;

	process_suspend_session();
}

void  channel_req_processor::ams_logout_sucess()
{
	state_ = ams_sucess_logout;
	username_.clear();
	account_id_.clear();
	api_key_.clear();
	shared_secret_key_.clear();
}

void channel_req_processor::ams_get_ca_sucess(request_session_buf_base::shared_ptr sess, 
	const std::string& ca_data, const std::string& cms_host)
{
	ams_ca_data_ = ca_data;
	ams_cms_host_ = cms_host;

	request_session_buf_base::shared_ptr session=
		factory_.create("ams_cms_request", sess->client(), sess->url());	

	post_erase(sess);

	session_.insert(session);
	session->process(SHARED_OBJ_FROM_THIS);
}

void channel_req_processor::ams_get_ca_sucess(request_session_buf_base::shared_ptr sess, 
	const std::string& ca_data)
{
	request_session_buf_base::shared_ptr session =
		req_ams_session_cms_coll::create(sess->client(), ca_data, sess->url());

	session_.insert(session);
	session->process(SHARED_OBJ_FROM_THIS);
	post_erase(sess);
}

bool channel_req_processor::process(const uri& url, const http::request& req, 
									const connection_sptr& conn)
{
	error_code ec;
	uri u(req.url(), ec);
	if(!ec)
	{
		std::string str_cmd = get_value(u.query_map(), "cmd");
		request_session_buf_base::shared_ptr session_ptr 
			= factory_.create(str_cmd, conn, u);

		if(0==strcmp("get_cbms", str_cmd.c_str()))//if is request from cbms, reset state
			state_=init;

		if(!session_ptr)
			return false;

		session_.insert(session_ptr);

		//AMS�����������Ҫ����MAP����CMS������Ҫ
		ams_chlst_qmap_ = u.to_string(uri::query_component);

		return session_ptr->process(SHARED_OBJ_FROM_THIS);
	}
	return false;
}

bool channel_req_processor::is_cert_ready()
{
	return cert_ready==state_;
}

bool channel_req_processor::is_request_challenge()
{
	return request_challenge == state_;
}

void channel_req_processor::suspend_session(request_session_buf_base::shared_ptr sess)
{
	waiting_session_.insert(sess);
	if(init==state_||gen_key==state_)//������ȡ֤��session
	{
		request_session_buf_base::shared_ptr session=
			factory_.create("get_cas_challenge", sess->client(), sess->url());		

		state_=request_challenge;
		session_.insert(session);
		session->process(SHARED_OBJ_FROM_THIS);
	}

	BOOST_ASSERT(cert_ready!=state_);
}

void channel_req_processor::set_cas_challenge(request_session_buf_base::shared_ptr sess, 
											  const std::string& cas_challenge)
{
	cas_challenge_=cas_challenge;
	if(request_challenge==state_)
	{
		request_session_buf_base::shared_ptr session=
			factory_.create("get_cert", sess->client(), sess->url());	

		post_erase(sess);

		state_=request_cert;
		session_.insert(session);
		session->process(SHARED_OBJ_FROM_THIS);
	}
	BOOST_ASSERT(cert_ready!=state_);
}
void channel_req_processor::set_package_hash(request_session_buf_base::shared_ptr sess, 
	const std::string& cert)
{
	//note �����յ���������package_hash
	post_erase(sess);

	package_hash_=cert;
	state_=cert_ready;

	process_suspend_session();
}

NAMESPACE_END(ppc);
