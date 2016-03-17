#ifndef p2sppc_server__
#define p2sppc_server__

#include "p2s_ppc/typedef.h"
#include "client/pa_handler.h"

extern std::string g_xor_key;

namespace urlcrack
{
	class crack_adapter;
}

namespace ppc{

void set_operators(std::string& op);

class p2p_processor;

class session_processor_base
{
protected:
	typedef http::http_connection_base   connection_type;
	typedef boost::shared_ptr<connection_type> connection_sptr;

public:
	virtual bool process(const uri& url, const http::request& req, const connection_sptr&)=0;
};

class p2sppc_server
	:public basic_engine_object
	, public session_processor_base
{
	typedef p2sppc_server this_type;
	SHARED_ACCESS_DECLARE;

	typedef boost::asio::ip::tcp tcp;
	typedef http::http_connection_base   connection_type;
	typedef boost::shared_ptr<connection_type> connection_sptr;
	typedef rough_timer timer;
	typedef boost::shared_ptr<timer> timer_sptr;
	friend class p2p_processor;
public:
	static shared_ptr create(io_service& ios, int port, const std::string& pas_host="", 
		const std::string& ext_pas_host="")
	{
		return shared_ptr(new this_type(ios, port, pas_host, ext_pas_host), 
			shared_access_destroy<this_type>());
	}
	void run();

	void set_cache_param(const std::string& dir, size_t file_size);
	const std::pair<std::string, std::string>& key_pair()const{return key_pair_;}

	static void close_connection(connection_sptr sock, int replyCode=0, 
		const std::string&content="");
	static void close_connection(connection_sptr sock, const safe_buffer& buf);

	//boost::shared_ptr<p2client::pa_handler> pa_handler()const{return pa_handler_;}
	//const std::vector<boost::shared_ptr<p2client::pa_handler> >& pa_handler_list() const 
	//{ return pa_handler_list_; }

	void report_viewing_info(opt_type opt, 	const std::string& type = "", 
		const std::string& link="", const std::string& channel_name="", 
		const std::string& op=""); 


protected:
	explicit p2sppc_server(io_service& ios, int port, const std::string& pas_host,
		const std::string& ext_pas_host_list_string);
	virtual ~p2sppc_server();

protected:
	void on_accept(connection_sptr conn);
	void on_request(const http::request& req, connection_type* conn);

protected:
	void start_cache_server();
	void start_acceptor();
	void start_pana();
	void start_detectors();
	void prepare_processors();

	virtual bool process(const uri& url, const http::request& req, 
		const connection_sptr&);
	static void handle_sentout_and_close(connection_sptr conn);

private:
	boost::shared_ptr<http_acceptor> acceptor_;
	int port_;
	timed_keeper_set<connection_sptr> conn_keeper_;
	std::pair<std::string, std::string> key_pair_;
	std::vector<boost::shared_ptr<session_processor_base> >  processors_;
	
	std::string cache_dir_;
	int64_t cache_file_size_;

#ifndef POOR_CPU
	std::vector< boost::shared_ptr<p2client::pa_handler> > pa_handler_list_;
#endif
	std::string pas_host_;

	std::vector<std::string> ext_pas_host_list_;
};

}

#endif // p2sppc_server__
