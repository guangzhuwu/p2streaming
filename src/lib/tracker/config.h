//
// typedef.h
// ~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2008 GuangZhu Wu (GuangZhuWu@gmail.com)
//
// All rights reserved. 
//
#ifndef tracker_typedef_h__
#define tracker_typedef_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "common/common.h"

namespace p2tracker
{
	using namespace p2engine;
	using namespace p2common;
	using namespace p2message;

	class basic_tracker_object
	{
	public:
		basic_tracker_object(const tracker_param_sptr& param)
			:tracker_param_(param)
		{
		}
		virtual ~basic_tracker_object()
		{
		}
		tracker_param_sptr& get_tracker_param_sptr()
		{
			return tracker_param_;
		}
		const tracker_param_sptr& get_tracker_param_sptr()const
		{
			return tracker_param_;
		}
	protected:
		tracker_param_sptr tracker_param_;
	};
}

#endif//tracker_typedef_h__
