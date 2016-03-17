#ifndef _PPC_REQUEST_SESSION_
#define _PPC_REQUEST_SESSION_
#include <p2engine/p2engine.hpp>
#include "typedef.h"
#include "version.h"
namespace ppc{
	typedef http::http_connection_base			connection_type;
	typedef boost::shared_ptr<connection_type>	connection_sptr;
	typedef const std::string&					string_cref;
	typedef std::map<std::string, std::string>	qmap_type;

	class channel_req_processor;
	const static std::string null_string;

	inline const std::string& get_value(const qmap_type& qmap, const char* key)
	{
		BOOST_AUTO(itr, qmap.find(key));
		if (itr!=qmap.end())
			return itr->second;
		return null_string;
	}

	inline std::string get_cbms_host(const qmap_type& qmap)
	{
		return hex_to_string(get_value(qmap, "cbms_host"));
	}

	inline std::string get_cms_host(const qmap_type& qmap)
	{
		return hex_to_string(get_value(qmap, "cms_host"));
	}

	inline std::string get_ca_host(const qmap_type& qmap)
	{
		return hex_to_string(get_value(qmap, "cas_host"));
	}

	inline std::string get_ams_host(const qmap_type& qmap)
	{
		return hex_to_string(get_value(qmap, "ams_host"));
	}

	class request_session_buf_base: public basic_object
	{
		typedef request_session_buf_base this_type;
		SHARED_ACCESS_DECLARE;
	public:
		static request_session_buf_base::shared_ptr create(connection_sptr _client, 
			const uri& u)
		{
			return request_session_buf_base::shared_ptr(new this_type(_client, u), 
				shared_access_destroy<this_type>());
		}
		virtual bool process(boost::shared_ptr<channel_req_processor> processor){return false;}
		void send_out(const safe_buffer& buf);
		void send_out(const std::string& buf);
		void handle_sentout_and_close(connection_type* conn);
		void erase();
	public:
		inline connection_sptr client(){return client_;}
		inline qmap_type qmap()const{return uri_.query_map();}
		inline uri&  url(){return uri_;}
	protected:
		request_session_buf_base(connection_sptr _client, const uri& u);
		~request_session_buf_base(){}
	public:
		struct cache
		{
			bool		is_modified;
			std::string etag;
			std::string data;
		};
	protected:
		uri					                   uri_;
		connection_sptr                        client_;
		cache				                   cache_;
		std::string                            session_id_;
		std::string                            session_cmd_;
		boost::weak_ptr<channel_req_processor> processor_;
	};

	class request_session_base: public request_session_buf_base
	{
		typedef request_session_base this_type;
		SHARED_ACCESS_DECLARE;
	public:
		static request_session_base::shared_ptr create(connection_sptr Client, 
			const uri& u)
		{
			return request_session_base::shared_ptr(new this_type(Client, u, true), 
				shared_access_destroy<this_type>());
		}
	public:
		virtual bool process(boost::shared_ptr<channel_req_processor> processor);
		inline connection_sptr client(){return client_;}
		inline const qmap_type& qmap()const{return uri_.query_map();}
		inline uri&  url(){return uri_;}

	protected:
		virtual std::string get_host(){return get_cms_host(qmap());}
		virtual std::string format_url();
		virtual safe_buffer get_request_buf();

		virtual void process_data(connection_type* conn, const safe_buffer& buf)
		{process_encyped_data(conn, buf);}

	protected:
		void on_connected(connection_type* conn, const error_code& ec);
		void on_header(connection_type* conn, const http::response& _response);
		virtual void on_data(connection_type* conn, const safe_buffer& buf);
	protected:
		virtual void do_request();
		bool is_cert_ready();
		void suspend_session();
		void start_reconnect_timer();
	protected:
		void process_encyped_data(connection_type* conn, const safe_buffer& buf);
		bool do_connect();
		bool is_respones_status_ok(http::header::status_type status);
		void stop();
		void reconnect();
	protected:
		request_session_base(connection_sptr Client, const uri& u, bool needCert=true);
		virtual ~request_session_base(){}
	protected:
		boost::shared_ptr<rough_timer> request_timer_;
		int64_t				length_;
		std::string			content_;
		connection_sptr		server_;
		bool				need_cert_;
		http::header::status_type reponse_type_;
	};

#define  REQ_SESSION_DECLARE(name, host, needcert)\
	class req_##name \
	: public request_session_base\
	{\
	typedef req_##name this_type; \
	SHARED_ACCESS_DECLARE;\
	public: \
	static request_session_base::shared_ptr create(\
	connection_sptr client, const uri& u)\
	{\
	return boost::shared_ptr<this_type>(\
	new this_type(client, u, needcert), shared_access_destroy<this_type>()\
	);\
	}\
	protected: \
	req_##name(connection_sptr client, const uri& u, bool needCert)\
	:request_session_base(client, u, needCert)\
	{}\
	virtual ~req_##name(){}\
	virtual std::string get_host(){return  get_##host(qmap());}\
	virtual std::string format_url();\
	virtual void process_data(connection_type* conn, const safe_buffer& buf)\
	{process_unencyped_data(conn, buf);	}\
	\
	private:\
	void process_unencyped_data(connection_type* conn, const safe_buffer& buf);\
	}

	REQ_SESSION_DECLARE(cbms_xml, 		cbms_host, false);
	REQ_SESSION_DECLARE(cas_challenge, 	ca_host,   false);
	REQ_SESSION_DECLARE(cert, 			ca_host,   false);

	template<
		typename AbstractSession, 
		typename IdentifierType, 
		typename SessionCreator
	>
	class session_factory 
	{
		typedef std::map<IdentifierType, SessionCreator> IdToProductMap;
	public:
		bool Register(const IdentifierType& id, SessionCreator creator)
		{
			return associations_.insert(std::make_pair(id, creator)).second;
		}

		bool Unregister(const IdentifierType& id)
		{
			return associations_.erase(id) == 1;
		}

		boost::shared_ptr<AbstractSession> create(const IdentifierType& id, 
			connection_sptr client, const uri& u)
		{
			typename IdToProductMap::iterator i = associations_.find(id);
			if (i != associations_.end())
			{
				return (i->second)(client, u);
			}
			return boost::shared_ptr<AbstractSession>();
		}

	private:
		IdToProductMap associations_;
	};
}

#endif //__PPC_REQUEST_SESSION_