#include "client/peer_connection.h"

NAMESPACE_BEGIN(p2client);

peer_connection::peer_connection(io_service& ios, bool realTimeUsage, bool isPassive)
	:message_socket(ios, realTimeUsage, isPassive)
	, local_id_(INVALID_ID)
	, local_uuid_(INVALID_ID)
{}
 
peer_connection::~peer_connection()
{
	if (peer_)peer_->be_member(this);
}

bool peer_connection::is_link_local()
{
	if (!is_link_local_.is_initialized())
	{
		if (is_connected())
		{
			error_code ec;
			is_link_local_ = is_local(remote_endpoint(ec).address());
		}
		else
			return false;
	}
	return is_link_local_.get();
}

NAMESPACE_END(p2client);
