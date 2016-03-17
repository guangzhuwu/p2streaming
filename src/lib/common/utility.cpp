#include "common/utility.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <p2engine/push_warning_option.hpp>
#include <boost/tokenizer.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/regex.hpp>
#include <boost/lexical_cast.hpp>
#include <p2engine/pop_warning_option.hpp>
#include "asfio/os_api.hpp"
#include "common/curve25519.h"
#include "common/md5.h"
#include "common/const_define.h"


NAMESPACE_BEGIN(p2common);

/*
#if defined(_WIN32) || defined (_WIN64)
#include<intrin.h>
#endif

#if defined (__GNUC__)
#define __cpuid(out, infoType)\
asm volatile("cpuid": "=a" (out[0]), "=b" (out[1]), "=c" (out[2]), "=d" (out[3]): "a" (infoType));
#define __cpuidex(out, infoType, ecx)\
asm volatile("cpuid": "=a" (out[0]), "=b" (out[1]), "=c" (out[2]), "=d" (out[3]): "a" (infoType), "c" (ecx));
#endif
std::string get_cpuid_string()
{
enum CPUIDInfoType
{
String = 0, FeatureSupport = 1
};
int info[4];
__cpuid(info, String);
char* p=(char*)info;
return std::string(p+4, p+sizeof(info));
}
*/
endpoint external_udp_endpoint(const peer_info& info)
{
	boost::asio::ip::address_v4 addr = (info.has_external_ip() ?
		boost::asio::ip::address_v4(info.external_ip()) : boost::asio::ip::address_v4()
		);
	int port = (info.has_external_udp_port() ? info.external_udp_port() : 0);
	return endpoint(boost::asio::ip::address(addr), port);
}
endpoint external_tcp_endpoint(const peer_info& info)
{
	boost::asio::ip::address_v4 addr = (info.has_external_ip() ?
		boost::asio::ip::address_v4(info.external_ip()) : boost::asio::ip::address_v4()
		);
	int port = (info.has_external_tcp_port() ? info.external_tcp_port() : 0);
	return endpoint(boost::asio::ip::address(addr), port);
}

endpoint internal_udp_endpoint(const peer_info& info)
{
	boost::asio::ip::address_v4 addr = (info.has_internal_ip() ?
		boost::asio::ip::address_v4(info.internal_ip()) : boost::asio::ip::address_v4()
		);
	int port = (info.has_internal_udp_port() ? info.internal_udp_port() : 0);
	return endpoint(boost::asio::ip::address(addr), port);
}
endpoint internal_tcp_endpoint(const peer_info& info)
{
	boost::asio::ip::address_v4 addr = (info.has_internal_ip() ?
		boost::asio::ip::address_v4(info.internal_ip()) : boost::asio::ip::address_v4()
		);
	int port = (info.has_internal_tcp_port() ? info.internal_tcp_port() : 0);
	return endpoint(boost::asio::ip::address(addr), port);
}

std::string string_to_hex(const std::string&str)
{
	static const char* dict = "0123456789abcdef";

	std::string rst;
	rst.resize(str.length() << 1);
	for (size_t i = 0, j = 0; i < str.length(); i++, j += 2)
	{
		int c1 = ((int(str[i]) & 0xf0) >> 4);
		int c2 = (int(str[i]) & 0x0f);
		rst[j] = dict[c1];
		rst[j + 1] = dict[c2];
	}
	return rst;
}

std::string hex_to_string(const std::string&str)
{
	struct is_space
	{
		inline bool operator()(char c)const
		{
			const static char* ws = " \t\n\r\f\v";
			return std::strchr(ws, c) != 0;
		}
	};
	std::size_t strLen = str.size();
	std::string rst;
	rst.reserve(strLen >> 1);
	char c = '\0';
	int flag = 0;
	for (size_t i = 0; i < strLen; i++)
	{
		char c1 = str[i];
		if (c1 >= '0'&&c1 <= '9')
			c = (c << 4) | (0x0f & (c1 - '0'));
		else if (c1 >= 'a'&&c1 <= 'f')
			c = (c << 4) | (0x0f & (10 + (c1 - 'a')));
		else if (c1 >= 'A'&&c1 <= 'F')
			c = (c << 4) | (0x0f & (10 + (c1 - 'A')));
		else if (is_space()(c1))
			continue;
		else
			return rst;
		if (++flag == 2)
		{
			flag = 0;
			rst += c;
		}
	}
	return rst;
}
//////////////////////////////////////////////////////////////////////////
//登录加密认证相关
std::string generate_shared_key(std::string digest, const std::string &hispublic)
{
	char out[32];

	const char basepoint[32] = { 9 };

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

void fast_xor(void* io, int ioLen, const void* xorStr, int xorStrLen)
{
	boost::int8_t* char_io = (boost::int8_t*)io;
	boost::int8_t* char_xor = (boost::int8_t*)xorStr;
	boost::int8_t* char_io_end = char_io + ioLen;
	boost::int8_t* char_xor_end = char_xor + xorStrLen;
	if (xorStrLen >= 4 && xorStrLen % 4 == 0)
	{
		boost::int32_t* int_io = (boost::int32_t*)io;
		boost::int32_t* int_xor = (boost::int32_t*)xorStr;
		if (ioLen >= 4
			&& (uintptr_t)int_io == (uintptr_t)char_io
			&& (uintptr_t)int_xor == (uintptr_t)char_xor
			)
		{
			int ioIntLen = ioLen / sizeof(boost::int32_t);
			int xorStrIntLen = xorStrLen / sizeof(boost::int32_t);
			int i = 0, j = 0;
			for (; i < ioIntLen; ++i)
			{
				int_io[i] ^= int_xor[j++];
				if (j >= xorStrIntLen)
					j = 0;
			}
			char_io = (boost::int8_t*)(&int_io[i]);
			char_xor = (boost::int8_t*)(&int_xor[i%xorStrIntLen]);
		}
	}
	for (; char_io < char_io_end;)
	{
		if (char_xor >= char_xor_end)
			char_xor = (boost::int8_t*)xorStr;
		*char_io++ ^= *char_xor++;
	}
}

static const boost::uint8_t BMAP[] = { 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01 };
bool is_bit(const void*p, int bit)
{
	int Bn = (bit >> 3);
	int bn = bit & 0x07;//bit%8
	const boost::uint8_t* pchar = (const boost::uint8_t*)p;
	return (pchar[Bn] & BMAP[bn]) != boost::uint8_t(0);
}
void set_bit(void*p, int bit, bool v)
{
	int Bn = (bit >> 3);
	int bn = bit & 0x07;//bit%8
	boost::uint8_t* pchar = (boost::uint8_t*)p;
	if (v)
		pchar[Bn] |= BMAP[bn];
	else
		pchar[Bn] &= ~BMAP[bn];
}

std::string md5(const std::string&str)
{
	md5_byte_t digest[16];
	md5_state_t pms;
	md5_init(&pms);
	md5_append(&pms, (const md5_byte_t *)str.c_str(), str.length());
	md5_finish(&pms, digest);
	return std::string((char*)digest, 16);
}

std::string md5(const char* str, size_t len)
{
	md5_byte_t digest[16];
	md5_state_t pms;
	md5_init(&pms);
	md5_append(&pms, (const md5_byte_t *)str, len);
	md5_finish(&pms, digest);
	return std::string((char*)digest, 16);
}

static const std::string base64_chars =
"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
"abcdefghijklmnopqrstuvwxyz"
"0123456789+/";
std::string base64encode(const std::string& s)
{
	unsigned char inbuf[3];
	unsigned char outbuf[4];

	std::string ret;
	for (std::string::const_iterator i = s.begin(); i != s.end();)
	{
		// available input is 1, 2 or 3 bytes
		// since we read 3 bytes at a time at most
		int available_input = (std::min)(3, int(s.end() - i));

		// clear input buffer
		std::fill(inbuf, inbuf + 3, 0);

		// read a chunk of input into inbuf
		std::copy(i, i + available_input, inbuf);
		i += available_input;

		// encode inbuf to outbuf
		outbuf[0] = (inbuf[0] & 0xfc) >> 2;
		outbuf[1] = ((inbuf[0] & 0x03) << 4) | ((inbuf[1] & 0xf0) >> 4);
		outbuf[2] = ((inbuf[1] & 0x0f) << 2) | ((inbuf[2] & 0xc0) >> 6);
		outbuf[3] = inbuf[2] & 0x3f;

		// write output
		for (int j = 0; j < available_input + 1; ++j)
		{
			ret += base64_chars[outbuf[j]];
		}

		// write pad
		for (int j = 0; j < 3 - available_input; ++j)
		{
			ret += '=';
		}
	}
	return ret;
}

static inline bool is_base64(unsigned char c) {
	return (isalnum(c) || (c == '+') || (c == '/'));
}

std::string base64decode(std::string const& s)
{
	int in_len = s.size();
	int i = 0;
	int j = 0;
	int in_ = 0;
	unsigned char outbuf[4], inbuf[3];
	std::string ret;

	while (in_len--
		&& (s[in_] != '=')
		&& is_base64(s[in_])
		)
	{
		outbuf[i++] = s[in_]; in_++;
		if (i == 4)
		{
			for (i = 0; i < 4; i++)
				outbuf[i] = base64_chars.find(outbuf[i]);

			inbuf[0] = (outbuf[0] << 2) + ((outbuf[1] & 0x30) >> 4);
			inbuf[1] = ((outbuf[1] & 0xf) << 4) + ((outbuf[2] & 0x3c) >> 2);
			inbuf[2] = ((outbuf[2] & 0x3) << 6) + outbuf[3];

			for (i = 0; (i < 3); i++)
				ret += inbuf[i];
			i = 0;
		}
	}

	if (i)
	{
		for (j = i; j < 4; j++)
			outbuf[j] = 0;

		for (j = 0; j < 4; j++)
			outbuf[j] = base64_chars.find(outbuf[j]);

		inbuf[0] = (outbuf[0] << 2) + ((outbuf[1] & 0x30) >> 4);
		inbuf[1] = ((outbuf[1] & 0xf) << 4) + ((outbuf[2] & 0x3c) >> 2);
		inbuf[2] = ((outbuf[2] & 0x3) << 6) + outbuf[3];

		for (j = 0; (j < i - 1); j++) ret += inbuf[j];
	}

	return ret;
}

void search_files(const boost::filesystem::path& root_path,
	const std::string& strExt,
	std::vector<boost::filesystem::path>& files)
{
	namespace fs = boost::filesystem;

	if (!fs::exists(root_path))
		return;

	fs::directory_iterator itr_begin(root_path);
	fs::directory_iterator itr_end;
	for (; itr_begin != itr_end; ++itr_begin)
	{
		if (fs::is_directory(*itr_begin))
		{
			search_files((*itr_begin).path(), strExt, files);
		}
		else
		{
			//判断后缀strExt，用“，”分割
			std::list<std::string> str_exts;
			boost::tokenizer<> tok(strExt);
			for (boost::tokenizer<>::iterator beg = tok.begin(); beg != tok.end(); ++beg)
			{
				str_exts.push_back("." + *beg);
			}

			fs::path full_path((*itr_begin).path().string());
			fs::path src_ext = full_path.extension();

			BOOST_AUTO(itr_ext, find(str_exts.begin(), str_exts.end(),
				boost::lexical_cast<std::string>(src_ext.string()))
				);

			BOOST_AUTO(itr_all, find(str_exts.begin(), str_exts.end(), ".*")
				);

			if (itr_ext != str_exts.end() || itr_all != str_exts.end())
				files.push_back((*itr_begin).path());
		}
	}
}

int get_duration(const boost::filesystem::path& pszfile)
{
	boost::optional<int> bitRate;
	return get_duration(pszfile, bitRate);
}

int get_duration(const boost::filesystem::path& pszfile, boost::optional<int>& bitRate)
{
#ifdef _WIN32
#	define popen _popen
#	define pclose _pclose
#endif

	struct parse{
		bool operator ()(const std::string& content, const char& sep,
			const std::string& Key, std::string& Value)const
		{
			const char* p = strstr(content.c_str(), Key.c_str());
			if (p)
			{
				p += Key.size();
				const char* pe = strchr(p, sep);
				if (pe)
					Value.assign(p, pe);
				else
					Value = p;
				return true;
			}
			return false;
		}
	};

	std::string content;
	std::string cmd = "ffmpeg -i \"" + pszfile.string() + "\" 2>&1";
	FILE *pf = popen(cmd.c_str(), "r");
	if (pf)
	{
		char buf[2048];
		int len = fread(buf, 1, sizeof(buf), pf);
		if (len > 0)
		{
			content.assign(buf, len);
		}
		pclose(pf);
	}

	int durationValue = -1;
	//解析时长
	std::string Durationkey = "Duration: ";
	std::string Duration;
	if (parse()(content, ', ', Durationkey, Duration)
		&& boost::posix_time::duration_from_string(Duration).total_milliseconds() != 0)
	{
		durationValue = (int)boost::posix_time::duration_from_string(Duration).total_milliseconds();
	}

	//解析bitrate
	std::string BitrateKey = "bitrate: ";
	std::string BitRate;
	if (parse()(content, ' ', BitrateKey, BitRate))
	{
		//根据码率计算
		int64_t len = get_length(pszfile);
		try{
			int kbps = boost::lexical_cast<int>(BitRate);
			if (kbps)
			{
				bitRate = boost::optional<int>(kbps);
				if (-1 == durationValue)
					durationValue = static_cast<int>((len * 1000 / 8) / (kbps*15.65));
			}
		}
		catch (...){}
	}

	return durationValue;
}

int64_t get_length(const boost::filesystem::path& pszfile)
{
	try
	{
		return boost::filesystem::file_size(pszfile);
	}
	catch (...)
	{
		return -1;
	}
}

std::string get_file_md5(const boost::filesystem::path& pszfile)
{
	error_code ec;
	FILE* fp = asfio::fileopen(pszfile, "rb", ec);
	if (!fp)
		return "";

	char digest[16];
	md5_state_t pms;
	md5_init(&pms);

	int len = 0;
	char buf[8192];
	while ((len = fread(buf, 1, sizeof(buf), fp)) > 0)
	{
		md5_append(&pms, (md5_byte_t*)&buf[0], len);
	}
	fclose(fp);

	md5_finish(&pms, (md5_byte_t*)digest);
	return std::string(digest, digest + 16);
}

#ifdef WIN32
int get_sys_cpu_usage(timestamp_t)
{
	return 0;
}
int bogo_mips()
{
	return 4096;// 假设是PC ，如果是WINCE嵌入式系统呢？？？
}
#else
int bogo_mips(){
	static double bogomips=0;
	if (bogomips)
		return bogomips;

	char line[512]={0};
	FILE *fp = fopen("/proc/cpuinfo", "r");
	if( fp == NULL ){
		perror("can't open /proc/cpuinfo");
		return -1;
	}
	while( fgets(line, sizeof(line)-1, fp) ){
		//转化为小写
		for (int i=0;line[i];++i)
			line[i]=::tolower(line[i]);

		char* bogo=strstr(line, "bogomips");
		if(bogo){
			/* sscanf should match any amount of whitespace around the : */
			int nscan = sscanf(bogo, "%*[^1-9]%lf", &bogomips);
			if( nscan == 1 ){
				fclose(fp);
				return (int)bogomips;
			}else{
				fclose(fp);
				return -1;
			}
		}
	}
	fclose(fp);
	return -1;
}
//
//static  void __delay(volatile long d0)
//{
//	for(;--d0;);
//}
//int bogo_mips()
//{
//	static int mips=0;
//	if (mips)
//		return mips;
//	
//	int64_t elapse;
//	int64_t time_precision=0;
//	long loops_per_sec = (1<<12);
//	while (loops_per_sec <<= 1) {
//		/* wait for "start of" clock tick */
//		int64_t t1 = system_time::precise_tick_count();
//		while (t1 == system_time::precise_tick_count())
//			/* nothing */;
//		/* Go .. */
//		int64_t t2 = system_time::precise_tick_count();
//		__delay(loops_per_sec);
//		elapse = system_time::precise_tick_count() - t2;
//		if (time_precision==0)
//			time_precision=(t2-t1);
//		else
//			time_precision=((t2-t1)+time_precision)/2;
//		if (elapse>std::max(50LL, 20*time_precision))
//			break;
//	}
//
//	mips=(int)(loops_per_sec*1000LL/(elapse+time_precision/2)/100000);
//	std::cout<<"TimePrecision="<<time_precision<<", elapse="<<elapse
//		<<", MIPS "<<(loops_per_sec*1000LL/(elapse+time_precision/2)/100000)<<"\n";
//	return mips;
//}

int get_sys_cpu_usage (timestamp_t now){
	/*                 
	CPU在t1到t2时间段总的使用时间 = ( user2+ nice2+ system2+ idle2+ iowait2+ irq2+ softirq2) 
	-( user1+ nice1+ system1+ idle1+ iowait1+ irq1+ softirq1)
	CPU在t1到t2时间段空闲使用时间 = (idle2 - idle1)
	CPU在t1到t2时间段即时利用率 =  1 - CPU空闲使用时间 / CPU总的使用时间                                                    
	*/

	static timestamp_t last_call_time=int(timestamp_now()-0xffff);
	static int usage=0;

	if (!is_time_passed(usage<95?100:50, last_call_time, now))
		return usage;
	last_call_time=now;

	double usr=0, nice=0, system=0, idle=0, iowait=0, irq=0, softirq=0;
	FILE *fp=fopen("/proc/stat", "r");
	if (!fp) {
		perror("can't open /proc/stat");
		return -1;
	}
	fscanf(fp, "%*s %lf %lf %lf %lf %lf %lf %lf", &usr, &nice, &system, &idle, &iowait, &irq, &softirq);
	fclose(fp);

	/* calc CPU usage */
	static double pre_idle=0, pre_total=0;
	double total= usr + nice + system + idle + iowait + irq + softirq;
	if ((pre_total == 0) || !(total - pre_total > 0)) {
		usage = 0;
	} else {
		double u=1.0-(idle-pre_idle)/(total-pre_total);
		usage = (int)(u*100);
	}

	/* save current values for next calculation */
	pre_total = total;
	pre_idle = idle;

	//printf("%d\n", usage);
	return usage;
}
#endif

#if BOOST_FILESYSTEM_VERSION<=2
std::string filename(std::string const& f)
{
	if (f.empty()) return "";
	char const* first = f.c_str();
	char const* sep = strrchr(first, '/');
#ifdef BOOST_WINDOWS_API
	char const* altsep = strrchr(first, '\\');
	if (sep == 0 || altsep > sep) sep = altsep;
#endif
	if (sep == 0) return f;

	if (sep - first == int(f.size()) - 1)
	{
		// if the last character is a / (or \)
		// ignore it
		int len = 0;
		while (sep > first)
		{
			--sep;
			if (*sep == '/'
#ifdef BOOST_WINDOWS_API
				|| *sep == '\\'
#endif
				)
				return std::string(sep + 1, len);
			++len;
		}
		return std::string(first, len);

	}
	return std::string(sep + 1);
}

std::string filename(boost::filesystem::path const& f)
{
	return filename(f.string());
}
#else
std::string filename(std::string const& f)
{
	return boost::filesystem::path(f).filename().string();
}
std::string filename(boost::filesystem::path const& f)
{
	return f.filename().string();
}
#endif

std::size_t MurmurHash(const void * key, size_t len)
{
#define  mmix(h, k) {k *= m; k ^= k >> r; k *= m; h *= m; h ^= k;}
	const uint32_t m = 0x5bd1e995;
	const int r = 24;
	uint32_t l = (uint32_t)len;

	const unsigned char * data = (const unsigned char *)key;

	const uint32_t seed = 0x3FB0BB5F;
	uint32_t h = seed;

	while (len >= 4)
	{
		uint32_t k = *(uint32_t*)data;

		mmix(h, k);

		data += 4;
		len -= 4;
	}

	uint32_t t = 0;

	switch (len)
	{
	case 3: t ^= data[2] << 16;
	case 2: t ^= data[1] << 8;
	case 1: t ^= data[0];
	};

	mmix(h, t);
	mmix(h, l);

	h ^= h >> 13;
	h *= m;
	h ^= h >> 15;

	return h;
#undef mmix
}

std::string asc_time_string()
{
	time_t st = time(NULL);
	struct tm* lt = std::localtime(&st);
	return asctime(lt);
}

NAMESPACE_END(p2common);
