#include "common/common.h"
#include "p2s_shunt/alive_alarm.h"
#include "p2s_shunt/version.h"
#include "p2s_shunt/shunt.pb.h"
#include "app_common/interprocess.h"
#include "shunt/shunt.h"

progress_alive_alarm::progress_alive_alarm(const std::string&id, 
	p2sshunt& shunt, int guardPort)
	: shunt_(shunt)
{
	DEBUG_SCOPE(
		std::cout << "-------------------alarm on port: " << guardPort << "\n";
	);
}

progress_alive_alarm::~progress_alive_alarm()
{
	if (timer_)
		timer_->cancel();
}

void progress_alive_alarm::start(boost::shared_ptr<interprocess_client> proxy)
{
	interprocess_client_ = proxy;

	if (!timer_)
	{
		timer_ = rough_timer::create(shunt_.get_io_service());
		timer_->set_obj_desc("p2s_shunt::progress_alive_alarm::timer_");
		timer_->register_time_handler(boost::bind(&this_type::on_timer, this));
		timer_->async_keep_waiting(millisec(random(0, 10000)), millisec(10000));
	}
}

void progress_alive_alarm::on_timer()
{
	shunt_alive::Alive msg;
	msg.set_id(shunt_.id());
	msg.set_is_connected(shunt_.is_connected());
	msg.set_kbps((int)shunt_.average_media_speed());
	msg.set_pid(getpid());

	LogInfo("id: %s, is_connected: %d, kbps:%2f", shunt_.id(), shunt_.is_connected(), shunt_.average_media_speed());

	if (interprocess_client_.lock())
		interprocess_client_.lock()->send(serialize(msg), control_cmd_msg::alive_alarm);
}
