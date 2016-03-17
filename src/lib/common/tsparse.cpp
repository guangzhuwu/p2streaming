#include "tsparse.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <iostream>
#include <stdexcept>

#define ARRAY_SIZE(x)	(sizeof((x))/sizeof(*(x)))
static const int TS_PACKET_SIZE[]={188, 192, 204, 208};
static const int TS_PACKET_SIZE_MAX=TS_PACKET_SIZE[ARRAY_SIZE(TS_PACKET_SIZE)-1];
static const int TS_PACKET_SIZE_MIN=TS_PACKET_SIZE[0];

// H.264 NAL type 
enum H264NALTYPE{ 
	H264NT_NAL = 0, 
	H264NT_SLICE, 
	H264NT_SLICE_DPA, 
	H264NT_SLICE_DPB, 
	H264NT_SLICE_DPC, 
	H264NT_SLICE_IDR, 
	H264NT_SEI, 
	H264NT_SPS, 
	H264NT_PPS, 
}; 

static inline bool check_ts_sync(const unsigned char *buf)
{
	// must have initial sync byte & a legal adaptation ctrl
	return (buf[0] == 0x47) && (((buf[3] & 0x30) >> 4) > 0);
}
static inline bool have_ts_sync(const unsigned char *buf, int psize, int count)
{
	for (int  i = 0; i < count; i++ )
	{
		if ( !check_ts_sync(&buf[i*psize]) )
			return false;
	}
	return true;
}

static inline int H264GetNALType(const unsigned char * pBSBuf, int nBSLen) 
{ 
	if ( nBSLen < 5 )  // 不完整的NAL单元 
		return H264NT_NAL;

	int nType = pBSBuf[4] & 0x1F;  // NAL类型在固定的位置上  
	if ( nType <= H264NT_PPS ) 
		return nType;

	return 0; 
}

void inline ts_decode_pts_dts(const unsigned char *data, long long *value) 
{
	long long pts1 = ((unsigned int)data[0] & 0x0E) >> 1;
	long long pts2 = ((unsigned int)data[1] << 7) | (((unsigned int)data[2] & 0xFE) >> 1);
	long long pts3 = ((unsigned int)data[3] << 7) | (((unsigned int)data[4] & 0xFE) >> 1);
	*value = (pts1 << 30) | (pts2 << 15) | pts3;
}

static inline void init(pes_t* pes)
{
	memset(pes, 0, sizeof(pes_t));
	pes->pts=pes->dts=NOPTS_VALUE;
}

static inline  void init(ts_header_t* th)
{
	memset(th, 0, sizeof(ts_header_t));
}

static inline  void init(ts_t* ts)
{
	memset(ts, 0, sizeof(ts_t));
	ts->pes.pts=ts->pes.dts=NOPTS_VALUE;
}

void ts_parse::reset()
{
	m_nTSPacketSize = 0;
	m_nTSVidId	  = 0;
}
//函数名称：packet_size()
//函数功能：得到TS包的长度
//输入参数：无
//输入参数要求:在执行完sync后，返回的是当前数据段的TS包长度
//返回值：188，或者192，或者204 ，数值0标示前面为调用sync函数，或者前面的sync函数失败
int  ts_parse::packet_size()
{
	return m_nTSPacketSize;
}
//函数名称：sync (const unsigned char *Data, int DataLen)
//函数功能：0x47数据同步
//输入参数：TS流数据unsigned char *Data, 数据长度DataLen。
//输入参数要求:DataLen必须大于204*4=816byte
//返回值：-1 0x47字节同步错误，-2，数据长度不满足816byte
int  ts_parse::sync (const unsigned char *Data, int DataLen)
{
	int		i_sync		= 0;

	int		TSPacketSize= 0;

	enum {min_check_packet_count=4};
	if (DataLen < TS_PACKET_SIZE_MAX*min_check_packet_count+4)
		return -2;

	for( i_sync = 0; i_sync < TS_PACKET_SIZE_MAX ; i_sync++)
	{
		const unsigned char* p=Data+i_sync;
		if(check_ts_sync(p))
		{
			for(size_t i=0;i<ARRAY_SIZE(TS_PACKET_SIZE);++i)
			{
				if(have_ts_sync(p, TS_PACKET_SIZE[i], min_check_packet_count-1))
				{
					TSPacketSize = TS_PACKET_SIZE[i];
					break;
				}
			}
			if (TSPacketSize>0)
			{
				m_nTSPacketSize=TSPacketSize;
				break;
			}
		}

	}
	if( i_sync >= TS_PACKET_SIZE_MAX )
	{
		return -1;
	}
	return i_sync;
}
bool ts_parse::parse_packet_header(const unsigned char *Data, int DataLen, ts_header_t* TSHeader)
{
	if(TSHeader == 0||Data == 0||DataLen<4)
		return false;

	init(TSHeader);
	TSHeader->sync_byte						= Data[0];
	TSHeader->transport_error_indicator		= ((Data[1] & 0x80) >> 7);
	TSHeader->payload_unit_start_indicator	= ((Data[1] & 0x40) >> 6);
	TSHeader->transport_priority			= ((Data[1] & 0x20) >> 5);
	TSHeader->PID							= ((Data[1] & 0x1F) << 8) + Data[2];
	TSHeader->transport_scrambling_control	= ((Data[3] & 0xC0) >> 6);
	TSHeader->adaptation_field_control		= ((Data[3] & 0x30) >> 4);
	TSHeader->continuity_counter			= (Data[3] & 0x0f);
	return true;
}

struct SafeData
{
	SafeData(const unsigned char *Data, size_t DataLen)
		:Data_(Data), DataLen_(DataLen)
	{}
	inline const unsigned char& operator[](size_t pos)const
	{
		if (pos>=DataLen_) throw std::overflow_error("data overflow");
		return Data_[pos];
	}
private:
	const unsigned char *Data_;
	size_t DataLen_;
};

bool ts_parse::parse_one_packet(const unsigned char *buf, int bufLen, ts_t *ts)
{ 
	try
	{
		int i_skip = 4, adap_DataLen = 0, okIDR = 0, okSPS = 0, okPPS = 0;
		if (bufLen<m_nTSPacketSize||m_nTSPacketSize<TS_PACKET_SIZE_MIN)
			return false;

		SafeData Data(buf, bufLen);
		init(ts);
		if(!parse_packet_header(buf, 4, &ts->ts_header))
			return false;

		if(ts->ts_header.PID ==PAT_PID||ts->ts_header.PID == CAT_PID
			||ts->ts_header.PID == NIT_PID||ts->ts_header.PID == SDT_PID
			|| ts->ts_header.PID == EIT_PID||ts->ts_header.PID == RST_PID 
			|| ts->ts_header.PID == TDT_PID)
			return false;

		if(m_nTSVidId>0&&(ts->ts_header.PID!=(m_nTSVidId&PID_BITS)))
		{
			return false;
		}

		if ( 0 == ( ts->ts_header.adaptation_field_control & 0x1 ) )	// last bit is 0 means there is no useful info.
		{
			return false;
		}

		if (ts->ts_header.adaptation_field_control==3) 
		{
			adap_DataLen = Data[i_skip];
			//if (adap_DataLen>0) {
			//		if(Data[5] & 0x10) {
			//			typedef unsigned long long pcr_t;
			//			pcr_t pcr  = (pcr_t)Data[6] << 25;
			//			pcr |= (pcr_t)Data[7] << 17;
			//			pcr |= (pcr_t)Data[8] << 9;
			//			pcr |= (pcr_t)Data[9] << 1;
			//			pcr |= (pcr_t)Data[10] >> 7 & 0x01;
			//			ts->ts_header.pcr= pcr;
			//		}
			//	}
			i_skip += adap_DataLen;
		}

		if(i_skip>=m_nTSPacketSize-4)
			return false;

		pes_t *pes = &(ts->pes);
		if (( 1 == ( ts->ts_header.adaptation_field_control & 0x1 ) ) // last bit is 1 means useful info do exist.
			&& (ts->ts_header.payload_unit_start_indicator)) 
		{
			i_skip += 3;
			if (Data[i_skip] == 1)
				i_skip++;

			pes->stream_id = Data[i_skip++];
			if (!IS_PES_STREAM_SUPPORTED(pes->stream_id))
			{
				return false;
			}

			if(m_nTSVidId==0&&(pes->stream_id)>>4==0xe)
			{
				m_nTSVidId = ts->ts_header.PID;
			}

			if(i_skip>=m_nTSPacketSize-5)
				return false;

			pes->PESplen = (Data[i_skip] << 8) + Data[i_skip+1];
			i_skip += 3;
			pes->PDflags = (int(Data[i_skip++] & 0xC0) >> 6);
			pes->PEShlen = Data[i_skip++];

			if (pes->PDflags == 1)//invalid can't be only pds
			{
				return false;
			}
			else if (pes->PDflags == 2) // bit"10"
			{
				if(i_skip>=m_nTSPacketSize-5)
					return false;
				ts_decode_pts_dts(&Data[i_skip], &pes->pts);
				i_skip += 5;
			}
			else if (pes->PDflags == 3) // "bit11"
			{
				if(i_skip>=m_nTSPacketSize-10)
					return false;

				ts_decode_pts_dts(&Data[i_skip], &pes->pts);
				i_skip += 5;
				ts_decode_pts_dts(&Data[i_skip], &pes->dts);
				i_skip += 5;
			}
			//if (pes->ESCR_flag) {
			//	uint64_t ESCR_base;
			//	uint32_t ESCR_extn;
			//	ESCR_base = (data[dpos+4] >>  3) |
			//		(data[dpos+3] <<  5) |
			//		(data[dpos+2] << 13) |
			//		(data[dpos+1] << 20) |
			//		((((uint64_t)data[dpos]) & 0x03) << 28) |
			//		((((uint64_t)data[dpos]) & 0x38) << 27);
			//	ESCR_extn = (data[dpos+5] >> 1) | (data[dpos+4] << 7);
			//	pes->ESCR = ESCR_base * 300 + ESCR_extn;
			//	dpos += 6;
			//}
		}

		unsigned int strid=0;
		for (int i = i_skip; i < bufLen - 4; i++) 
		{
			strid = (strid << 8) |Data[i];
			if ( ( strid >> 8 ) == 1 )
			{
				// we found a start code - remove the ref_idc from the nal type
				unsigned char nal_type = strid & 0x1f;
				if (nal_type == 5)
					ts->IDR = true;
				else if (nal_type == 7)
					ts->SPS = true;
				else if (nal_type == 8)
					ts->PPS = true;
				//else if (nal_type == 9)
				//	ts->AUD = true;
				break;
			}
			//if (!Data[i] && !Data[i+1] && !Data[i+2] && (Data[i+3] == unsigned char(1))) 
			//{
			//	int nalType=(Data[i+4]&0x1f);
			//	if (nalType == 5)
			//		ts->IDR = true;
			//	else if (nalType == 7)
			//		ts->SPS = true;
			//	else if (nalType == 8)
			//		ts->PPS = true;
			//	//else if (nalType == 9)
			//	//	ts->AUD = true;
			//	break;
			//}
		}
		return true;
	}
	catch (...)
	{
	}
	return false;
}

int ts_parse::check(const unsigned char *Data, int DataLen)
{
	int offset=sync(Data, DataLen);
	if (offset<0)
		return -1;
	Data+=offset;

	if(Data[0]!=0x47)
		return -1;

	if(DataLen<m_nTSPacketSize)
		return -3;

	if(m_nTSPacketSize<=0)
		return -4;
	return offset;
}

//函数名称：exist_keyframe(const unsigned char *Data, int DataLen, ts_t *ts)
//函数功能：长度为DataLen的数据Data中是否含有IDR信息
//输入参数：TS流数据unsigned char *Data, 数据长度DataLen。
//输入参数要求:必须sync同步后的数据
//返回值：-1 0x47字节同步错误，-3, 数据长度不足一个TS包，-4, 前面的sync调用错误或者没有调用sync函数，
//		-2，不存在所要查找的信息，>=0检测数据在当前buf中的第几个TS 包中
int ts_parse::exist_keyframe(const unsigned char *Data, int DataLen, ts_t *ts, const int* streamID)
{
	int offset=check(Data, DataLen);
	if (offset<0)
		return offset;
	
	Data+=offset;
	DataLen-=offset;

	for(int iPacket = 0 ; (DataLen -(m_nTSPacketSize*iPacket))>= m_nTSPacketSize; iPacket++)
	{
		if(parse_one_packet(Data+iPacket*m_nTSPacketSize, m_nTSPacketSize, ts)
			&&(ts->IDR||ts->AUD)//iframe
			&&(!streamID||*streamID==ts->pes.stream_id)
			)
			return iPacket;
	}
	return -2;
}

int ts_parse::get_pts(const unsigned char *Data, int DataLen, ts_t *ts, const int* streamID)
{
	ts->pes.pts=NOPTS_VALUE;

	int offset=check(Data, DataLen);
	if (offset<0)
		return offset;

	Data+=offset;
	DataLen-=offset;

	for(int iPacket = 0 ; (DataLen -(m_nTSPacketSize*iPacket))>= m_nTSPacketSize; iPacket++)
	{
		if(parse_one_packet(Data+iPacket*m_nTSPacketSize, m_nTSPacketSize, ts)
			&&ts->pes.pts!=NOPTS_VALUE
			&&(!streamID||*streamID==ts->pes.stream_id)
			)
		{
			return iPacket;
		}
	}
	return -2;
}


int ts_parse::get_dts(const unsigned char *Data, int DataLen, ts_t *ts, const int* streamID)
{
	ts->pes.dts=NOPTS_VALUE;

	int offset=check(Data, DataLen);
	if (offset<0)
		return offset;

	Data+=offset;
	DataLen-=offset;

	for(int iPacket = 0;(DataLen -(m_nTSPacketSize*iPacket))>= m_nTSPacketSize; iPacket++)
	{
		if(parse_one_packet(Data+iPacket*m_nTSPacketSize, m_nTSPacketSize, ts)
			&&ts->pes.dts!=NOPTS_VALUE
			&&(!streamID||*streamID==ts->pes.stream_id)
			)
		{
			return iPacket;
		}
	}
	return -2;
}

//
//int ts_parse::get_pcr(const unsigned char *Data, int DataLen, ts_t *ts)
//{
//	ts->ts_header.pcr=NOPTS_VALUE;
//
//	int offset=check(Data, DataLen);
//	if (offset<0)
//		return offset;
//	Data+=offset;
//
//	int iPacket = 0;
//
//	for( ; (DataLen -(m_nTSPacketSize*iPacket))>= m_nTSPacketSize; iPacket++)
//	{
//		if(parse_one_packet(Data+iPacket*m_nTSPacketSize, m_nTSPacketSize, ts)
//			&&ts->ts_header.pcr!=NOPTS_VALUE)
//		{
//			return iPacket;
//		}
//	}
//	return -2;
//}

//函数名称：exist_pat(const unsigned char *Data, int DataLen)
//函数功能：长度为DataLen的数据Data中是否含有PAT表
//输入参数：TS流数据unsigned char *Data, 数据长度DataLen。
//输入参数要求:必须sync同步后的数据
//返回值：-1 0x47字节同步错误，-3, 数据长度不足一个TS包，-4, 前面的sync调用错误或者没有调用sync函数，
//		-2，不存在所要查找的信息， >=0检测数据在当前buf中的第几个TS 包中
int ts_parse::exist_pat(const unsigned char *Data, int DataLen)
{
	int offset=check(Data, DataLen);
	if (offset<0)
		return offset;

	Data+=offset;
	DataLen-=offset;
	
	ts_header_t TSHeader;
	for(int iPacket = 0;(DataLen -(m_nTSPacketSize*iPacket))>= m_nTSPacketSize; iPacket++)
	{
		if(parse_packet_header(Data+iPacket*m_nTSPacketSize, m_nTSPacketSize, &TSHeader)
			&&TSHeader.PID ==PAT_PID
			)
			return iPacket;
	}
	return -2;
}