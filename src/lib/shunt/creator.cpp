#include "shunt/creator.h"
#include "shunt/fluid_receiver.h"
#include "shunt/media_receiver.h"
#include "shunt/fluid_sender.h"
#include "shunt/media_sender.h"

NAMESPACE_BEGIN(p2shunt);

boost::shared_ptr<receiver> receiver_creator::create(const std::string& url,
	io_service& ios, bool usevlc)
{
	error_code ec;
	uri u(url, ec);
	if (ec)
		return boost::shared_ptr<receiver>();

	if (usevlc)
	{
		return vlc_fluid_receiver::create(ios);
	}
	else if ("udp" == u.protocol())
	{
		if (is_multicast(endpoint_from_string<udp::endpoint>(u.host()).address()))
			return multicast_fluid_receiver::create(ios);
		else
			return unicast_fluid_receiver::create(ios);
	}
	else if ("http" == u.protocol())
	{
		return http_fluid_receiver::create(ios);
	}
	else if ("shunt" == u.protocol())
	{
		return media_receiver::create(ios);
	}
	return boost::shared_ptr<receiver>();
}

boost::shared_ptr<sender> sender_creator::create(
	const std::string& url, io_service& ios)
{
	error_code ec;
	uri u(url, ec);
	if (ec)
		return boost::shared_ptr<sender>();

	if ("udp" == u.protocol())
	{
		return udp_fluid_sender::create(ios);
	}
	else if ("http" == u.protocol())
	{
		return http_fluid_sender::create(ios);
	}
	else if ("shunt" == u.protocol())
	{
		return media_sender::create(ios);
	}

	return boost::shared_ptr<sender>();
}

NAMESPACE_END(p2shunt);