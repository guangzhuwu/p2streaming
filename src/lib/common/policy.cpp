#include "common/policy.h"
#include "common/utility.h"
#include "common/md5.h"
#include "common/security_policy.h"

/************************************************************************/
/* ts2p_challenge_checker                                               */
/************************************************************************/

using namespace p2common;

ts2p_challenge_checker::ts2p_challenge_checker()
{
	key_pair_ = security_policy::generate_key_pair();
}
void ts2p_challenge_checker::send_challenge_msg(message_socket_sptr conn)
{
	ts2p_challenge_msg msg;
	msg.set_challenge(key_pair_.first.c_str(), key_pair_.first.length());
	conn->async_send_reliable(serialize(msg), tracker_peer_msg::challenge);
}

bool ts2p_challenge_checker::challenge_check(const p2ts_login_msg&  msg, 
						                   const std::string&  private_key)
{
	std::string theStr = private_key + string_to_hex(msg.public_key());
	md5_byte_t digest[16];
	md5_state_t pms;
	md5_init(&pms);
	md5_append(&pms, (const md5_byte_t *)theStr.c_str(), theStr.length());
	md5_finish(&pms, digest);

	const std::string& ce=msg.certificate();
	if((!private_key.empty()&&(ce.length()!=sizeof(digest)||memcmp(digest, ce.c_str(), sizeof(digest))) )
		||msg.shared_key_signature() != shared_key_signature(msg.public_key()) 
		)
	{
		return false;
	}

	return true;
}
void  ts2p_challenge_checker::challenge_failed(message_socket_sptr  conn, 
											   const int            session, 
											   error_code_enum ec /*= e_unknown*/)
{
	ts2p_login_reply_msg msg;
	msg.set_error_code(ec);//具体原因不明
	msg.set_session_id(session);
	//下面的字段必须给...
	msg.set_external_ip(0);
	msg.set_external_port(0);
	msg.set_join_time(0);
	msg.set_online_peer_cnt(0);
	msg.set_channel_id("0");

	//发送消息
	conn->async_send_reliable(serialize(msg), tracker_peer_msg::login_reply);
	conn->close();
}

std::string ts2p_challenge_checker::shared_key_signature(const std::string& pubkey)
{
	std::string theStr = security_policy::generate_shared_key(key_pair_.second, pubkey);

	md5_byte_t digest[16];
	md5_state_t pms;
	md5_init(&pms);
	md5_append(&pms, (const md5_byte_t *)theStr.c_str(), theStr.length());
	md5_finish(&pms, digest);

	return std::string((char*)digest, 16);
}


/************************************************************************/
/*                                                                      */
/************************************************************************/

bool p2ts_challenge_responser::challenge_response(const ts2p_challenge_msg&  msg, client_param_sptr param, message_socket_sptr conn)
{
	std::string challenge = security_policy::client_encrypt_tracker_challenge(msg.challenge(), param);
	//if(!is_cache_category())
	{
		//发送登录请求
		p2ts_login_msg loginMsg;
		loginMsg.set_session_id(0);//暂时未用
		loginMsg.set_channel_id(param->channel_link);	//消息中携带频道ID信息
		*(loginMsg.mutable_peer_info()) = param->local_info;
		loginMsg.set_certificate(param->channel_key);
		loginMsg.set_shared_key_signature(challenge);
		loginMsg.set_public_key(param->public_key);

		conn->async_send_reliable(serialize(loginMsg), tracker_peer_msg::login);
	}
	return true;
}
