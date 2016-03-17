#ifndef _COMMON_FEC_H__
#define _COMMON_FEC_H__

#include "common/config.h"
#include "common/packet_buffer.h"
#include <vector>

namespace p2common{

	class packet_fec
	{
	public:
		packet_fec(int m, bool decoder):m_(m), mm_(0), len_header_(0), decoder_(decoder){}
		void reset()
		{		
			len_header_=0;
			buf_.resize(0);//must set to size 0
			mm_=0;
		}
		bool operator()(const safe_buffer& mediaPacket, safe_buffer& result);
	private:
		const int m_;
		int mm_;
		int32_t len_header_;
		std::string buf_;
		bool decoder_;
	};

	class fec_encoder
	{
	public:
		fec_encoder();
		bool operator()(const media_packet& pkt, std::vector<media_packet>&results);

	private:
		std::vector<packet_fec> fec_;
		std::vector<media_packet> fec_packets_;
		bool found_first_fec_seqno_;
	};

	class fec_decoder
	{
	public:
		fec_decoder();

		bool operator()(seqno_t theLostSeqno, const packet_buffer& media_packets, 
			const std::map<seqno_t, media_packet, wrappable_less<seqno_t> >& fecPkts, 
			const std::string& pktCasString, timestamp_t now, 
			safe_buffer&decodeResult
			);

	private:
		packet_fec packet_fec_decoder_;
	};
}


#endif//_COMMON_FEC_H__
