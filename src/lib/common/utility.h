//
// utility.h
// ~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2008 GuangZhu Wu (GuangZhuWu@gmail.com)
//
// All rights reserved. 
//

#ifndef common_utility_h__
#define common_utility_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include <p2engine/push_warning_option.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/tuple/tuple.hpp>
#include <p2engine/pop_warning_option.hpp>

#include "common/config.h"
#include "common/typedef.h"
#include "common/media_packet.h"
#include "common/message.pb.h"
#include "common/utility.h"
#include "common/parameter.h"


namespace p2common{

	using namespace p2message;

	template<typename Type>
	static Type& get_static_null_value()
	{
		static Type s_null_vale_;
		return s_null_vale_;
	}

	template<typename ProtoMsgType>
	safe_buffer serialize(message_t msgType, const ProtoMsgType&msgBody)
	{
		safe_buffer buf;
		safe_buffer_io io(&buf);
		int dataLen=msgBody.ByteSize();
		io.prepare(sizeof(msgType)+dataLen);
		io<<msgType;
		if (dataLen>0)
		{
			msgBody.SerializeWithCachedSizesToArray((unsigned char*)io.pptr());
			io.commit(dataLen);
			DEBUG_SCOPE(
				ProtoMsgType msg;
			BOOST_ASSERT(parser(buf.buffer_ref(sizeof(message_t)), msg));
			);
		}
		BOOST_ASSERT(sizeof(msgType)+dataLen==buf.length());
		return buf;
	}

	template<typename ProtoMsgType>
	safe_buffer serialize(const ProtoMsgType&msgBody)
	{
		safe_buffer buf;
		int len=msgBody.ByteSize();
		BOOST_ASSERT(len>0);
		if (len>0)
		{
			safe_buffer_io io(&buf);
			io.prepare(len);
			msgBody.SerializeWithCachedSizesToArray((unsigned char*)io.pptr());
			io.commit(len);
			DEBUG_SCOPE(
				ProtoMsgType msg;
			BOOST_ASSERT(parser(buf.buffer_ref(0), msg));
			);
		}
		return buf;
	}

	template<typename ProtoMsgType>
	bool parser(const safe_buffer& buf, message_t& msgType, ProtoMsgType& msgBody)
	{
		if (buf.size()<msgType)
			return false;
		safe_buffer _buf=buf;
		safe_buffer_io io(&_buf);
		io>>msgType;
		bool rst=msgBody.ParseFromArray(buffer_cast<void const*>(_buf), buffer_size(_buf));
		//BOOST_ASSERT(rst);//for debug
		return rst;
	}
	template<typename ProtoMsgType>
	inline bool parser(const safe_buffer& buf, ProtoMsgType&msgBody)
	{
		bool rst=msgBody.ParseFromArray(buffer_cast<void const*>(buf), buffer_size(buf));
		//BOOST_ASSERT(rst);//for debug
		return rst;
	}

	template<typename TheType>
	struct must_be_the_type 
	{
		must_be_the_type(TheType v)
			:v_(v){}

		operator TheType()const{return v_;}

	private:
		template<typename Type>
		operator Type()const;//不允许隐式转换为其他类型

		TheType v_;
	};
	inline must_be_the_type<timestamp_t> timestamp_now()
	{
		return (timestamp_t)system_time::tick_count();
	}
	inline ptime ptime_now()
	{
		return system_time::universal_time();
	}
	inline must_be_the_type<tick_type> tick_now()
	{
		return system_time::tick_count();
	}

	template<typename Duration>
	bool is_time_passed(const Duration& duration, const ptime& startTime, const ptime& now)
	{
		//BOOST_ASSERT(now>=startTime);
		return (startTime+duration)<=now;
	}
	template<typename Time>
	bool is_time_passed(int64_t duration, Time startTime, Time now)
	{
		//BOOST_ASSERT(wrappable_greater_equal<Time>()(now, startTime));
		return  wrappable_less_equal<Time>()(static_cast<Time>(startTime+duration), now);
	}

	template<typename Type>
	inline Type bound(Type lower, Type middle, Type upper) 
	{
		//BOOST_ASSERT(std::min(lower, upper)==lower);
		if (lower>upper)
			return std::min(std::max(upper, middle), lower);
		return std::min(std::max(lower, middle), upper);
	}
	template<typename Type, typename CmpFunc>
	inline Type bound(Type lower, Type middle, Type upper, CmpFunc func) 
	{
		if (!func(lower, upper))
			return std::min(std::max(upper, middle, func), lower, func);
		return std::min(std::max(lower, middle, func), upper, func);
	}

	endpoint external_udp_endpoint(const peer_info& info);
	endpoint external_tcp_endpoint(const peer_info& info);
	endpoint internal_udp_endpoint(const peer_info& info);
	endpoint internal_tcp_endpoint(const peer_info& info);

	template<typename ValueType, typename Sequence>
	inline ValueType& get_slot(std::vector<ValueType>&container, Sequence seq)
	{
		BOOST_STATIC_ASSERT(boost::is_unsigned<Sequence>::value);
		//最大值的槽位不能和0槽位相同, 否则，必须调整container的大小。
		BOOST_ASSERT(int64_t((std::numeric_limits<Sequence>::max)())%int64_t(container.size())!=0);
		return container[seq%container.size()];
	}
	template<typename ValueType, typename Sequence>
	inline const ValueType& get_slot(const std::vector<ValueType>&container, Sequence seq)
	{
		BOOST_STATIC_ASSERT(boost::is_unsigned<Sequence>::value);
		//最大值的槽位不能和0槽位相同, 否则，必须调整container的大小。
		BOOST_ASSERT(int64_t((std::numeric_limits<Sequence>::max)())%int64_t(container.size())!=0);
		return container[seq%container.size()];
	}

	//std::string get_cpuid_string();

	std::string string_to_hex(const std::string&str);
	std::string hex_to_string(const std::string&str);

	bool is_bit(const void*p, int bit);
	void set_bit(void*p, int bit, bool v);

	std::string md5(const std::string&str);
	std::string md5(const char* str, size_t len);

	std::string base64encode(std::string const& s);
	std::string base64decode(std::string const& s);

	void search_files(const boost::filesystem::path& root_folder, 
		const std::string& strExt, 
		std::vector<boost::filesystem::path>& files);

	int64_t get_length(const boost::filesystem::path&);
	int get_duration(const boost::filesystem::path&);
	int get_duration(const boost::filesystem::path&, boost::optional<int>& bitRate);
	std::string get_file_md5(const boost::filesystem::path&);

	void fast_xor(void* io, int ioLen, const void* xorStr, int xorStrLen);

	int get_sys_cpu_usage(timestamp_t now);
	int bogo_mips();

	std::string filename(std::string const& f);
	std::string filename(boost::filesystem::path const& f);

	//字符串哈希算法，参考 http://murmurhash.googlepages.com/MurmurHash2.cpp
	std::size_t MurmurHash(const void * key, size_t len);
	template<typename StringType>
	struct string_hasher
	{
		std::size_t operator()(const StringType&s)const
		{
			return MurmurHash(&s[0], s.size());
		}
	};

	std::string asc_time_string();
}

#endif // common_utility_h__


