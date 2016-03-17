#ifndef TS_PARSE_H
#define TS_PARSE_H

#include <cstdio>

#define  NOPTS_VALUE 0x8000000000000000LL 

struct ts_header_t 
{
	unsigned int  sync_byte                    : 8;
	unsigned int  transport_error_indicator    : 1;
	unsigned int  payload_unit_start_indicator : 1;
	unsigned int  transport_priority           : 1;
	unsigned int  PID                          : 13;
	unsigned int  transport_scrambling_control : 2;
	unsigned int  adaptation_field_control     : 2;
	unsigned int  continuity_counter           : 4;
};

struct pes_t 
{
	long packet_start_code_prefix;
	unsigned char stream_id;
	unsigned int PESplen;
	unsigned char PEShlen;
	unsigned char PDflags;
	long long  pts;
	long long  dts;
} ;

struct ts_t{
	ts_header_t ts_header;
	pes_t pes;
	bool IDR;
	bool SPS;
	bool PPS;
	bool AUD;
} ;

#define PID_BITS				0x1fff	// pid width : 13
#define PAT_PID                 ( 0x0000 & PID_BITS )
#define CAT_PID					( 0x0001 & PID_BITS )
#define NIT_PID					( 0x0010 & PID_BITS )
#define SDT_PID                 ( 0x0011 & PID_BITS )
#define EIT_PID					( 0x0012 & PID_BITS )
#define RST_PID					( 0x0013 & PID_BITS )
#define TDT_PID					( 0x0014 & PID_BITS )



// PMT stream types
enum ts_stream_type {
	STREAM_TYPE_MPEG1_VIDEO			= 0x01, // MPEG-1 video
	STREAM_TYPE_MPEG2_VIDEO			= 0x02, 	// H.262 - MPEG-2 video

	STREAM_TYPE_MPEG1_AUDIO			= 0x03, // MPEG-1 audio
	STREAM_TYPE_MPEG2_AUDIO			= 0x04, // MPEG-2 audio

	STREAM_TYPE_ADTS_AUDIO			= 0x0F, 	// AAC ADTS
	STREAM_TYPE_MPEG4_PART2_VIDEO	= 0x10, // DIVX - MPEG-4 part 2

	STREAM_TYPE_AVC_VIDEO			= 0x1B, 	// H.264 - MPEG-4 part 10
	STREAM_TYPE_AVS_VIDEO			= 0x42, 	// Chinese AVS

	STREAM_TYPE_DOLBY_DVB_AUDIO		= 0x06, // 0x06 - Private stream, look at stream descriptors for AC-3 descriptor
	STREAM_TYPE_DOLBY_ATSC_AUDIO	= 0x81, // 0x81 - Private stream in ATSC (US system, probably we shouldn't care)
};

// ------------------------------------------------------------
// PES packet stream ids
// See H.222.0 Table 2-17 and Table 2-18
#define STREAM_ID_PROGRAM_STREAM_MAP		0xbc
#define STREAM_ID_PRIVATE_STREAM_1			0xbd
#define STREAM_ID_PADDING_STREAM			0xbe
#define STREAM_ID_PRIVATE_STREAM_2			0xbf
#define STREAM_ID_ECM_STREAM				0xf0
#define STREAM_ID_EMM_STREAM				0xf1
#define STREAM_ID_DSMCC_STREAM				0xf2
#define STREAM_ID_13522_STREAM				0xf3
#define STREAM_ID_H222_A_STREAM				0xf4
#define STREAM_ID_H222_B_STREAM				0xf5
#define STREAM_ID_H222_C_STREAM				0xf6
#define STREAM_ID_H222_D_STREAM				0xf7
#define STREAM_ID_H222_E_STREAM				0xf8
#define STREAM_ID_ANCILLARY_STREAM			0xf9
#define STREAM_ID_PROGRAM_STREAM_DIRECTORY	0xff

#define IS_AUDIO_STREAM_ID(id)				((id) >= 0xc0 && (id) <= 0xdf)
#define IS_VIDEO_STREAM_ID(id)				((id) >= 0xe0 && (id) <= 0xef)
#define IS_PES_STREAM_SUPPORTED(id)			(!(id == STREAM_ID_PROGRAM_STREAM_MAP       || \
	id == STREAM_ID_PADDING_STREAM           || \
	id == STREAM_ID_PRIVATE_STREAM_2         || \
	id == STREAM_ID_ECM_STREAM               || \
	id == STREAM_ID_EMM_STREAM               || \
	id == STREAM_ID_PROGRAM_STREAM_DIRECTORY || \
	id == STREAM_ID_DSMCC_STREAM             || \
	id == STREAM_ID_H222_E_STREAM))

class ts_parse
{
public:
	ts_parse():m_nTSPacketSize(0), m_nTSVidId(0){};
	~ts_parse(){};
public:
	//0x47数据同步，
	int  sync (const unsigned char *Data, int DataLen);

	int  packet_size();

	int  exist_pat(const unsigned char *Data, int DataLen);

	int  exist_keyframe(const unsigned char *Data, int DataLen, ts_t *ts, const int* streamID=NULL);

	int get_pts(const unsigned char *Data, int DataLen, ts_t *ts, const int* streamID=NULL);
	int get_dts(const unsigned char *Data, int DataLen, ts_t *ts, const int* streamID=NULL);
	//int get_pcr(const unsigned char *Data, int DataLen, ts_t *ts);

	void reset();

private:
	bool parse_packet_header(const unsigned char *Data, int DataLen, ts_header_t* TSHeader);

	bool parse_one_packet(const unsigned char *Data, int DataLen, ts_t *ts);

	int check(const unsigned char *Data, int DataLen);
private:

	int m_nTSPacketSize;
	int m_nTSVidId;
};

#endif
