#include "client/stream/scheduling_typedef.h"
#include "client/stream/stream_scheduling.h"

NAMESPACE_BEGIN(p2client);

basic_stream_scheduling::basic_stream_scheduling(stream_scheduling& scheduling)
	:basic_client_object(scheduling.get_client_param_sptr())
	, basic_engine_object(scheduling.get_io_service())
	, scheduling_(&scheduling)
{
}

basic_stream_scheduling::~basic_stream_scheduling()
{
}

NAMESPACE_END(p2client);

