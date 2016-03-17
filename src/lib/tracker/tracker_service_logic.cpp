#include "tracker/member_service.h"
#include "tracker/tracker_service_logic.h"
#include "tracker/tracker_service.h"
using namespace p2tracker;

tracker_service_logic_base::tracker_service_logic_base(io_service& svc)
:basic_engine_object(svc)
{
	set_obj_desc("tracker_service_logic");
}

tracker_service_logic_base::~tracker_service_logic_base()
{
	__stop();
}

void tracker_service_logic_base::start(const tracker_param_base& param)
{
	if (tracker_service_)
		return;
	tracker_service_=tracker_service::create(get_io_service(), 
		create_tracker_param_sptr(param)
		);
	tracker_service_->start(SHARED_OBJ_FROM_THIS);
}

void tracker_service_logic_base::__stop()
{
	if (tracker_service_)
	{
		tracker_service_->stop();
		tracker_service_.reset();
	}
}
