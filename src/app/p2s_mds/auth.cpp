#include "common/common.h"
#include "p2s_mds/auth.h"
#include "p2s_mds/version.h"
#include "app_common/mds.pb.h"
#include <p2engine/push_warning_option.hpp>
#include <sstream>
#include <strstream>
#include <boost/algorithm/string/find.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <p2engine/pop_warning_option.hpp>

#ifdef P2ENGINE_DEBUG
#	define DEBUG_AUTH
#define AUTH_DEBUG(x)  /*x*/
#else
#define AUTH_DEBUG(x) 
#endif

//////////////////////////////////////////////////////////////////////////
///auth
auth::auth(io_service& ios)
	:basic_engine_object(ios)
	, auth_failed_cnt_(0)
{
	//regist_code_= "fae3a46252cac83907522f9c21850000a5b61e087fe281a8e26485c1d570cb4e";
}
auth::~auth()
{
	timer_->cancel();
	timer_.reset();
}
void auth::run(const std::string& regist_code)
{
	if (timer_)
		return;

	regist_code_= regist_code;

	timer_=rough_timer::create(get_io_service());
	timer_->set_obj_desc("p2s_mds::auth::timer_");
	timer_->register_time_handler(boost::bind(&this_type::on_timer, this));
#ifdef DEBUG_AUTH
	timer_->async_keep_waiting(millisec(0), seconds(5));
#else
	timer_->async_keep_waiting(seconds(random(0, 24*3600)), hours(24));
#endif
}

void auth::stop()
{
	if (auth_conn_)
	{
		auth_conn_->close();
		auth_conn_.reset();
	}
	if (timer_)
	{
		timer_->cancel();
		timer_.reset();
	}
}

void auth::on_timer()
{
	if(++auth_failed_cnt_>=10)
	{
		handle_error("auth: connect too many times!");
	}
	do_auth();
}

void auth::do_auth(const http::response& resp, 
	const safe_buffer& buf, error_code ec, 
	coroutine coro)
{
	using boost::property_tree::ptree;
	safe_buffer sndbuf;
	CORO_REENTER(coro)
	{
		if (regist_code_.empty())
		{
			if(++auth_failed_cnt_>=10)
			{
				handle_error("auth: connect too many times, register_code is empty!");
			}
			return;
		}

		if (auth_conn_)
			auth_conn_->close();
		auth_conn_=http_connection::create(get_io_service());

		CORO_YIELD(
			auth_conn_->register_connected_handler(boost::bind(
			&this_type::do_auth, this, http::response(), safe_buffer(), _1, coro));
		auth_conn_->async_connect(AUTH_SERVER_HOST, 80);
		);

		if (ec)
			goto CONNECT_ERROR;

		//1.����challenge
		{
			http::request req(http::HTTP_METHORD_GET, 
				std::string("/p2s/auth?")
				+"&id="+string_to_hex(md5(regist_code_))
				+"&challenge_request="
				+"&version="P2S_MDS_VERSION
				);
			req.keep_alive(false);
			req.content_length(0);
			req.host(AUTH_SERVER_HOST, 80);

			safe_buffer_io sbio(&sndbuf);
			sbio<<req;
			AUTH_DEBUG(
				std::cout<<req<<std::endl;
			);
		}
		++auth_failed_cnt_;//��fail��ǰ++���������ղ���response
		CORO_YIELD(
			auth_conn_->register_response_handler(boost::bind(
			&this_type::do_auth, this, _1, safe_buffer(), error_code(), coro));
		auth_conn_->async_send(sndbuf);
		);
		--auth_failed_cnt_;//�յ�response�ˣ�--��ǰ++�Ĳ���

		//DEBUG_SCOPE(
		//	std::cout<<"--------response---header------\n"<<resp<<std::endl;
		//	);

		if(resp.status()!=http::header::HTTP_OK)
		{
			goto HTTP_NOT_OK;
		}
		auth_content_len_=resp.content_length();
		auth_content_.clear();
		++auth_failed_cnt_;//��fail��ǰ++���������ղ���responsedata
		do
		{
			CORO_YIELD(
				auth_conn_->register_data_handler(
				boost::bind(&this_type::do_auth, this, http::response(), _1, error_code(), coro)
				);
			);
			auth_content_.append(buffer_cast<char*>(buf), buffer_size(buf));
		}while (auth_content_.length()<auth_content_len_);
		if (auth_content_len_>0&&auth_content_.length()<auth_content_len_)
			return;
		--auth_failed_cnt_;//�յ�data�ˣ�--��ǰ++�Ĳ���
		if (auth_conn_)
			auth_conn_->close();
		//DEBUG_SCOPE(
		//	std::cout<<"----the---content---\n"<<auth_content_<<std::endl;
		//);
		try
		{
			std::strstream st;
			st.write(auth_content_.c_str(), auth_content_.length());
			ptree pt;
			boost::property_tree::read_xml(st, pt);
			boost::optional<std::string> challengeStr 
				= pt.get_optional<std::string>("auth.p2s.challenge");
			if (!challengeStr)
			{
				auth_conn_->close();
				if(++auth_failed_cnt_>=10)
				{
					handle_error("auth: connect too many times!");
				}
				return;
			}
			challenge_=*challengeStr;
		}
		catch (...)
		{
			if(++auth_failed_cnt_>=10)
			{
				handle_error("auth: connect too many times!");
			}
			return;
		}

		//2. �Ѿ��ɹ��Ļ�ȡchallenge��������Ȩ
		if (auth_conn_)
			auth_conn_->close();
		auth_conn_=http_connection::create(get_io_service());
		CORO_YIELD(auth_conn_->register_connected_handler(
			boost::bind(&this_type::do_auth, this, http::response(), safe_buffer(), _1, coro));
		auth_conn_->async_connect(AUTH_SERVER_HOST, 80);
		);

		if (ec)
			goto CONNECT_ERROR;

		{
			auth_key_pair_=security_policy::generate_key_pair();
			http::request req(http::HTTP_METHORD_GET, 
				std::string("/p2s/auth?")
				+"pubkey="+string_to_hex(auth_key_pair_.first)
				+"&id="+string_to_hex(md5(regist_code_))
				+"&challenge="+challenge_
				+"&challenge_response="+string_to_hex(md5(regist_code_+challenge_))
				+"&version="P2S_MDS_VERSION
				);
			req.host(AUTH_SERVER_HOST, 80);
			req.keep_alive(false);
			req.content_length(0);

			safe_buffer_io sbio(&sndbuf);
			sbio<<req;
		}
		++auth_failed_cnt_;//��fail��ǰ++���������ղ���response
		CORO_YIELD(
			auth_conn_->register_response_handler(
			boost::bind(&this_type::do_auth, this, _1, safe_buffer(), error_code(), coro));
		auth_conn_->async_send(sndbuf);
		);
		--auth_failed_cnt_;//�յ�response�ˣ�--��ǰ++�Ĳ���
		if(resp.status()!=http::header::HTTP_OK)
		{
			goto HTTP_NOT_OK;
		}
		auth_content_len_=resp.content_length();
		auth_content_.clear();
		++auth_failed_cnt_;//��fail��ǰ++���������ղ���responsedata
		do
		{
			CORO_YIELD(
				auth_conn_->register_data_handler(
				boost::bind(&this_type::do_auth, this, http::response(), _1, error_code(), coro));
			);
			auth_content_.append(buffer_cast<char*>(buf), buffer_size(buf));
		}while (auth_content_.length()<auth_content_len_);
		if (auth_content_len_>0&&auth_content_.length()<auth_content_len_)
			return;
		--auth_failed_cnt_;//�յ�data�ˣ�--��ǰ++�Ĳ���
		if (auth_conn_)
			auth_conn_->close();
		/*DEBUG_SCOPE(
			std::cout<<"----the---content---\n"<<auth_content_<<std::endl;
		);*/
		try
		{
			on_auth_signal_(auth_content_);

			std::strstream st;
			st.write(auth_content_.c_str(), auth_content_.length());

			ptree pt;
			boost::property_tree::read_xml(st, pt);

			boost::optional<std::string> startStr = pt.get_optional<std::string>("auth.p2s.start");
			boost::optional<std::string> expireStr = pt.get_optional<std::string>("auth.p2s.expire");
			boost::optional<std::string> signatureStr = pt.get_optional<std::string>("auth.p2s.signature");

			if (!startStr||!expireStr||!signatureStr)
			{
				auth_conn_->close();
				if(++auth_failed_cnt_>=10)
				{
					throw(std::exception("auth: connect too many times!"));
					handle_error("auth: connect too many times!");
				}
				return;
			}

			std::string shareKey=security_policy::generate_shared_key(auth_key_pair_.second, hex_to_string(regist_code_));
			if (string_to_hex(md5(*startStr+*expireStr+shareKey))!=*signatureStr
				||::time(NULL)>boost::lexical_cast<time_t>(*expireStr)
				)
			{
				throw(std::exception("auth: signature mismatched !"));
				handle_error("auth: signature mismatched !");
				return;
			}
			//ok
			auth_failed_cnt_=0;
			AUTH_DEBUG(
				std::cout<<"auth_failed_cnt_="<<auth_failed_cnt_<<std::endl;
			);
		}
		catch (...)
		{
			if(++auth_failed_cnt_>=10)
			{
				throw(std::exception("auth: connect too many times!"));
				handle_error("auth: connect too many times!");
			}
		}

		return;
HTTP_NOT_OK:
		if (++auth_failed_cnt_>=10)
		{	
			throw(std::exception("auth: http connection not ok!"));
			handle_error("auth: http connection failed!");
		}
		if (resp.status()==http::header::HTTP_FORBIDDEN)
		{
			auth_content_len_=resp.content_length();
			auth_content_.clear();
			do
			{
				CORO_YIELD(
					auth_conn_->register_data_handler(boost::bind(
					&this_type::do_auth, this, http::response(), _1, error_code(), coro));
				);
				auth_content_.append(buffer_cast<char*>(buf), buffer_size(buf));
			}while (auth_content_.length()<auth_content_len_);
			if (auth_content_len_>0&&auth_content_.length()<auth_content_len_)
				return;
			DEBUG_SCOPE(
				std::cout<<"----the---content---\n"<<auth_content_<<std::endl;
			);
			on_auth_signal_(auth_content_);

			if (boost::find_first(auth_content_, "P2S")
				||boost::find_first(auth_content_, "p2s")
				)
			{
				handle_error("p2s auth failed, register_code="+regist_code_);
			}
		}
		if (auth_conn_)
			auth_conn_->close();
		return;
CONNECT_ERROR:
		if (auth_conn_)
			auth_conn_->close();
		if(++auth_failed_cnt_>=10)
		{
			handle_error("auth: connect error!");
			throw(std::exception("auth: connect errro!"));
		}
		return;
	}

}

void auth::handle_error(const std::string& errorMsg)
{
	AUTH_DEBUG(
		std::cout<<errorMsg<<std::endl;
	);
	on_error_signal_(errorMsg);
	system_time::sleep(hours(random(1, 24)));
	exit(1);
}