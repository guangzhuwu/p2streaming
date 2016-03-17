#ifndef p2sppc_h__
#define p2sppc_h__

#include "p2s_ppc/typedef.h"
#include "p2s_ppc/server.hpp"

namespace ppc{

class p2p_processor
	:public client_service_logic_base
	, public session_processor_base
{
	typedef p2p_processor this_type;
	typedef boost::asio::ip::tcp tcp;
	SHARED_ACCESS_DECLARE;
	enum state_code{
		DISSCONNECTED, 
		CONNECTING, 
		CONNECTED
	};
	typedef http::http_connection_base   connection_type;
	typedef boost::shared_ptr<connection_type> connection_sptr;

protected:
	p2p_processor(boost::shared_ptr<p2sppc_server> svr);
	virtual ~p2p_processor();

public:
	static shared_ptr create(boost::shared_ptr<p2sppc_server> svr)
	{
		return shared_ptr(new this_type( svr), 
			shared_access_destroy<this_type>()
			);
	}

	virtual bool process(const uri& url, const http::request& req, 
		const connection_sptr&);

protected:
	void stop_channel(bool flush=false);
	void start_channel(const client_param_base& para);
	void register_stream_cmd_socket(connection_sptr sock, const http::request& req, 
		bool head_only = false);
	void erase(connection_sptr sock);

public:
	virtual void register_message_handler(message_socket*)
	{}

public:
	//��ѹδ�����ý�����ݳ��ȣ�һ���ǲ���������������ᷢ����
	virtual int overstocked_to_player_media_size();

protected:
	//��¼ʧ��
	virtual void on_login_failed(error_code_enum code, const std::string& errorMsg);

	//��¼�ɹ�
	virtual void on_login_success();

	//����
	virtual void on_droped();

	//һ���½ڵ����ϵͳ
	virtual void on_join_new_peer(const peer_id_t& newPeerID, const std::string& userInfo);
	//������һ�����ڱ��ڵ����ߵĽڵ�
	virtual void on_known_online_peer(const peer_id_t& newPeerID, const std::string& userInfo);
	//�ڵ��뿪
	virtual void on_known_offline_peer(const peer_id_t& newPeerID);

	virtual void on_user_info_changed(const peer_id_t& newPeerID, const std::string& oldUserInfo, 
		const std::string& newUserInfo)
	{}
	//�յ�ý���
	virtual void  on_recvd_media(const void*data, size_t len, const peer_id_t& srcPeerID, 
		media_channel_id_t mediaChannelID)
	{}
	virtual void  on_recvd_media(const safe_buffer& buf, const peer_id_t& srcPeerID, 
		media_channel_id_t mediaChannelID);

	virtual void on_media_end(const peer_id_t& srcPeerID, media_channel_id_t mediaChannelID);

	virtual void on_recvd_msg(const std::string&msg, const peer_id_t& srcPeerID)
	{}

protected:
	void __on_multicast_recvd(error_code ec, size_t len);

protected:
	void on_disconnected(error_code ec, connection_type*conn);
	void response_stream_socket(connection_sptr conn, bool RangRequest);
	void response_header_socket(connection_sptr conn);

protected:
	void send_flv_header(connection_sptr conn);

protected:
	std::set<connection_sptr> head_request_sockets_;
	std::set<connection_sptr> switch_request_sockets_;
	std::set<connection_sptr> stream_sockets_;
	std::map<connection_sptr, bool/*Rang Request*/> waiting_stream_socekets_;

	state_code state_;
	std::string current_channel_link_;
	int64_t current_play_offset_;
	boost::optional<ptime> idle_time_;
	std::deque<std::pair<p2engine::safe_buffer, ptime> > packets_;
	
	boost::weak_ptr<p2sppc_server> p2sppc_server_;

	//IP�鲥����
	udp::socket mulicast_socket_;
	udp::endpoint mulicast_endpoint_;
	std::vector<char> mulicast_buf_;
};

}

#endif // p2sppc_h__
