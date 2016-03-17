#ifndef _SHUNT_RECEIVER_H__
#define _SHUNT_RECEIVER_H__

#include "shunt/config.h"

namespace p2shunt{

	class receiver
		:public basic_engine_object
	{
	public:
		typedef boost::function<void(const safe_buffer&)> media_handler_type;
		media_handler_type media_handler;

	public:
		virtual void stop()=0;
		virtual bool is_connected()const=0;
		virtual bool updata(const std::string& edpStr, error_code& ec)=0;
		double  average_media_speed()//kbps
		{
			return speed_meter_.bytes_per_second()*8/1024;
		}
		double  average_packet_speed()//packets per second
		{
			return packet_speed_meter_.bytes_per_second();
		}
		double instantaneous_media_speed()//kbps
		{
			return instantaneous_speed_meter_.bytes_per_second()*8/1024;
		}
		double  instantaneous_packet_speed()//packets per second
		{
			return instantaneous_packet_speed_meter_.bytes_per_second();
		}
	protected:
		receiver(io_service& ios)
			:basic_engine_object(ios)
			, speed_meter_(seconds(15))
			, packet_speed_meter_(seconds(15))
			, instantaneous_speed_meter_(millisec(500))
			, instantaneous_packet_speed_meter_(millisec(500))
		{}

	protected:
		rough_speed_meter speed_meter_;
		rough_speed_meter packet_speed_meter_;
		rough_speed_meter instantaneous_speed_meter_;
		rough_speed_meter instantaneous_packet_speed_meter_;
	};

}

#endif//_SHUNT_RECEIVER_H__

