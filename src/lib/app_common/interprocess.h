#ifndef app_common_interprocess_h__ 
#define app_common_interprocess_h__ 

#include <boost/unordered_map.hpp>

#include <p2engine/p2engine.hpp>
#include <p2engine/http/http.hpp>

#include "common/common.h"
#include "common/typedef.h"
#include "common/utility.h"

namespace p2control{

	//∆µµ¿√¸¡Ó
	static const char* CMD_ADD_CHANNEL = "add_channel";
	static const char* CMD_DEL_CHANNEL = "del_channel";
	static const char* CMD_STOP_CHANNEL = "stop_channel";
	static const char* CMD_START_CHANNEL = "start_channel";
	static const char* CMD_RESET_CHANNEL = "reset_channel";
	static const char* CMD_CHECK_CHANNEL = "check_channel";

	//≈‰÷√√¸¡Ó
	static const char* CMD_ADD_TRACKER = "tracker_add";
	static const char* CMD_DEL_TRACKER = "tracker_del";
	static const char* CMD_CHANGE_TRACKER = "tracker_change";
	static const char* CMD_RESET_REGCODE = "reset_regist_code";
	static const char* CMD_EXIT = "mds_exit";
	static const char* CMD_INVALID = "invalid";

	static const std::string INTERNAL_SESSION_ID = "INTERNAL_SESSION_ID";
	static const std::string INTERNAL_CHANNEL_ID = "INTERNAL_CHANNEL_ID";

	//////////////////////////////////////////////////////////////////////////
	using namespace p2engine;
	using namespace p2common;
	class control_base;

	class interprocess_server
		: public basic_engine_object
		, public boost::noncopyable
	{
		typedef interprocess_server this_type;
		SHARED_ACCESS_DECLARE;

	public:
		class __id {
		public:
			std::string          id; //mds_id ªÓshunt_id
			int					 type;
			timestamp_t			 send_time;

			bool operator==(const __id &rhs) const
			{
				return (id == rhs.id) && (type == rhs.type);
			}
			bool operator< (const __id &rhs) const
			{
				if (type != rhs.type)
					return type < rhs.type;
				return (id < rhs.id);
			}
		};
		friend  size_t hash_value(const __id &id)
		{
			if (id.type < 0)
				return boost::hash<std::string>()(id.id);
			else
				return boost::hash<std::string>()(id.id + boost::lexical_cast<std::string>(id.type));
		}

		typedef message_socket_sptr							__conn;
		typedef boost::unordered_map<__id, __conn>			_bimap;
		typedef _bimap::value_type							value_t;

	public:
		static this_type::shared_ptr create(io_service& ios)
		{
			return this_type::shared_ptr(new this_type(ios),
				shared_access_destroy<this_type>());
		}
	public:
		void start(int& port, boost::shared_ptr<control_base> Ctrl);
		void stop();
		void post_cmd(int type, const std::string&, const safe_buffer& buf);
		void post_cmd(const std::string&id, const safe_buffer& buf){ post_cmd(-1, id, buf); }

	protected:
		void __start(int& port);

	protected:
		void on_accept(message_socket_sptr conn, const error_code& ec);
		void on_disconnected(message_socket* conn, const error_code& ec);

		void on_recvd_client_login(message_socket* conn, const safe_buffer& buf);
		void on_recvd_cmd_reply(message_socket* conn, const safe_buffer& buf);
		void on_recvd_alarm(message_socket* conn, const safe_buffer& buf);
		void on_recvd_auth_message(message_socket* conn, const safe_buffer& buf);
		void on_post_waiting_msg();

	protected:
		void __on_client_login(message_socket_sptr conn, const safe_buffer& buf);
		void __on_recvd_cmd_reply(message_socket* conn, const safe_buffer& buf);
		void __post_cmd(const __id&, const safe_buffer& buf);
		void __post_cmd(message_socket_sptr conn, const safe_buffer& buf);

	protected:
		interprocess_server(io_service& ios);
		virtual ~interprocess_server();

	private:
		void register_message_handler(message_socket* conn);
		void start_timer(int port);
		void start_resend_timer();

	private:
		boost::weak_ptr<control_base> ctrl_;
		trdp_acceptor::shared_ptr			  trdp_acceptor_;
		boost::shared_ptr<rough_timer>		  timer_;
		boost::shared_ptr<rough_timer>		  resend_timer_;
		timed_keeper_set<message_socket_sptr> pending_sockets_;

		_bimap chnl_sock_map_;
		boost::unordered_map<__id, safe_buffer> unconfirmed_msg_;
	};

	//////////////////////////////////////////////////////////////////////////
	class interprocess_client
		: public basic_engine_object
		, private boost::noncopyable
	{
		typedef interprocess_client		this_type;
		typedef boost::function<void(message_socket*, const safe_buffer&)> on_recvd_cmd_handler;
		SHARED_ACCESS_DECLARE;

	public:
		static interprocess_client::shared_ptr create(io_service& ios,
			const std::string& id,
			const endpoint& remote_edp,
			int type = -1)
		{
			return interprocess_client::shared_ptr(new this_type(ios, id, remote_edp, type),
				shared_access_destroy<this_type>());
		}
	public:
		void start(){ connect(); }
		void stop();
		void send(const safe_buffer& buf, message_type msg_type);
		void reply_cmd(const std::string& cmd, int Code, const std::string& errorMsg);
		on_recvd_cmd_handler& on_recvd_cmd_signal(){ return on_recvd_cmd_sig_; }
		const std::string& id()const{ return id_; }

	protected:
		void connect();
		void report_id();

	protected:
		void on_connected(message_socket* conn, const error_code& ec);
		void on_disconnected(const error_code& ec);
		void on_recvd_cmd(message_socket* conn, safe_buffer buf);

	protected:
		interprocess_client(io_service& ios,
			const std::string& id, const
			endpoint& remote_edp,
			int type);
		virtual ~interprocess_client();

	private:
		void start_cmd_handler();
		void start_connect_timer();
		void register_message_handler(message_socket* conn);

	private:
		std::string                    id_;
		int							   type_;
		boost::shared_ptr<rough_timer> connect_timer_;
		boost::shared_ptr<rough_timer> timer_;
		trdp_connection::shared_ptr    socket_;
		endpoint	                   remote_edp_;
		on_recvd_cmd_handler	   on_recvd_cmd_sig_;
	};

};//namespace p2control

#endif //app_common_interprocess_h__ 

