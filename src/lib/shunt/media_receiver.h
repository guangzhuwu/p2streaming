#ifndef _SHUNT_MEDIA_RECEIVER_H__
#define _SHUNT_MEDIA_RECEIVER_H__

#include "shunt/receiver.h"

namespace p2shunt{
	using namespace  p2client;

	class client_for_receiver;

	class media_receiver
		:public receiver
	{
		typedef media_receiver this_type;
		SHARED_ACCESS_DECLARE;
		friend class client_for_receiver;
	public:
		static shared_ptr create(io_service& ios)
		{
			return shared_ptr(new this_type(ios), 
				shared_access_destroy<this_type>());
		}

		virtual void stop(){};
		virtual bool is_connected()const{return is_connected_;};
		virtual bool updata(const std::string& url, error_code& ec);

		void handle_media(const safe_buffer&);

	protected:
		media_receiver(io_service& ios);
		~media_receiver();

	protected:
		boost::shared_ptr<client_service_logic_base> client_interface_;
		variant_endpoint the_edp_;
		std::pair<std::string, std::string> key_pair_;//

		bool is_connected_;
	};
}

#endif//_SHUNT_MEDIA_RECEIVER_H__

