#include "client/client_service.h"
#include "client/client_service_logic.h"

NAMESPACE_BEGIN(p2client);

client_service_logic_base::client_service_logic_base(io_service& iosvc)
	:basic_engine_object(iosvc)
	, basic_client_object(client_param_sptr())
{
	set_obj_desc("client_service_logic");
}

client_service_logic_base::~client_service_logic_base()
{
	stop_service();
}

void client_service_logic_base::start_service(const client_param_base& param)
{
	if(client_service_)//already started
		return;

	this->client_param_=create_client_param_sptr(param);
	client_service_=client_service::create(get_io_service(), get_client_param_sptr());
	get_io_service().post(make_alloc_handler(
		boost::bind(&client_service::start, client_service_, SHARED_OBJ_FROM_THIS)
		));
}

void client_service_logic_base::stop_service(bool flush)
{
	if(client_service_)
	{
		client_service_->stop(flush);
		client_service_.reset();
	}
}

void client_service_logic_base::set_play_offset(int64_t offset)
{
	if(client_service_)
		client_service_->set_play_offset(offset);
}

NAMESPACE_END(p2client);
