#include "common/common.h"
#include "p2s_mds/progress_alive_alarm.h"
#include "p2s_mds/version.h"
#include "app_common/mds.pb.h"
#include "app_common/interprocess.h"
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
#endif

//////////////////////////////////////////////////////////////////////////
///progress_alive_alarm
progress_alive_alarm::progress_alive_alarm(
	boost::shared_ptr<server_service_logic_base> s, 
	const time_duration& interval, 
	const std::string& id, 
	int type, 
	int guardPort
	)
	:basic_engine_object(s->get_io_service())
	, guard_socket_(s->get_io_service())
	, server_(s)
	, interval_(interval.total_milliseconds())
	, id_(id)
	, type_(type)
	, last_alarm_time_(ptime_now()-seconds(10))
{
	error_code ec;
	reset_alarm_port(guardPort);
}
progress_alive_alarm::~progress_alive_alarm()
{
	stop();
}
void progress_alive_alarm::reset_alarm_port(int port)
{
	guard_remote_edp_.address(address_v4::loopback());
	guard_remote_edp_.port(port);
}

void progress_alive_alarm::start(boost::shared_ptr<interprocess_client> proxy, error_code&)
{
	interprocess_client_=proxy;

	if (timer_)
		return;
	last_alarm_time_ = ptime_now()-seconds(10);
	timer_=rough_timer::create(get_io_service());
	timer_->set_obj_desc("p2s_mds::progress_alive_alarm::timer_");
	timer_->register_time_handler(boost::bind(&this_type::on_timer, this));
	timer_->async_keep_waiting(millisec(random(500, 1000)), millisec(5000));
}

void progress_alive_alarm::stop()
{
	if (timer_)
	{
		timer_->cancel();
		timer_.reset();
	}
}

void progress_alive_alarm::reset(error_code& ec)
{
	start(interprocess_client_.lock(), ec);
}

void progress_alive_alarm::on_timer()
{
	//guard
	ptime now=ptime_now();
	if (is_time_passed(millisec(interval_), last_alarm_time_, now))
	{
		last_alarm_time_=now;
		BOOST_ASSERT(server_);

		alive::mds_Alive msg;
		msg.set_type(type_);
		msg.set_id(id_);
		msg.set_channel_bitrate(server_->bitrate());
		msg.set_out_kbps(server_->out_kbps());
		msg.set_client_count(server_->client_count());
		msg.set_p2p_efficient(server_->p2p_efficient());
		msg.set_playing_quality(server_->playing_quality());
		msg.set_global_remote_to_local_lost_rate(server_->global_remote_to_local_lost_rate());

		if(interprocess_client_.lock())
			interprocess_client_.lock()->send(serialize(msg), control_cmd_msg::alive_alarm);
		else{
			BOOST_ASSERT(0);
			exit(-1);
		}
	}
}

void progress_alive_alarm::on_quality_reported(const safe_buffer& buf)
{
	p2ts_quality_report_msg msg;
	if(!parser(buf, msg))
		return;

	error_code ec;
	guard_socket_.send_to(serialize(msg).to_asio_const_buffers_1(), 
		guard_remote_edp_, 0, ec);

	DEBUG_SCOPE(
		std::cout<<"quality: -------------------------"
		<<"\n buffer health: "<<msg.buffer_health()
		<<"\n quality: "<<msg.playing_quality()
		<<"\n push rate: "<<msg.push_rate()
		<<std::endl;
	);
}