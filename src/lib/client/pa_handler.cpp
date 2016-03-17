#include "client/pa_handler.h"
#include "p2engine/macro.hpp"
#include "common/message_type.h"
#include "common/pa_message.pb.h"
#include "common/utility.h"
#include "natpunch/auto_mapping.h"

#if !defined(_DEBUG_SCOPE) && defined(NDEBUG)
#	define  PA_HANDLER_DBG(x)
#else 
#	define  PA_HANDLER_DBG(x) //x
#endif


PA_HANDLER_DBG(
	std::ofstream g_outfile;		   		   
);

namespace
{
	//const int DEFAULT_CONNECT_TIMEOUT = 30; 
	const std::string DEFAULT_PAS_HOST("analitics.568tv.hk:10000");
	const int DEFAULT_PAS_PORT = 10000;
	const std::string DEFAULT_PAS_DOMAIN("domain");

}
namespace ppc
{
	extern std::string g_orig_mac_id;
}

NAMESPACE_BEGIN(p2client);

void pa_handler::register_message_handler(message_socket* conn)
{
#define REGISTER_HANDLER(msgType, handler)\
	conn->register_message_handler(msgType, boost::bind(&this_type::handler, this, _1, conn));

	REGISTER_HANDLER(p2common::proanalytics_msg::init_client_msg, on_receive_init_msg);
//	REGISTER_HANDLER(p2common::proanalytics_msg::sample_msg, on_receive_control_msg);
#undef REGISTER_HANDLER
}


timestamp_t start_time;
pa_handler::pa_handler(io_service& ios, const std::string& pas_host)
	:basic_engine_object(ios)
	, connection_()
	, connect_state_(s_unconnected)
	, client_state_(cs_unlogin)
	, pac_mode_(pm_keep_connection)
	, domain_()
	, relogin_timer_()
	, relogin_times_(0)
	, local_edp_()
	, socket_map_()
	, socket_()
	, schedule_timer_()
	, send_queue_()
	, viewing_state_()
	, sampling_interval_()
	, last_submit_time_()
	, per_submit_block_size_()
	, pas_host_(boost::iequals(pas_host, "")?DEFAULT_PAS_HOST:pas_host)
	, schedule_delay_(0)
	, connect_pas_using_rudp_(true)
	, connect_pas_using_rtcp_(true)
	, operators_("P2S")
	, mac_("")
{
	PA_HANDLER_DBG(
		g_outfile.open("pa_handler_log.txt");
	BOOST_ASSERT(!g_outfile.bad() && "open log file failed");
	);

	PA_HANDLER_DBG(
		std::cout<<">>>>> pa_handler on create "<<operators_<<" pas_host: "<<pas_host_<<std::endl;
	g_outfile<<">>>>> pa_handler on create "<<operators_<<" pas_host: "<<pas_host_<<std::endl;
	);
	//LOGD("pa_handler on create");
}

pa_handler::~pa_handler(void)
{

}

void pa_handler::set_operators(const std::string& op)
{
	operators_ = op;
}

void pa_handler::set_mac(const std::string& mac)
{
	mac_ = mac;
}

void pa_handler::start()
{
	__stop();

	start_time = timestamp_now();
	relogin_times_=0;
	client_state_ = cs_unlogin;
	connect_state_ = s_unconnected;

	__connect_pas();
}

void pa_handler::__connect_pas()
{
	PA_HANDLER_DBG(;
	std::cout<<"__connect_pas, connect_state_: "<<connect_state_<<std::endl;
	g_outfile<<"__connect_pas, connect_state_: "<<connect_state_<<std::endl;
	);
	__do_connect_pas(endpoint());
}

void pa_handler::__do_connect_pas(const endpoint& localEdp, message_socket*conn, 
								  error_code ec, coroutine coro)
{
	message_socket_sptr newsock;
	CORO_REENTER(coro)
	{
		PA_HANDLER_DBG(;
		//std::cout<<">>>>> pa_handler connect to pas: "<<pas_host_<<std::endl;
		//g_outfile<<">>>>> pa_handler connect to pas: "<<pas_host_<<std::endl;
		);


		if(++relogin_times_ > 1)
		{
			PA_HANDLER_DBG(;
			//std::cout<<">>>>> pa_handler relogin"<<std::endl;
			//g_outfile<<">>>>> pa_handler relogin"<<std::endl;
			);
		}


		if( connect_state_ != s_unconnected)
		{
			connect_state_ = s_connecting;
		}


		if(connect_pas_using_rudp_)
		{
			newsock = urdp_message_socket::create(get_io_service(), true);
			newsock->open(endpoint(localEdp.address(), natpunch::get_udp_mapping().first), ec);
			socket_map_[newsock.get()]=newsock;


			PA_HANDLER_DBG(;
			std::cout<<">>>>> pa_handler connecting pas: "<< pas_host_
				<<" , UDP-localendpoint:"<< newsock->local_endpoint(ec)
				<<std::endl;
			g_outfile<<">>>>> pa_handler connecting pas: "<< pas_host_
				<<" , UDP-localendpoint:"<< newsock->local_endpoint(ec)
				<<std::endl;
			);

			if(!ec)
			{
				CORO_YIELD(
					newsock->register_connected_handler(
					boost::bind(&this_type::__do_connect_pas, this, localEdp, newsock.get(), _1, coro)
					);
				newsock->async_connect(pas_host_, DEFAULT_PAS_DOMAIN, seconds(8));
				);
				if(!ec)
				{
					on_connected(conn, ec);
					return;
				}
				else
				{
					if(connect_pas_using_rtcp_)
					{
						error_code e;
						newsock=trdp_message_socket::create(get_io_service(), true);
						newsock->open(localEdp, e);
						socket_map_[newsock.get()]=newsock;
						newsock->register_connected_handler(
							boost::bind(&this_type::on_connected, this, newsock.get(), _1)
							);
						newsock->async_connect( pas_host_, DEFAULT_PAS_DOMAIN, seconds(8));
						PA_HANDLER_DBG(
							std::cout<<"pa_handler connecting pas: "<< pas_host_
							<<" , TCP-localendpoint:"<<newsock->local_endpoint(e)
							<<std::endl;
						);
					}
					on_connected(conn, ec);
					return;
				}
			}
		}
		else if(connect_pas_using_rtcp_)
		{
			error_code e;
			newsock=trdp_message_socket::create(get_io_service(), true);
			newsock->open(localEdp, e);
			socket_map_[newsock.get()]=newsock;
			newsock->register_connected_handler(
				boost::bind(&this_type::on_connected, this, newsock.get(), _1)
				);
			newsock->async_connect(pas_host_, DEFAULT_PAS_DOMAIN, seconds(8));
			PA_HANDLER_DBG(;
			std::cout<<"pa_handler connecting pas: "<< pas_host_
				<<" , TCP-localendpoint:"<<newsock->local_endpoint(e)
				<<std::endl;
			g_outfile<<"pa_handler connecting pas: "<< pas_host_
				<<" , TCP-localendpoint:"<<newsock->local_endpoint(e)
				<<std::endl;
			);
		}
		else
		{
			get_io_service().post(make_alloc_handler(
				boost::bind(&this_type::on_connected, SHARED_OBJ_FROM_THIS, (message_socket* )NULL, boost::asio::error::not_socket)
				));
		}

	}
}

void pa_handler::stop()
{
	__stop();
}

void pa_handler::__stop()
{
	connect_state_;
	client_state_;

	if (socket_) 
	{
		socket_->close();
		socket_.reset();
	}
	if(relogin_timer_)
	{
		relogin_timer_->cancel();
		relogin_timer_.reset();
	}
	socket_map_.clear();
}

void pa_handler::report_viewing_info( opt_type opt, 	const std::string& type, const std::string& link, 
									 const std::string& channel_name, const std::string& op)
{
	PA_HANDLER_DBG(;
	std::cout<<std::endl<<"pa_handler report_viewing_info"<<std::endl;
	g_outfile<<std::endl<<"pa_handler report_viewing_info"<<std::endl;
	);

	BOOST_ASSERT( opt == p2client::OPT_CHANNEL_START || opt == p2client::OPT_CHANNEL_STOP);
	if(opt == p2client::OPT_CHANNEL_START)
	{
		BOOST_ASSERT( boost::iequals(type, "p2s") || boost::iequals(type, "p2v") || boost::iequals(type, "dvb"));
	}

	PA_HANDLER_DBG(;
	std::cout<<"***************************************   report_viewing_info opt: "<<opt<<std::endl;
	g_outfile<<"***************************************   report_viewing_info opt: "<<opt<<std::endl;
	if(opt == OPT_CHANNEL_START)
	{
		std::cout<<"type: "<<type<<std::endl;
		std::cout<<"link: "<<link<<std::endl;
		if( boost::iequals(type, "dvb"))
		{
			std::cout<<"channel_name: "<<channel_name<<std::endl;
		}
	}
	);

	boost::mutex::scoped_lock lk(viewing_state_.mutex);
	if( opt == OPT_CHANNEL_START)
	{
		viewing_state_.type = type;
		if(viewing_state_.is_watching == false)
		{
			viewing_state_.last_channel_link = viewing_state_.cur_channel_link = link;
			if( boost::iequals(type, "dvb"))
			{
				viewing_state_.last_channel_name = viewing_state_.cur_channel_name = channel_name;
			}
			viewing_state_.is_watching = true;

			if( client_state_ == cs_unlogin)
			{
				start();
			}

			PA_HANDLER_DBG(
				std::cout<<std::endl<<"first time channel start, connect to pas"<<std::endl;
			g_outfile<<std::endl<<"first time channel start, connect to pas"<<std::endl;
			)
		}
		else
		{
			viewing_state_.last_channel_link = viewing_state_.cur_channel_link;
			viewing_state_.cur_channel_link = link;
			if( boost::iequals(type, "dvb"))
			{
				viewing_state_.last_channel_name = viewing_state_.cur_channel_name;
				viewing_state_.cur_channel_name = channel_name;
			}
		}
	}
	else if( opt == OPT_CHANNEL_STOP)
	{
		if( viewing_state_.is_watching == false)
		{
			return ;
		}

		viewing_state_.is_watching = false;
		viewing_state_.type = SAMPLE_UNKNOW;

		PA_HANDLER_DBG(
			std::cout<<std::endl<<"channel stop, close connection"<<std::endl;
		g_outfile<<std::endl<<"channel stop, close connection"<<std::endl;
		);

		// �������ر�ʱ����ʣ���sample����server
		stop_schedule();
		submit_sample(cs_wait_send_and_close);

		if( pac_mode_ == pm_keep_connection)
		{
			//socket_->close();
			//socket_.reset();

			// ��ģʽ�£����ݻ�ֱ�ӷ��ͣ���������������״̬
			client_state_ = cs_unlogin;
		}
	}
}

void pa_handler::async_send_reliable(const safe_buffer& buf, message_type msgType)
{
	//if (socket_)
	//socket_->async_send_reliable(buf, msgType);


	if (socket_){
		PA_HANDLER_DBG(
			std::cout<<">>>>> pac async send sample msg to pas: "<<pas_host_<<" data size: "<<buf.size()<<std::endl;
		g_outfile<<">>>>> pac async send sample msg to pas: "<<pas_host_<<" data size: "<<buf.size()<<std::endl;
		)
			socket_->async_send_reliable(buf, msgType);
	}
	else
	{
		PA_HANDLER_DBG(
			std::cout<<">>>>> pac is not connected to pas, sample data lost"<<std::endl;
		g_outfile<<">>>>> pac is not connected to pas, sample data lost"<<std::endl;
		)
	}
}

void pa_handler::async_send_unreliable(const safe_buffer& buf, message_type msgType)
{
	if (socket_){
		PA_HANDLER_DBG(
			std::cout<<">>>>> pac async send sample msg to pas: "<<pas_host_<<" data size: "<<buf.size()<<std::endl;
		g_outfile<<">>>>> pac async send sample msg to pas: "<<pas_host_<<" data size: "<<buf.size()<<std::endl;
		)
			socket_->async_send_unreliable(buf, msgType);
	}
	else
	{
		PA_HANDLER_DBG(
			std::cout<<">>>>> pac is not connected to pas, sample data lost"<<std::endl;
		g_outfile<<">>>>> pac is not connected to pas, sample data lost"<<std::endl;
		)
	}
}

void pa_handler::start_schedule()
{
	BOOST_ASSERT(sampling_interval_);
	BOOST_ASSERT(per_submit_block_size_);
	BOOST_ASSERT(last_submit_time_);

	PA_HANDLER_DBG(
		std::cout<<">>>>> pac start schedule"<<std::endl;

	g_outfile<<">>>>> pac start schedule"<<std::endl;
	)
		if( !schedule_timer_)
		{
			schedule_timer_ = rough_timer::create(get_io_service());
			schedule_timer_->register_time_handler(boost::bind(&this_type::schedule, this));
		}
		schedule_timer_->cancel();
		schedule_timer_->async_keep_waiting(seconds(schedule_delay_), seconds(sampling_interval_.get()));
		//std::cout<<"expires_from_now: "<<schedule_timer_->expires_from_now()<<std::endl;

}

void pa_handler::stop_schedule()
{
	if(schedule_timer_)
	{
		PA_HANDLER_DBG(
			std::cout<<">>>>> pac stop schedule"<<std::endl;
		g_outfile<<">>>>> pac stop schedule"<<std::endl;
		)
			schedule_timer_->cancel();
	}
}

void pa_handler::schedule()
{
	PA_HANDLER_DBG(
		std::cout<<"sample submit schedule times: "<< send_queue_.size()<<std::endl;
	);

	boost::mutex::scoped_lock lk(viewing_state_.mutex);
	if(!viewing_state_.is_watching)
	{
		PA_HANDLER_DBG(
			std::cout<<"ppc not in watching state"<<std::endl;
		g_outfile<<"ppc not in watching state"<<std::endl;
		)
	}
	else
	{
		view_sample	vs;
		vs.type = viewing_state_.type;
		vs.channel_link = viewing_state_.cur_channel_link;
		if(boost::iequals(vs.type, "dvb")){
			vs.channel_name = viewing_state_.cur_channel_name;
		}
		if( schedule_delay_ != 0){
			last_submit_time_ = boost::optional<uint32_t>(schedule_delay_+last_submit_time_.get());
			schedule_delay_ = 0;
		}
		else{
			last_submit_time_ = boost::optional<uint32_t>(sampling_interval_.get()+last_submit_time_.get());
		}
		vs.timestamp = last_submit_time_.get();
		send_queue_.push(vs);
		if(send_queue_.size() >= boost::lexical_cast<unsigned int>(per_submit_block_size_.get())){
			submit_sample(cs_wait_send);
		}
	}
}

void pa_handler::submit_sample(client_state cs)
{
	if( client_state_ != cs_logined)
	{
		return ;
	}

	if(send_queue_.empty())
	{
		return ;
	}

	if( pac_mode_ == pm_reconnect_when_send)
	{
		PA_HANDLER_DBG(
			g_outfile<<"submit_sample, pm_reconnect_when_send"<<std::endl;
		)
			client_state_ = cs;
		__connect_pas();
	}
	else if( pac_mode_ == pm_keep_connection)
	{
		PA_HANDLER_DBG(
			g_outfile<<"submit_sample, pm_keep_connection"<<std::endl;
		)
			//get_io_service().post(make_alloc_handler(boost::bind(&this_type::do_submit_sample, SHARED_OBJ_FROM_THIS, p2common::serialize(sample_msg))));
			do_submit_sample();
	}
	else
	{
		//BOOST_ASSERT(0 && "submit_sample bad pac_mode_");
	}
}

long pa_handler::get_sample_msg(proanalytics::sample_msg& sample_msg)
{
	long last_timestamp = 0;
	if( !send_queue_.empty())
	{
		sample_msg.set_mac(mac_);
		sample_msg.set_operators(operators_);
		while( !send_queue_.empty())
		{
			//std::cout<<"queue_size: "<<send_queue_.size()<<std::endl;

			view_sample vs = send_queue_.front();
			proanalytics::sample* ps = sample_msg.add_sample_list();
			ps->set_type(vs.type);
			ps->set_channel_link(vs.channel_link);
			if(boost::iequals(vs.type, "dvb")){
				ps->set_channel_name(vs.channel_name);
			}
			ps->set_time_swamp(vs.timestamp);
			last_timestamp = vs.timestamp;
			send_queue_.pop();
		}
	}
	return last_timestamp;
}

void pa_handler::do_submit_sample()
{
	if (mac_.empty() && !ppc::g_orig_mac_id.empty())
	{
		mac_ = ppc::g_orig_mac_id;
	}
	std::cout<<"pas mac: "<<mac_<<std::endl;

	proanalytics::sample_msg sample_msg;
	long last_timestamp = get_sample_msg(sample_msg);
	PA_HANDLER_DBG(
		std::cout<<"last_timestamp: "<<last_timestamp<<std::endl;
	g_outfile<<"last_timestamp: "<<last_timestamp<<std::endl;
	)

		async_send_reliable( p2common::serialize(sample_msg), p2common::proanalytics_msg::sample_msg_2);
	//async_send_unreliable( p2common::serialize(sample_msg), p2common::proanalytics_msg::sample_msg);
}

void pa_handler::on_connected(message_socket* conn, error_code ec)
{ 
	error_code e;
	endpoint edp = conn->remote_endpoint(e);
	std::cout<<"pas is conneced "<<edp.address().to_string()<<":"<<edp.port()<<std::endl;

	BOOST_ASSERT(ec||conn&&conn->is_open());

	if(relogin_timer_)
		relogin_timer_->cancel();

	if(!ec)
	{
		PA_HANDLER_DBG(
			std::cout<<std::endl<<">>>>> CONNECT PAS SUCCESS: "<<pas_host_<<" now="
			<<(timestamp_t)timestamp_now()
			<<", time="<<time_minus(timestamp_now(), start_time)<<std::endl;
		g_outfile<<std::endl<<">>>>> CONNECT PAS SUCCESS: "<<pas_host_<<" now="
			<<(timestamp_t)timestamp_now()
			<<", time="<<time_minus(timestamp_now(), start_time)<<std::endl;
		);

		BOOST_AUTO(itr, socket_map_.find(conn));
		if(socket_map_.end()==itr)
			return;
		if(socket_map_.empty())
			return;
		if (socket_==itr->second)
			return;
		BOOST_ASSERT(socket_!=itr->second);

		error_code err;
		socket_=itr->second;
		local_edp_=socket_->local_endpoint(err);
		local_edp_->address(address());
		socket_map_.erase(itr);

		while(!socket_map_.empty())
		{
			if(socket_map_.begin()->second)
				socket_map_.begin()->second->close();
			socket_map_.erase(socket_map_.begin());
		}

		connect_state_ = s_connected;
		socket_->register_disconnected_handler(boost::bind(&this_type::on_disconnected, this, conn, _1));

		if( pac_mode_ == pm_reconnect_when_send 
			&& (client_state_ == cs_wait_send || client_state_ == cs_wait_send_and_close))
		{
			do_submit_sample();
			if( client_state_ == cs_wait_send)
			{
				client_state_ = cs_logined;
			}
			else
			{
				client_state_ = cs_unlogin;
			}

			//socket_->close();
			//socket_.reset();

			socket_->register_writable_handler(boost::bind(&this_type::on_sentout_and_close, this, conn));
			PA_HANDLER_DBG(
				std::cout<<"on_connected, do_submit_sample ok, client_state_: "<<client_state_<<std::endl;
			g_outfile<<"on_connected, do_submit_sample ok, client_state_: "<<client_state_<<std::endl;
			)
				return ;
		}

		register_message_handler(conn);
		//socket_->writable_signal().bind(&this_type::on_sentout_and_close, this, conn);
		socket_->keep_async_receiving();
	}
	else
	{
		PA_HANDLER_DBG(
			if(conn){
				std::cout<<(conn->connection_category()==message_socket::UDP?"UDP":"TCP")
					<<" connect "<<pas_host_<<" failed, local edp:"<<conn->local_endpoint(ec)<<std::endl;
				g_outfile<<(conn->connection_category()==message_socket::UDP?"UDP":"TCP")
					<<" connect "<<pas_host_<<" failed, local edp:"<<conn->local_endpoint(ec)<<std::endl;
			}
			)

				BOOST_AUTO(itr, socket_map_.find(conn));
			if(itr!=socket_map_.end())
			{
				socket_map_.erase(itr);
			}
			if (socket_map_.empty())
			{
				if(relogin_timer_)
				{
					relogin_timer_->cancel();
					relogin_timer_->async_wait(seconds(std::min(2*relogin_times_, 64)));
				}
				else
				{
					start_relogin_timer();
				}	
			}
	}
}

void pa_handler::start_relogin_timer()
{
	PA_HANDLER_DBG(
		std::cout<<"start_relogin_timer"<<std::endl;
	g_outfile<<"start_relogin_timer"<<std::endl;
	)

		if(!relogin_timer_)
		{
			relogin_timer_=rough_timer::create(get_io_service());
			relogin_timer_->set_obj_desc("client::pa_handler::relogin_timer_");
			relogin_timer_->register_time_handler(boost::bind(&this_type::__connect_pas, this));
		}
		relogin_timer_->cancel();
		relogin_timer_->async_wait(seconds(2));
		relogin_times_ = 0;
}

void pa_handler::on_receive_init_msg(const safe_buffer& buf, message_socket* conn)
{
	proanalytics::init_msg	msg;
	if(!p2common::parser(buf, msg))
	{
		PA_HANDLER_DBG(
			std::cout<<"parser failed"<<std::endl;
		g_outfile<<"parser failed"<<std::endl;
		)
			BOOST_ASSERT(0);
	}

	if( client_state_ == cs_logined)
	{
		return ;
	}

	PA_HANDLER_DBG(
		std::cout<<std::endl<<"pac receiveed pas init msg: "<<std::endl;
	std::cout<<"sampling_interval_: "<<msg.sample_interval()<<std::endl;
	std::cout<<"per_submit_block_size_: "<<msg.packet_sample_count()<<std::endl;
	std::cout<<"last_submit_time_: "<<msg.curr_time()<<std::endl;

	g_outfile<<std::endl<<"pac receiveed pas init msg: "<<std::endl;
	g_outfile<<"sampling_interval_: "<<msg.sample_interval()<<std::endl;
	g_outfile<<"per_submit_block_size_: "<<msg.packet_sample_count()<<std::endl;
	g_outfile<<"last_submit_time_: "<<msg.curr_time()<<std::endl;
	);

	if( client_state_ == cs_unlogin)
	{
		client_state_ = cs_logined;
	}


	sampling_interval_ = boost::optional<uint32_t>(msg.sample_interval());
	per_submit_block_size_ = boost::optional<uint32_t>(msg.packet_sample_count());
	last_submit_time_ = boost::optional<uint64_t>(msg.curr_time());
	time_t	temp_time = msg.curr_time();
	struct tm* g_tm = localtime(&temp_time);
	if( g_tm->tm_sec % sampling_interval_.get() != 0)
	{
		schedule_delay_ = sampling_interval_.get() - ( g_tm->tm_sec % sampling_interval_.get());
	}

	PA_HANDLER_DBG(
		std::cout<<"receive server time: "<<asctime(g_tm)<<std::endl;
	//std::cout<<"g_tm->tm_sec: "<<g_tm->tm_sec<<std::endl;
	//std::cout<<"sampling_interval_.get(): "<<sampling_interval_.get()<<std::endl;
	//std::cout<<"tm: "<<asctime(g_tm)<<" delay_time:"<<schedule_delay_<<std::endl;
	g_outfile<<"receive server time: "<<asctime(g_tm)<<std::endl;
	);

	if( pac_mode_ == pm_reconnect_when_send)
	{
		conn->close();
		socket_.reset();
		PA_HANDLER_DBG(
			std::cout<<"on_receive_init_msg, init ok, close connection "<<std::endl;
		g_outfile<<"on_receive_init_msg, init ok, close connection "<<std::endl;
		)
	}

	start_schedule();

}

void pa_handler::on_receive_control_msg(const safe_buffer& buf, message_socket* conn)
{
	PA_HANDLER_DBG(
		std::cout<<"client receive control msg from pas"<<std::endl;
	g_outfile<<"client receive control msg from pas"<<std::endl;
	)
}

void pa_handler::on_disconnected(message_socket* sock, const error_code& ec)
{
	PA_HANDLER_DBG(
		std::cout<<"pac disconnected from pas: "<<ec.message()<<std::endl;
	g_outfile<<"pac disconnected from pas: "<<ec.message()<<std::endl;
	)
		connect_state_ = s_unconnected;

	if (socket_) 
	{
		socket_->close();
		socket_.reset();
	}
	if(relogin_timer_)
	{
		relogin_timer_->cancel();
		relogin_timer_.reset();
	}
	socket_map_.clear();

	if( client_state_ == cs_logined && pac_mode_ == pm_keep_connection)
	{
		start_relogin_timer();
	}
}


void pa_handler::on_sentout_and_close(message_socket* conn)
{
	PA_HANDLER_DBG(
		std::cout<<"on_sentout_and_close, send ok, close connection"<<std::endl;
	g_outfile<<"on_sentout_and_close, send ok, close connection"<<std::endl;
	)

	conn->unregister_writable_handler();;
	conn->close();
	socket_.reset();
}


//sample_type pa_handler::viewingType2Sampletype(vsr::viewing_type type)
//{
//	if( type == vsr::VIEWING_P2S) 
//		return proanalytics::P2S;
//	else if( type == vsr::VIEWING_P2V)
//		return proanalytics::P2V;
//	else if( type == vsr::VIEWING_DVB) 
//		return proanalytics::DVB;
//	else 
//		return proanalytics::SAMPLE_UNKNOW;
//}

//void pa_handler::start_test_report_timer()
//{
//	if(!test_timer_)
//	{
//		test_timer_=rough_timer::create(get_io_service());
//		test_timer_->register_time_handler(boost::bind(&this_type::test_report, this);
//	}
//	test_timer_->cancel();
//	test_timer_->async_keep_waiting(seconds(2), seconds(2));
//}
//
//void pa_handler::test_report()
//{
//	std::cout<<"test_report to report state"<<std::endl;
//
//	proanalytics::sample_msg sample_msg;
//	proanalytics::sample* ps = sample_msg.add_sample_list();
//	ps->set_type(proanalytics::P2S);
//	ps->set_channel_id(123);
//	ps->set_channel_name("123");
//	ps->set_time_swamp(time(NULL));
//	connection_->async_send_unreliable(p2common::serialize(sample_msg), p2common::proanalytics_msg::sample_msg);
//}


NAMESPACE_END(pac);
