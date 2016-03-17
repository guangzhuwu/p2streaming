//
// security_policy.h
// ~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2008 GuangZhu Wu (GuangZhuWu@gmail.com)
//
// All rights reserved. 
//

#ifndef common_security_policy_h__
#define common_security_policy_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include <utility>//std::pair

#include "common/parameter.h"
#include "common/media_packet.h"

namespace p2common{

	class security_policy
	{
	public:
		static std::string get_machine_id();
		static std::string generate_shared_key(const std::string& digest, const std::string &hispublic);
		static std::pair<std::string, std::string> generate_key_pair();

		static std::string client_encrypt_tracker_challenge(const std::string& challenge, const client_param_sptr&);

		static int64_t signature_mediapacket(const media_packet& pkt, const std::string& channelID);
		static void cas_mediapacket(media_packet& pkt, const std::string& casKeyStr);
	};
}

#endif//common_security_policy_h__




