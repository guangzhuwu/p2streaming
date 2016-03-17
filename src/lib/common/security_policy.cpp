#include "common/security_policy.h"

#include "common/curve25519.h"
#include "common/md5.h"
#include "common/utility.h"

#include "p2engine/enum_net.hpp"

namespace p2common{

	std::string security_policy::get_machine_id()
	{
		io_service ios;
		error_code ec;
		std::vector<ip_interface> ipf = enum_net_interfaces(ios, ec);
		std::set<std::string> mac_set;
		for (size_t i = 0; i < ipf.size(); ++i)
			mac_set.insert(std::string(ipf[i].mac, 6));

		char digest[16];
		md5_state_t pms;
		md5_init(&pms);
		for (std::set<std::string>::iterator itr = mac_set.begin(); itr != mac_set.end(); ++itr)
		{
			std::cout << "MAC:" << string_to_hex(*itr) << "\n";
			md5_append(&pms, (md5_byte_t*)&((*itr)[0]), 6);
		}
		if (mac_set.empty())
		{
			md5_append(&pms, (md5_byte_t*)"123456", 6);
		}
		md5_finish(&pms, (md5_byte_t*)digest);
		return std::string(digest, 16);
	}

	std::string security_policy::generate_shared_key(const std::string& _digest, const std::string& hispublic)
	{
		char out[32];
		const char basepoint[32] = { 9 };
		std::string digest = _digest;

		if ((int)digest.size() < 32)
		{
			digest.resize(32);
		}
		digest[0] &= 248;
		digest[31] &= 127;
		digest[31] |= 64;

		if (hispublic.empty())	// generate public key
		{
			curve25519(out, digest.c_str(), basepoint);
		}
		else
		{
			curve25519(out, digest.c_str(), hispublic.c_str());
		}
		return std::string(out, 32);
	}

	std::pair<std::string, std::string> security_policy::generate_key_pair()
	{
		io_service ios;
		std::string privateKey = get_machine_id();
		privateKey += md5(&privateKey[0], privateKey.size());
		privateKey.resize(32, (char)0xff);

		privateKey[0] &= 248;
		privateKey[31] &= 127;
		privateKey[31] |= 64;

		std::string pubKey = generate_shared_key(privateKey, "");
		return std::make_pair(pubKey, privateKey);
	}

	std::string security_policy::client_encrypt_tracker_challenge(const std::string& challenge, 
		const client_param_sptr& param)
	{
		std::string theStr = generate_shared_key(param->private_key, challenge);

		md5_byte_t digest[16];
		md5_state_t pms;
		md5_init(&pms);
		md5_append(&pms, (const md5_byte_t *)theStr.c_str(), theStr.length());
		md5_finish(&pms, digest);

		return std::string((char*)digest, 16);
	}

	int64_t security_policy::signature_mediapacket(const media_packet& pkt, const std::string& channelID)
	{
		//计算anti pollution, 检查包的合法性
		boost::uint32_t seqnoForSignature = p2engine::hton<boost::uint32_t>()(pkt.get_seqno());
		boost::uint32_t timestampForSignature = p2engine::hton<boost::uint32_t>()(pkt.get_time_stamp());
		boost::int64_t digest[2];
		md5_state_t pms;
		md5_init(&pms);
		md5_append(&pms, (md5_byte_t*)channelID.c_str(), channelID.length());
		md5_append(&pms, (md5_byte_t*)&seqnoForSignature, sizeof(seqnoForSignature));
		md5_append(&pms, (md5_byte_t*)&timestampForSignature, sizeof(timestampForSignature));
		md5_append(&pms, buffer_cast<md5_byte_t*>(pkt.buffer()) + media_packet::format_size(), pkt.buffer().length() - media_packet::format_size());
		md5_append(&pms, (md5_byte_t*)ANTI_POLLUTION_CODE.c_str(), ANTI_POLLUTION_CODE.length());
		md5_finish(&pms, (md5_byte_t*)digest);

		return digest[0] ^ digest[1];
	}

	void security_policy::cas_mediapacket(media_packet& pkt, const std::string& casKeyStr)
	{
		//计算anti pollution, 检查包的合法性
		boost::uint32_t seqnoForSignature = p2engine::hton<boost::uint32_t>()(pkt.get_seqno());
		boost::uint32_t timestampForSignature = p2engine::hton<boost::uint32_t>()(pkt.get_time_stamp());
		boost::int64_t digest[2];
		md5_state_t pms;
		md5_init(&pms);
		md5_append(&pms, (md5_byte_t*)casKeyStr.c_str(), casKeyStr.length());
		md5_append(&pms, (md5_byte_t*)&seqnoForSignature, sizeof(seqnoForSignature));
		md5_append(&pms, (md5_byte_t*)&timestampForSignature, sizeof(timestampForSignature));
		md5_append(&pms, (md5_byte_t*)ANTI_POLLUTION_CODE.c_str(), ANTI_POLLUTION_CODE.length() / 2);
		md5_finish(&pms, (md5_byte_t*)digest);
		fast_xor(p2engine::buffer_cast<char*>(pkt.buffer()) + media_packet::format_size(), 
			pkt.buffer().length() - media_packet::format_size(), digest, sizeof(digest));
	}



}