#ifndef p2s_p2p_shunt_creator_h__
#define p2s_p2p_shunt_creator_h__

#include <string>
#include <boost/shared_ptr.hpp>
#include <p2engine/p2engine.hpp>

using namespace p2engine;

namespace p2shunt
{
	class receiver;
	class sender;

	struct receiver_creator
	{
		static boost::shared_ptr<receiver> create(const std::string& url, 
			io_service& ios, bool usevlc=false);
	};

	struct sender_creator
	{
		static boost::shared_ptr<sender> create(const std::string& url, 
			io_service& ios);
	};
};

#endif // p2s_p2p_shunt_creator_h__
