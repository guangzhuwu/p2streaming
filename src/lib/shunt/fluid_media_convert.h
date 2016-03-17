#ifndef _SHUNT_FLUID_TO_MEDIA_H__
#define _SHUNT_FLUID_TO_MEDIA_H__

#include "common/common.h"

namespace p2shunt{
	using namespace  p2engine;

	class fluid_to_media
	{
	public:
		fluid_to_media()
		{
			seqno_seed_=0;
		}
		void operator()(const asio::const_buffers_1& in, media_packet& out)
		{
			out.set_time_stamp(timestamp_now());
			out.set_seqno(seqno_seed_++);
			safe_buffer_io io(&out.buffer());
			io.write(asio::buffer_cast<const char*>(in), asio::buffer_size(in));
		}
		void operator()(const safe_buffer& in, media_packet& out)
		{
			out.set_time_stamp(timestamp_now());
			out.set_seqno(seqno_seed_++);
			safe_buffer_io io(&out.buffer());
			io.write(p2engine::buffer_cast<char*>(in), p2engine::buffer_size(in));
		}
	protected:
		seqno_t seqno_seed_;
	};

	class media_to_fluid
	{
	public:
		void operator()(const media_packet& in, safe_buffer& out)
		{
			safe_buffer_io io(&out);
			io.write(
				p2engine::buffer_cast<const char*>(in.buffer())+media_packet::format_size(), 
				in.buffer().length()-media_packet::format_size()
				);
		}
	};

}

#endif//_SHUNT_FLUID_TO_MEDIA_H__

