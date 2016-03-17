#ifndef __COMMON_AUTH
#define __COMMON_AUTH

#include "p2s_mds/media_server.h"
#include <p2engine/push_warning_option.hpp>
#include <string>
#include <p2engine/pop_warning_option.hpp>

using namespace p2engine;

class auth
	:public basic_engine_object
{
	typedef auth this_type;
	typedef http::http_connection http_connection;
	typedef boost::function<void(const std::string&)>  on_message_handler;
	SHARED_ACCESS_DECLARE;

protected:
	auth(io_service& ios);
	~auth();

public:
	static shared_ptr create(io_service& ios)
	{
		return shared_ptr(new this_type(ios), 
			shared_access_destroy<this_type>()
			);
	}
	void reset_regist_code(const std::string& regist_code)
	{
		regist_code_=regist_code;
	}
	void run(const std::string& regist_code);
	void stop();
	on_message_handler& on_error_signal(){return on_error_signal_;}
	on_message_handler& on_auth_signal(){return on_auth_signal_;}

protected:
	void on_timer();
	void do_auth(const http::response& resp=http::response(), 
		const safe_buffer& buf=safe_buffer(), 
		error_code ec=error_code(), coroutine=coroutine()
		);
private:
	void handle_error(const std::string& errorMsg);
protected:
	boost::shared_ptr<rough_timer> timer_;

	//����֤�������Ľ���
	boost::shared_ptr<http_connection> auth_conn_;
	boost::int64_t auth_content_len_;
	std::string auth_content_;
	std::pair<std::string, std::string> auth_key_pair_;
	int auth_failed_cnt_;
	std::string  challenge_;

	std::string regist_code_;
	on_message_handler on_error_signal_;
	on_message_handler on_auth_signal_;
};

#endif
