#ifndef _SHUNT_MEDIA_SENDER_H__
#define _SHUNT_MEDIA_SENDER_H__

#include "shunt/sender.h"

namespace p2shunt{
	using namespace p2server;

	class media_sender
		:public sender
	{
		typedef media_sender this_type;
		SHARED_ACCESS_DECLARE;
	protected:
		media_sender(io_service& ios);
		virtual ~media_sender();

	public:
		static shared_ptr create(io_service& ios)
		{
			return shared_ptr(new this_type(ios), 
				shared_access_destroy<this_type>()
				);
		}
		
		bool updata(const std::string&url, error_code& ec);

		void shunt(const safe_buffer& pkt);

	private:
		boost::shared_ptr<server_service_logic_base> server_interface_;
		boost::shared_ptr<tracker_service_logic_base> tracker_interface_;
		variant_endpoint the_edp_;
		variant_endpoint the_extrenal_edp_;
	};

}


#endif // _SHUNT_MEDIA_SENDER_H__



