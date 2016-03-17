#ifndef channel_req_processor_h__
#define channel_req_processor_h__

#include <p2engine/push_warning_option.hpp>
#include <boost/property_tree/ptree.hpp>   
#include <boost/property_tree/xml_parser.hpp>
#include <p2engine/pop_warning_option.hpp>
#include <p2engine/p2engine.hpp>
#include <p2engine/http/http.hpp>

#include <openssl/rsa.h>

#include "p2s_ppc/server.hpp"
#include "p2s_ppc/request_session_base.h"
#include "p2s_ppc/request_session_ams.h"
namespace ppc{
	using namespace p2engine;
	class request_session_buf_base;
	class parse_ca_xml;
	class req_ams_session_cms_coll;
	class channel_req_processor 
		: public session_processor_base
		, public basic_engine_object
	{
	public:
		enum state_t{init, gen_key, request_challenge, request_cert, cert_ready, 
			ams_login, ams_sucess_login, ams_logout, ams_sucess_logout};
		typedef p2engine::rough_timer			timer;
		typedef session_factory<
			request_session_buf_base, 
			std::string, 
			boost::function<request_session_buf_base::shared_ptr(connection_sptr, const uri&)> 
		> factory_type;

		typedef channel_req_processor			this_type;
		SHARED_ACCESS_DECLARE;

	public:
		static this_type::shared_ptr create(io_service& ios)
		{
			return boost::shared_ptr<this_type>(
				new this_type(ios), shared_access_destroy<this_type>()
				);
		}

	public:
		virtual bool process(const uri& url, const http::request& req, const connection_sptr&);

	public:
		template<typename HandlerType>
		void genkey(uint32_t digits, const HandlerType& handler)
		{
			BOOST_ASSERT(init==state_);

			state_=gen_key;
			gen_ssl_rsa(digits);
			handler();
		}

	public:
		bool is_login(){
			return ams_sucess_login == state_;
		}
		
		std::string account_id(){
			return account_id_;
		}
		
		std::string api_key(){
			return api_key_;
		}

		std::string ams_shared_secret_key(){
			return shared_secret_key_;
		}

		std::string user_name(){
			return username_;
		}

		std::string device_id(){
			return device_id_;
		}

		std::string ams_host(){
			return ams_host_;
		}

		std::string ams_cms_host(){
			return ams_cms_host_;
		}

		std::string ams_ca_data(){
			return ams_ca_data_;
		}

		state_t get_state(){
			return state_;
		}

		std::string ams_channellist_qmap(){
			return ams_chlst_qmap_;
		}

		bool is_cert_ready();
		bool is_request_challenge();
		void set_cas_challenge(request_session_buf_base::shared_ptr sess, const std::string& cas_challenge);
		void set_package_hash(request_session_buf_base::shared_ptr sess, const std::string& cert);
		void suspend_session(request_session_buf_base::shared_ptr sess);
	public:
		inline void set_mac_id(const std::string& mac){mac_id_=mac;}
		inline const std::string& mac_id()const{return mac_id_;}
		inline const std::string& cas_challenge()const{return cas_challenge_;}
		inline const RSA* rsa_key() const{return rsa_key_;}
		inline const std::string& public_key()const{return public_key_;}
		inline const std::string& package_hash()const{return package_hash_;}
		inline bool is_key_ready(){return NULL!=rsa_key_;}
		inline void post_erase(request_session_buf_base::shared_ptr sess)
		{
			get_io_service().post(
				boost::bind(&this_type::erase, SHARED_OBJ_FROM_THIS, sess));
		}
		inline void erase(request_session_buf_base::shared_ptr sess)
		{
			waiting_session_.erase(sess);
			session_.erase(sess);
		}
		void assign_ams_log_info(const std::string& _username, const std::string& _device_id, 
			                     const std::string& _ams_host);
		void ams_login_success(const std::string& _account_id, const std::string& _api_key, 
			                   const std::string& _shared_key);

		void ams_logout_sucess();

		void ams_get_ca_sucess(request_session_buf_base::shared_ptr sess, 
			const std::string& ca_data, const std::string& cms_host);

		void ams_get_ca_sucess(request_session_buf_base::shared_ptr sess, 
			                   const std::string& ca_data);
	protected:
		void register_session();
		void stop();
		void gen_ssl_rsa(int bits = 1024);
		void process_suspend_session();

	protected:
		channel_req_processor(io_service& ios);
		virtual ~channel_req_processor(void);

	private:
		std::set<request_session_buf_base::shared_ptr> waiting_session_;
		std::set<request_session_buf_base::shared_ptr> session_;

		std::string cas_challenge_;
		std::string mac_id_;
		std::string package_hash_;

		state_t		 state_;
		RSA*		 rsa_key_;
		std::string  public_key_;
		factory_type factory_;

		//for lc
		std::string   username_;
		std::string   account_id_;
		std::string   api_key_;
		std::string   shared_secret_key_;
		std::string   device_id_;
		std::string   ams_host_;
		std::string   ams_ca_data_;
		std::string   ams_cms_host_;
		std::string   ams_chlst_qmap_;
		//std::set<req_ams_session_cms_coll::shared_ptr> ams_session_;
	};

};
#endif //channel_req_processor_h_
