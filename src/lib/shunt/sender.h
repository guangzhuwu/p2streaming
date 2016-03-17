#ifndef _SHUNT_SENDER_H__
#define _SHUNT_SENDER_H__

#include "shunt/config.h"

namespace p2shunt{

	//·ÖÁ÷Æ÷
	class sender
		:public p2engine::basic_engine_object
	{
	public:
		virtual void shunt(const safe_buffer& buf)=0;
		virtual bool updata(const std::string& edp, error_code& ec)=0;

	protected:
		sender(io_service& ios)
			:basic_engine_object(ios){}
	};

}

#endif // _SHUNT_SENDER_H__


