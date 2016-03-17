#ifndef pa_handler_h__
#define pa_handler_h__

#include <p2engine/p2engine.hpp>
#include "common/typedef.h"
#include "common/pa_message.pb.h"
#include "common/message_type.h"


NAMESPACE_BEGIN(p2client)

using namespace p2engine;
using namespace p2common;
//using namespace natpunch;
using namespace proanalytics;

using proanalytics::sample_type;

struct viewing_state
{
	bool				is_watching;
	std::string			type;
	std::string			last_channel_link;
	std::string			last_channel_name;
	std::string			cur_channel_link;
	std::string			cur_channel_name;
	int					cur_channel_duration;
	boost::mutex		mutex;

	viewing_state():is_watching(false), cur_channel_duration(0)
	{}
};

struct view_sample
{
	std::string		type;
	std::string		channel_link;
	std::string		channel_name;
	long			timestamp;

	view_sample():timestamp(0)
	{}
};

enum opt_type
{
	OPT_UNKNOW = 0, 
	OPT_CHANNEL_START = 1, 
	OPT_CHANNEL_STOP = 2
};

class pa_handler
	:public basic_engine_object
	, public boost::noncopyable
{
	typedef pa_handler this_type;
	SHARED_ACCESS_DECLARE;

	typedef basic_connection					connection_type;
	typedef boost::shared_ptr<connection_type>	connection_sptr;

public:
	typedef variant_endpoint endpoint;
	typedef p2engine::rough_timer timer;

	enum state_t{state_init, state_logining, state_logined};

	enum connect_state
	{
		s_unknow, 
		s_unconnected, 
		s_connecting, 
		s_connected
	};
	enum client_state
	{
		cs_unkonw, 
		cs_unlogin, 
		cs_logined, 		// 已经登录获取init消息，开启了schedule
		cs_wait_send, 	// 有数据正在等待连接后发送
		cs_wait_send_and_close, // 发送后关闭连接
	};
	enum pac_mode
	{
		pm_unknow, 
		pm_keep_connection, 		// 登录后一直维持连接
		pm_reconnect_when_send, 	// 登录后断开连接，之后只在需要发送数据时再连接
	};

public:

	static this_type::shared_ptr create(io_service& ios, std::string pas_host="")
	{
		return boost::shared_ptr<this_type>(
			new this_type(ios, pas_host), shared_access_destroy<this_type>());
	}

	void start();
	void stop();
	void report_viewing_info( opt_type opt, 	const std::string& type = "", 
								const std::string& link="", 
								const std::string& channel_name="", 
								const std::string& op="");
	void async_send_reliable(const safe_buffer& buf, message_type msgType);
	void async_send_unreliable(const safe_buffer& buf, message_type msgType);

	void set_operators(const std::string& op);
	void set_mac(const std::string& mac);

protected:
	explicit pa_handler(io_service& io, const std::string& pas_host);
	~pa_handler();

protected:
	void __stop();
	void __connect_pas();
	void __do_connect_pas(const endpoint& localEdp, message_socket*sock=NULL, 
		error_code ec=error_code(), coroutine coro=coroutine());

protected:
	void start_schedule();
	void stop_schedule();
	void schedule();
	void submit_sample(client_state cs);
	void do_submit_sample();
	long get_sample_msg(proanalytics::sample_msg& sample_msg);

protected:
	void register_message_handler(message_socket* conn);
	void start_relogin_timer();

protected:
	void on_connected(message_socket* conn, error_code ec);
	void on_disconnected(message_socket*conn, const error_code& ec);
	void on_receive_init_msg(const safe_buffer& buf, message_socket* conn);
	void on_receive_control_msg(const safe_buffer& buf, message_socket* conn);
	void on_sentout_and_close(message_socket* conn);

private:
	boost::shared_ptr<urdp_connection> connection_;

	connect_state connect_state_;
	client_state client_state_;
	pac_mode pac_mode_;

	std::string domain_;
	boost::shared_ptr<timer> relogin_timer_;
	int relogin_times_;
	boost::optional<endpoint> local_edp_;
	std::map<message_socket*, message_socket_sptr> socket_map_;
	message_socket_sptr socket_;
	boost::shared_ptr<rough_timer> schedule_timer_;
	std::queue<view_sample>	send_queue_;
	viewing_state viewing_state_;
	boost::optional<unsigned int> sampling_interval_;
	boost::optional<long> last_submit_time_;
	boost::optional<unsigned int> per_submit_block_size_;
	std::string	pas_host_;
	unsigned int schedule_delay_;
	bool connect_pas_using_rudp_;
	bool connect_pas_using_rtcp_;
	std::string operators_;
	std::string mac_;
};

NAMESPACE_END(p2client)

#endif



