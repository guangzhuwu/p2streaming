#include "common/fec.h"
#include "common/utility.h"
#include "common/security_policy.h"

NAMESPACE_BEGIN(p2common);

//////////////////////////////////////////////////////////////////////////
//!!!!!!!!不要修改这两个数字!!!!!!!!!!!!!
enum
{
	FEC_M=15, 
	FEC_N=2
};
//BOOST_STATIC_ASSERT((~boost::make_unsigned<seqno_t>::type(0)+(int64_t)1)%((FEC_M+1)*FEC_N)==0);

bool packet_fec::operator()(const safe_buffer& mediaPacket, safe_buffer& result)
{
	const char* mediaPtr = buffer_cast<const char*>(mediaPacket);
	size_t minLen = std::min(buf_.size(), mediaPacket.size());
	if (minLen > 0)
		fast_xor(&buf_[0], minLen, mediaPtr, minLen);
	if (buf_.size() < mediaPacket.size())
	{
		buf_.resize(mediaPacket.size());
		memcpy(&buf_[minLen], &mediaPtr[minLen], mediaPacket.size() - minLen);
	}

	len_header_ ^= mediaPacket.size();
	if (++mm_ == m_)
	{
		safe_buffer_io io(&result);
		io.write(&buf_[0], buf_.size());
		if (!decoder_)
		{
			len_header_ ^= (buf_.size() + 4);
			io << len_header_;
		}
		else
		{
			if (result.size() < 4)
				result.resize(0);
			else
			{
				safe_buffer lenBuf = result.buffer_ref(result.size() - 4);
				safe_buffer_io io2(&lenBuf);
				int32_t len_result;
				io2 >> len_result;
				len_result ^= len_header_;
				if (len_result<0 || len_result>(int)result.size())
					result = safe_buffer();
				else
					result = result.buffer_ref(0, len_result);
			}
		}
		reset();
		return true;
	}
	return false;
}

//////////////////////////////////////////////////////////////////////////
fec_encoder::fec_encoder()
	:fec_(FEC_N, packet_fec(FEC_M, false))
	, found_first_fec_seqno_(false)
{
}

bool fec_encoder::operator()(const media_packet& pkt, std::vector<media_packet>&results)
{
	//比如FEC_M=3 FEC_N=2, 编码如下图
	// 0  2  4  (6)  8  10  12  (14)  子流0
	// 1  3  5  (7)  9  11  13  (15)  子流1

	static bool GOOD=(~boost::make_unsigned<seqno_t>::type(0)+(int64_t)1)%((FEC_M+1)*FEC_N)==0;
	static const seqno_t n=FEC_N*(FEC_M+1);//一个编码块的长度
	static const seqno_t MAX_CODE_SEQ=seqno_t(~seqno_t(0))-n;
	seqno_t seqno=pkt.get_seqno();
	if (!GOOD&&uint64_t(seqno)>=uint64_t(MAX_CODE_SEQ))
	{//在靠近最大seqno_t时候就不做FEC了，便于seqno_t回卷后，下次编码一定从seqno_t(0)开始
		fec_packets_.clear();
		for (size_t i=0;i<FEC_N;++i)
			fec_[i].reset();
		return false;
	}
	seqno_t i=pkt.get_seqno()%n;
	if (i<FEC_N)//一定是一个新FEC开始的片段
	{
		if (i==0)
			found_first_fec_seqno_=true;
		fec_[i].reset();
	}
	if (!found_first_fec_seqno_)
		return false;

	seqno_t j=i%FEC_N;//应该属于的编码子流

	safe_buffer buf;
	media_packet result;
	if(fec_[j](pkt.buffer(), buf))
	{
		result.set_level(SYSTEM_LEVEL);
		result.set_channel_id(FEC_MEDIA_CHANNEL);
		safe_buffer_io io(&result.buffer());
		io.write(buffer_cast<char*>(buf), buf.length());
		DEBUG_SCOPE(
			seqno_t fecSeq=pkt.get_seqno()%(FEC_N*(FEC_M+1));
			seqno_t fecSub=pkt.get_seqno()%FEC_N;
			seqno_t fecFirstSeqno=pkt.get_seqno()-fecSeq+fecSub;
			seqno_t theFecEncodeSeq=fecFirstSeqno+FEC_N*FEC_M;
			BOOST_ASSERT(theFecEncodeSeq-FEC_N==pkt.get_seqno());
			);
		fec_packets_.push_back(result);
	}
	if (fec_packets_.size()==FEC_N)
	{
		BOOST_ASSERT(j==FEC_N-1);//必须是最后一个子流
		results=fec_packets_;
		fec_packets_.clear();
		return true;
	}
	
	return false;
}

//////////////////////////////////////////////////////////////////////////
fec_decoder::fec_decoder():packet_fec_decoder_(FEC_M, true)
{
}

bool fec_decoder::operator()(seqno_t theLostSeqno, const packet_buffer& media_packets, 
	const std::map<seqno_t, media_packet, wrappable_less<seqno_t> >& fecPkts, 
	const std::string& casstring, timestamp_t now, 
	safe_buffer&decodeResult
	)
{
	bool fecOK=false;

	seqno_t fecSeq=theLostSeqno%(FEC_N*(FEC_M+1));
	seqno_t fecSub=theLostSeqno%FEC_N;
	seqno_t fecFirstSeqno=theLostSeqno-fecSeq+fecSub;
	seqno_t theFecEncodeSeq=fecFirstSeqno+FEC_N*FEC_M;

	BOOST_AUTO(itr, fecPkts.find(theFecEncodeSeq));
	if (itr==fecPkts.end())
		return false;

	seqno_t seq=fecFirstSeqno;
	for (int i=0;i<=FEC_M;++i, seq+=FEC_N)
	{
		if (seq==theLostSeqno)
			continue;
		else if(!const_cast<packet_buffer&>(media_packets).has(seq, now))
			return false;
	}

	packet_fec_decoder_.reset();

	media_packet fecPkt(itr->second.buffer().clone());
	security_policy::cas_mediapacket(fecPkt, casstring);
	packet_fec_decoder_(fecPkt.buffer().buffer_ref(media_packet::format_size()), decodeResult);
	media_packet tmpPkt;
	seq=fecFirstSeqno;
	for (int i=0;i<=FEC_M;++i, seq+=FEC_N)
	{
		if (seq==theLostSeqno)
			continue;
		else if(!media_packets.get(tmpPkt, seq, now))
			break;
		if(packet_fec_decoder_(tmpPkt.buffer(), decodeResult))
		{
			fecOK=true;
			break;
		}
	}
	return fecOK;
}

NAMESPACE_END(p2common);
