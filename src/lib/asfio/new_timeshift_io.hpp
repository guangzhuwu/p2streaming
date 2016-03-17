#ifndef __new_timeshift_io_hpp__
#define __new_timeshift_io_hpp__

NAMESPACE_BEGIN(asfio)


class media_file;

class timeshiht_channel
	: public p2engine::basic_engine_object
{
public:
	typedef timeshiht_channel this_type;

	typedef p2common::seqno_t seqno_t;
	typedef p2engine::safe_buffer safe_buffer;
	
	typedef boost::filesystem::path path;
	typedef boost::system::error_code error_code;
	typedef boost::uint64_t uint64_t;
	typedef boost::uint32_t uint32_t;
	typedef std::map<uint32_t, boost::shared_ptr<media_file> > media_file_set_type;

	typedef boost::function< void (const error_code&) >	open_handler_type;
	typedef boost::function<void(const error_code&, seqno_t)>	write_handler_type;

	friend class media_file;

public:
	timeshiht_channel( io_service& ios);
	timeshiht_channel( io_service& ios, path file_path, const std::string& channel_id, size_t file_size, int file_num);

	static boost::shared_ptr<this_type> create(io_service& ios)
	{
		return boost::shared_ptr<this_type>(new this_type(ios), p2engine::shared_access_destroy<this_type>());
	}

	template<typename OpenHandler>
	void channel_open(const path& name, const path& timeshift_dir, const std::string& channel_id, uint64_t max_file_num, 
		uint64_t per_file_len, const OpenHandler& handler)
	{
		if(channel_open_handler.empty())
		{
			channel_open_handler=boost::bind(&this_type::channel_open_callback<OpenHandler>, this, handler, _1);
		}
		__channel_open(name, timeshift_dir, channel_id, max_file_num, per_file_len);
	}

	template<typename WriteHandler>
	void channel_write_piece(seqno_t seqno, const safe_buffer& buf, const WriteHandler& handler)
	{
		if(write_piece_handler.empty())//假设都用一样的回调句柄
		{
			write_piece_handler=boost::bind(&this_type::channel_write_piece_callback<WriteHandler>, this, handler, _1, _2);
		}
		__channel_write_piece(seqno, buf);
	}

	void channel_close()
	{
		__channel_close();
	}

	template<typename OpenHandler>
	void channel_open_callback( OpenHandler& handler, const error_code& ec)
	{
		handler(ec);
	}

	template<typename WriteHandler>
	void channel_write_piece_callback( WriteHandler& handler, const error_code& ec, seqno_t seqno)
	{
		handler(ec, seqno);
	}

	void reset_channel_limit(uint32_t file_size);

private:
	void __channel_open(const path& channel_name, const path& timeshift_dir, const std::string& channel_id, uint64_t max_file_num, uint64_t per_file_len);
	void __channel_close();
	void __channel_write_piece(seqno_t seqno, const safe_buffer& buf);

	bool check_disk_space();
	void create_channel_directory();
	uint32_t calc_file_index(seqno_t seqno);
	void generate_file_name(seqno_t seqno, std::string& filename);

private:
	path timeshift_dir_;
	path channel_dir_;
	std::string channel_name_;
	std::string channel_id_;
	boost::optional<uint64_t> o_max_file_count_;
	boost::optional<uint64_t> o_per_file_size_;
	boost::optional<seqno_t> o_first_seqno_;
	boost::optional<seqno_t> o_last_write_seqno_;
	boost::optional<uint32_t> o_per_file_block_count_;
	boost::optional<uint32_t> o_timeshift_total_block_;
	boost::optional<uint32_t> o_block_size_;
	boost::optional<uint32_t> o_last_file_index_;

	open_handler_type channel_open_handler;
	write_handler_type write_piece_handler;

	media_file_set_type	media_file_set_;
};
















NAMESPACE_END(asfio)
#endif











