#ifndef _REQUEST_SESSION_AMSBASE_
#define _REQUEST_SESSION_AMSBASE_
#include "request_session_base.h"
#include <map>
namespace ppc{
	class request_session_ams: public request_session_base
	{
		typedef request_session_ams this_type;
		SHARED_ACCESS_DECLARE;
	public:
		static request_session_ams::shared_ptr create(connection_sptr _client, 
			const uri& u)
		{
			return request_session_ams::shared_ptr(new this_type(_client, u, true, true), 
				shared_access_destroy<this_type>());
		}
	public:
		virtual bool process(boost::shared_ptr<channel_req_processor> processor);
	protected:
		request_session_ams(connection_sptr _client, const uri& u, bool needCert=true, bool bmethodget=true)
		:request_session_base(_client, u, needCert)
		, b_request_get_(bmethodget)
		{}
		virtual ~request_session_ams(){}
		virtual safe_buffer get_request_buf();
		virtual std::string get_host();
		virtual void process_data(connection_type* conn, const safe_buffer& buf);
	protected:
		bool b_request_get_;
	};

#define  REQ_SESSION_AMS_DECLARE(name, needcert, bget)\
	class req_##name \
	: public request_session_ams\
	{\
	typedef req_##name this_type; \
	SHARED_ACCESS_DECLARE;\
	public: \
	static request_session_base::shared_ptr create(\
	connection_sptr client, const uri& u)\
	{\
	return boost::shared_ptr<this_type>(\
	new this_type(client, u, needcert, bget), shared_access_destroy<this_type>()\
	);\
	}\
	protected: \
	req_##name(connection_sptr client, const uri& u, bool needCert, bool bmethodget)\
	:request_session_ams(client, u, needCert, bmethodget)\
	{}\
	virtual ~req_##name(){}\
	virtual std::string format_url();\
	virtual void process_data(connection_type* conn, const safe_buffer& buf);\
	}

	REQ_SESSION_AMS_DECLARE(ams_login,             false, false);
	REQ_SESSION_AMS_DECLARE(ams_packages,          true,  true);
	REQ_SESSION_AMS_DECLARE(ams_logout,            true,  true);
	REQ_SESSION_AMS_DECLARE(ams_get_ca,            true,  true);
	REQ_SESSION_AMS_DECLARE(ams_new_ca,            true,  true);
	REQ_SESSION_AMS_DECLARE(ams_get_package_info,  true,  true);
	REQ_SESSION_AMS_DECLARE(ams_purchase_package,  true,  false);
	REQ_SESSION_AMS_DECLARE(ams_transactions,      true,  true);
	REQ_SESSION_AMS_DECLARE(ams_billing_info,      true,  false);
	REQ_SESSION_AMS_DECLARE(ams_registration,      false, false);

	class req_ams_session_cms:public request_session_ams
	{
		typedef req_ams_session_cms this_type;
		SHARED_ACCESS_DECLARE;
	public:
		static req_ams_session_cms::shared_ptr create(connection_sptr _client, 
			const uri& u)
		{
			return req_ams_session_cms::shared_ptr(new this_type(_client, u, true, false), 
				shared_access_destroy<this_type>());
		}
	protected:
		req_ams_session_cms(connection_sptr _client, const uri& u, bool needCert=true, bool bmethodget=false)
			:request_session_ams(_client, u, needCert, bmethodget)
		{}
		virtual ~req_ams_session_cms(){}
		virtual std::string format_url();
		virtual std::string get_host();
		virtual void process_data(connection_type* conn, const safe_buffer& buf);
	};
	
	class parse_ca_xml;
	class req_new_ams_session_cms;
	class req_ams_session_cms_coll:public request_session_buf_base
	{
		typedef req_ams_session_cms_coll this_type;
		SHARED_ACCESS_DECLARE;
	public:
		static req_ams_session_cms_coll::shared_ptr create(connection_sptr _client, 
			const std::string& ca_data, uri& _url) 
		{
			return req_ams_session_cms_coll::shared_ptr(new this_type(_client, ca_data, _url), 
				shared_access_destroy<this_type>());
		}
	public:
		bool process(boost::shared_ptr<channel_req_processor> processor);

		void sucess_get_cms_data(boost::shared_ptr<req_new_ams_session_cms> session, 
			                     safe_buffer recdata);

		std::string get_cms_host(boost::shared_ptr<req_new_ams_session_cms> session);

		std::string get_ca_data(boost::shared_ptr<req_new_ams_session_cms> session);

		boost::asio::io_service& get_io_service();
	protected:
		req_ams_session_cms_coll(connection_sptr _client, 
			const std::string& ca_data, uri& _url);
		~req_ams_session_cms_coll(){}
	private:
		typedef std::map<boost::shared_ptr<req_new_ams_session_cms>, int> cmssession_serverindex_map;
		boost::shared_ptr<parse_ca_xml>        ca_xml_parser_;
		cmssession_serverindex_map             cmssession_serverindex_;
		std::map<int, std::string>             cms_results_;
	};

	class req_new_ams_session_cms:public request_session_ams
	{
		typedef req_new_ams_session_cms this_type;
		SHARED_ACCESS_DECLARE;
	public:
		static req_new_ams_session_cms::shared_ptr create(connection_sptr _client, 
			const uri& u)
		{
			return req_new_ams_session_cms::shared_ptr(new this_type(_client, u, true, false), 
				shared_access_destroy<this_type>());
		}		
	protected:
		req_new_ams_session_cms(connection_sptr _client, const uri& u, bool needCert=true, bool bmethodget=false)
			:request_session_ams(_client, u, needCert, bmethodget)
		{}
		virtual ~req_new_ams_session_cms(){}
		virtual std::string format_url();
		virtual std::string get_host();
		virtual void process_data(connection_type* conn, const safe_buffer& buf);
	public:
		virtual bool process(boost::shared_ptr<channel_req_processor> processor, 
			boost::shared_ptr<req_ams_session_cms_coll> coll);

		void process_encyped_data(connection_type* conn, const safe_buffer& buf);
	private:
		boost::weak_ptr<req_ams_session_cms_coll> session_coll_;
	};
}

#endif //_REQUEST_SESSION_AMSBASE_