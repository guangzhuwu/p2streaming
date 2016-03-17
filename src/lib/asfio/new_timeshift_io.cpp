#include <boost/filesystem.hpp>
#include <boost/make_shared.hpp>

#include "p2engine/p2engine.hpp"
#include "common/typedef.h"
#include "new_timeshift_io.hpp"


NAMESPACE_BEGIN(asfio)

	using std::cout;
using std::endl;

using boost::filesystem::create_directory;
using boost::uint32_t;
using boost::uint64_t;

using p2engine::io_service;
using p2engine::safe_buffer;
using p2common::seqno_t;


#if !defined(_DEBUG_SCOPE) && defined(NDEBUG)
#define  TIMESHIFT_DBG(x)
#else 
#define  TIMESHIFT_DBG(x) x
#endif

#define  TIMESHIFT_ERR(x) x
#define  TIMESHIFT_INFO(x) x

class media_piece
{
public:
	typedef media_piece this_type;
public:
	static boost::shared_ptr<this_type> create(seqno_t seqno, const safe_buffer& buf)
	{
		return boost::shared_ptr<this_type>(new this_type(seqno, buf), p2engine::shared_access_destroy<this_type>());
	}
public:
	media_piece(seqno_t seqno, const safe_buffer& buf);
	~media_piece(){}
public:
	seqno_t seqno() const
	{ 
		return seqno_; 
	}
	safe_buffer& buffer()
	{
		return buffer_;
	}
	void set_offset(uint64_t offset)
	{
		offset_ = offset;
	}
	uint64_t get_offset()
	{
		return offset_;
	}
private:
	seqno_t	seqno_;
	uint64_t offset_;
	safe_buffer buffer_;
};

media_piece::media_piece(seqno_t seqno, const safe_buffer& buf)
	:seqno_(seqno)
	, offset_(0)
	, buffer_(buf)
{
	//printf("seqno_:%d  media_piece:  g: 0x%x   p: 0x%x\n", seqno_, buffer_.g_ptr_, buffer_.p_ptr_);
	//static int count = 0;
	//p2engine::safe_buffer_io io(&buffer_);
	//io.write(p2engine::buffer_cast<char*>(buf), buf.length());
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class media_file
	:public p2engine::basic_engine_object
{
	typedef media_file this_type;
	SHARED_ACCESS_DECLARE;

	typedef boost::system::error_code error_code;
	typedef p2engine::aiofile aiofile_type;
public:
	enum {DEFAULT_CACHE_PIECE=1};
public:
	static boost::shared_ptr<this_type> create(io_service& ios, const std::string& filename, boost::shared_ptr<timeshiht_channel> tc_sptr)
	{
		return boost::shared_ptr<this_type>(new this_type(ios, filename, tc_sptr), p2engine::shared_access_destroy<this_type>());
	}
public:
	friend class p2engine::safe_buffer;

	media_file(io_service& ios, const std::string& filename, boost::shared_ptr<timeshiht_channel> tc_sptr);
	~media_file();
public:
	void open(seqno_t first_seqno, uint32_t total_block_count, uint32_t per_block_size, uint32_t cache_bolck_count, bool circle_flag=false);
	void write_meida_piece(boost::shared_ptr<media_piece>& media_piece_sptr);
	bool is_opened();
	void close();
private:
	void do_write_media_piece();
	void handle_opened( error_code ec, const std::string& filename);
	void handle_writed( error_code ec, size_t len, seqno_t seqno);
	void __close();
private:
	bool is_last_seqno_of_file(seqno_t seqno)
	{
		BOOST_ASSERT( !single_file_circle_ && o_last_seqno_);
		return seqno == *o_last_seqno_;
	}
private:
	boost::weak_ptr<timeshiht_channel> tc_wptr_;
	std::string filename_;
	boost::optional<seqno_t> o_first_seqno_;
	boost::optional<seqno_t> o_last_seqno_;
	boost::optional<uint32_t> o_total_block_count_;
	boost::optional<uint32_t> o_per_block_size_;
	boost::optional<uint64_t> o_file_size_;
	boost::optional<uint32_t> o_cache_bolck_count_;
	bool opened_;
	bool single_file_circle_;
	uint32_t writed_block_count_;
	aiofile_type aiofile_handler_;
	std::queue<boost::shared_ptr<media_piece> >	meida_queue_;
	std::queue<boost::shared_ptr<media_piece> >	meida_queue_111_;
};

media_file::media_file(io_service& ios, const std::string& filename, boost::shared_ptr<timeshiht_channel> tc_sptr)
	:basic_engine_object(ios)
	, filename_(filename)
	, tc_wptr_(tc_sptr)
	, o_first_seqno_(boost::none)
	, o_last_seqno_(boost::none)
	, o_total_block_count_(boost::none)
	, o_per_block_size_(boost::none)
	, o_file_size_(boost::none)
	, o_cache_bolck_count_(boost::none)
	, writed_block_count_(0)
	, aiofile_handler_(ios)
	, meida_queue_()
	, single_file_circle_(false)
	, opened_(false)
{
	aiofile_handler_.async_open(filename_, p2engine::read_write, boost::bind(&this_type::handle_opened, this, _1, filename_));
}

media_file::~media_file()
{
	if( opened_)
	{
		__close();
	}
}

void media_file::open(seqno_t first_seqno, uint32_t total_block_count, uint32_t per_block_size, uint32_t cache_bolck_count, bool circle_flag)
{
	single_file_circle_ = circle_flag;
	o_first_seqno_ = first_seqno;
	o_total_block_count_ = total_block_count;
	if(!circle_flag)
	{
		o_last_seqno_ = first_seqno + total_block_count -1;
	}
	o_per_block_size_ = per_block_size;
	o_file_size_ = *o_total_block_count_ * (*o_per_block_size_);
	o_cache_bolck_count_ = cache_bolck_count;
	writed_block_count_ = 0;

	opened_ = true;

	TIMESHIFT_DBG(
		cout<<">>>>> media_file open "<<filename_<<" <<<<<<"<<endl;
	cout<<"first_seqno_: "<<*o_first_seqno_<<endl;
	cout<<"total_block_count_: "<<*o_total_block_count_<<endl;
	cout<<"per_block_size_: "<<*o_per_block_size_<<endl;
	cout<<"cache_bolck_count_: "<<*o_cache_bolck_count_<<endl;
	cout<<"single_file_circle_: "<<single_file_circle_<<endl;
	);
}

void media_file::close()
{
	__close();
}

bool media_file::is_opened()
{
	return opened_;
}

void media_file::__close()
{
	if(!opened_)
	{
		return ;
	}

	if( aiofile_handler_.is_open())
	{
		aiofile_handler_.close();
	}
	o_first_seqno_ = boost::none;
	o_total_block_count_ = boost::none;
	o_per_block_size_ = boost::none;
	o_cache_bolck_count_ = boost::none;
	writed_block_count_ = 0;

	while(!meida_queue_.empty())
	{
		meida_queue_.pop();
	}

	opened_ = false;

	TIMESHIFT_DBG(
		cout<<">>>>> media_file close "<<filename_<<" <<<<<<"<<endl;
	//BOOST_ASSERT(0);
	);
}

void media_file::write_meida_piece(boost::shared_ptr<media_piece>& media_piece_sptr)
{
	if(!opened_)
		return ;

	BOOST_ASSERT(o_first_seqno_ && o_total_block_count_ && o_per_block_size_ && o_cache_bolck_count_);

	if( !single_file_circle_) //非单文件循环模式下，保证seqno正确性
	{
		BOOST_ASSERT( p2common::seqno_minus(media_piece_sptr->seqno(), *o_first_seqno_) < (int)*o_total_block_count_);
		//if( p2common::seqno_minus(media_piece_sptr->seqno(), *o_first_seqno_) >= *o_total_block_count_)
		//{
		//	TIMESHIFT_DBG(
		//		cout<<"*o_total_block_count_: "<<*o_total_block_count_<<endl;
		//		cout<<"media_piece_sptr->seqno(): "<<media_piece_sptr->seqno()<<endl;
		//		cout<<"*o_first_seqno_: "<<*o_first_seqno_<<endl;
		//		cout<<"seqno_minus(media_piece_sptr->seqno(), *o_first_seqno_): "<<p2common::seqno_minus(media_piece_sptr->seqno(), *o_first_seqno_)<<endl;
		//	);
		//	BOOST_ASSERT(0);
		//	return ;
		//}
	}

	if( !aiofile_handler_.is_open())
	{
		aiofile_handler_.async_open(filename_, p2engine::read_write, boost::bind(&this_type::handle_opened, this, _1, filename_));
	}

	uint64_t write_offset = (*o_per_block_size_) * ( p2common::seqno_minus(media_piece_sptr->seqno(), *o_first_seqno_) % *o_total_block_count_);
	media_piece_sptr->set_offset(write_offset);
	meida_queue_.push(media_piece_sptr);

	if( aiofile_handler_.is_open())
	{
		if(!single_file_circle_)
		{
			if(is_last_seqno_of_file(media_piece_sptr->seqno()) /* && meida_queue_.size() == *o_total_block_count_ % *o_cache_bolck_count_*/)
			{
				do_write_media_piece();
				return ;
			}
		}

		if( meida_queue_.size() >= o_cache_bolck_count_)
		{
			do_write_media_piece();
		}
	}
}

void sbuf_data_check(safe_buffer& buf, int count);
void media_file::do_write_media_piece()
{
	boost::shared_ptr<media_piece> media_piece_sptr;
	uint64_t write_offset;
	while(!meida_queue_.empty())
	{
		media_piece_sptr = meida_queue_.front();

		TIMESHIFT_DBG(;
		//	cout<<"media_piece_sptr->seqno(): "<<media_piece_sptr->seqno()<<endl;
		//cout<<"write_offset: "<<write_offset<<endl;
		//cout<<"seqno_minus: "<<p2common::seqno_minus(media_piece_sptr->seqno(), *o_first_seqno_)<<endl;
		);
		write_offset = media_piece_sptr->get_offset();
		if( write_offset > *o_file_size_ - *o_per_block_size_)
		{
			TIMESHIFT_DBG(;
			cout<<"media_piece_sptr->seqno(): "<<media_piece_sptr->seqno()<<endl;
			cout<<"*o_first_seqno_: "<<*o_first_seqno_<<endl;
			cout<<"seqno_minus(media_piece_sptr->seqno(), *o_first_seqno_): "<<p2common::seqno_minus(media_piece_sptr->seqno(), *o_first_seqno_)<<endl;
			cout<<"write_offset: "<<write_offset<<endl;
			cout<<"writed_block_count_: "<<writed_block_count_<<endl;
			cout<<"*o_file_size_: "<<*o_file_size_<<endl;
			cout<<"*o_per_block_size_: "<<*o_per_block_size_<<endl;
			cout<<"*o_file_size_ - *o_per_block_size_: "<<*o_file_size_ - *o_per_block_size_<<endl;
			);
			BOOST_ASSERT( write_offset <= *o_file_size_ - *o_per_block_size_);
		}

		//sbuf_data_check(media_piece_sptr->buffer(), p2common::seqno_minus(media_piece_sptr->seqno(), *o_first_seqno_));
		//printf("^^^^^ will use raw_buffer: g: 0x%x p:0x%x\n", media_piece_sptr->buffer().g_ptr_, media_piece_sptr->buffer().p_ptr_);
		//printf("seqno: %d  do_write_media_piece:  g: 0x%x   p: 0x%x\n", media_piece_sptr->seqno(), media_piece_sptr->buffer().g_ptr_, media_piece_sptr->buffer().p_ptr_);

		aiofile_handler_.async_write_some_at(write_offset, media_piece_sptr->buffer().to_asio_const_buffers_1(), 
			boost::bind(&this_type::handle_writed, this, _1, _2, media_piece_sptr->seqno()) );
		meida_queue_.pop();
	}
}

void media_file::handle_opened( error_code ec, const std::string& filename)
{
	TIMESHIFT_DBG(;
	cout<<"handle_opened: "<<filename<<" "<<ec.message()<<endl;
	);
}

void media_file::handle_writed( error_code ec, size_t len, seqno_t seqno)
{
	TIMESHIFT_DBG(;
	//cout<<"handle_writed, seqno_diff: "<<p2common::seqno_minus(seqno, *o_first_seqno_)<<" len: "<<len<<" "<<ec.message()<<endl;
	);

	if(!ec)
	{
		writed_block_count_++;
		if( !single_file_circle_)//非循环模式，写完最后一个piece关闭文件
		{
			if( writed_block_count_ == *o_total_block_count_)
			{
				TIMESHIFT_DBG(;
				cout<<"seqno: "<<seqno<<endl;
				cout<<"*o_first_seqno_: "<<*o_first_seqno_<<endl;
				cout<<"*o_total_block_count_: "<<*o_total_block_count_<<endl;
				cout<<"*o_first_seqno_ + *o_total_block_count_: "<<*o_first_seqno_ + *o_total_block_count_<<endl;
				);

				BOOST_ASSERT( !(writed_block_count_ == *o_total_block_count_ && !meida_queue_.empty()));
				BOOST_ASSERT( !(writed_block_count_ == *o_total_block_count_ && !is_last_seqno_of_file(seqno)));
			}
			if( writed_block_count_ == *o_total_block_count_ && is_last_seqno_of_file(seqno) && meida_queue_.empty())
			{
				__close();
			}
		}
	}
	else
	{
		TIMESHIFT_ERR(
			cout<<"async write error, seqno: "<<seqno<<" message: "<<ec.message()<<endl;
		);
		__close();
	}

	if(!tc_wptr_.expired())
	{
		tc_wptr_.lock()->write_piece_handler(ec, seqno);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

timeshiht_channel::timeshiht_channel( io_service& ios)
	:basic_engine_object(ios)
{

}

timeshiht_channel::timeshiht_channel( io_service& ios, path file_path, const std::string& channel_id, size_t file_size, int file_num)
	:basic_engine_object(ios)
	, timeshift_dir_()
	, channel_name_()
	, channel_dir_()
	, channel_id_()
	, o_max_file_count_(boost::none)
	, o_per_file_size_(boost::none)
	, o_first_seqno_(boost::none)
	, o_last_write_seqno_(boost::none)
	, o_per_file_block_count_(boost::none)
	, o_timeshift_total_block_(boost::none)
	, o_block_size_(boost::none)
	, o_last_file_index_(boost::none)
	, media_file_set_()
{

}

void timeshiht_channel::__channel_open(const path& name, const path& timeshift_dir, const std::string& channel_id, uint64_t max_file_num, uint64_t per_file_len)
{
	if( !check_disk_space())
	{
		TIMESHIFT_INFO(
			cout<<"not enough disk space to open channel"<<endl;
		);
		return ;
	}

	timeshift_dir_ = timeshift_dir;
	channel_name_ = name.string();
	channel_id_ = channel_id;
	boost::filesystem::path	channel_path(channel_id);
	channel_dir_ = timeshift_dir_ / channel_path;
	o_max_file_count_ = max_file_num;//                             + 3;
	o_per_file_size_ = per_file_len;

	create_channel_directory();
	//channel_open_handler( make_error_code(boost::system::errc::success));
}

void timeshiht_channel::__channel_close()
{
	timeshift_dir_.clear();
	channel_dir_.clear();
	channel_name_.clear();
	channel_id_.clear();
	o_max_file_count_ = boost::none;
	o_per_file_size_ = boost::none;
	o_first_seqno_ = boost::none;
	o_last_write_seqno_ = boost::none;
	o_per_file_block_count_ = boost::none;
	o_timeshift_total_block_ = boost::none;
	o_block_size_ = boost::none;
	o_last_file_index_ = boost::none;
	channel_open_handler.clear();
	write_piece_handler.clear();

	// media_file 自动析构 释放资源
	media_file_set_.clear();
}

void timeshiht_channel::__channel_write_piece(seqno_t seqno, const safe_buffer& buf)
{
	static boost::shared_ptr<media_file> media_file_sptr; 
	static boost::shared_ptr<media_piece> new_media_piece_sptr; 

	TIMESHIFT_INFO(;
		//cout<<"seqno: "<< seqno <<endl;
		);

	if( !o_first_seqno_)
	{
		o_first_seqno_ = seqno;
		o_last_write_seqno_ = seqno;
		o_block_size_ = buf.size();
		o_per_file_size_ = *o_per_file_size_ - ( *o_per_file_size_ % (*o_block_size_));
		o_per_file_block_count_ = *o_per_file_size_ / *o_block_size_;
		o_timeshift_total_block_ = *o_max_file_count_ * (*o_per_file_block_count_);

		TIMESHIFT_INFO(
			cout<<">>>>> first time write piece <<<<<<"<<endl;
		cout<<"first_seqno_: "<<*o_first_seqno_<<endl;
		cout<<"max_file_count_: "<<*o_max_file_count_<<endl;
		cout<<"per_file_len_: "<<*o_per_file_size_<<endl;
		cout<<"block_size_: "<<*o_block_size_<<endl;
		cout<<"per_file_block_: "<<*o_per_file_block_count_<<endl;
		cout<<"total_block_: "<<*o_timeshift_total_block_<<endl;
		cout<<"channel_dir_: "<<channel_dir_<<endl;
		);
	}
	else
	{
		// seqno 必须有序到达
		TIMESHIFT_INFO(
			if( seqno != *o_last_write_seqno_ + 1)
			{
				seqno = *o_last_write_seqno_ +1;
				//cout<<"seqno: "<<seqno<<endl;
				//cout<<"o_last_write_seqno_: "<<*o_last_write_seqno_<<endl;
			}
			);
			//BOOST_ASSERT( seqno == *o_last_write_seqno_ + 1);
	}
	BOOST_ASSERT(o_first_seqno_ && o_max_file_count_ && o_per_file_size_ && o_block_size_ && o_per_file_block_count_ && o_timeshift_total_block_);

	uint32_t file_index = calc_file_index(seqno);
	media_file_set_type::iterator iter = media_file_set_.find( file_index);
	BOOST_ASSERT( file_index < *o_max_file_count_);

	if(iter == media_file_set_.end())
	{
		std::string filename( channel_dir_.string() + "/"+boost::lexical_cast<std::string>(file_index) + ".ts");
		//std::string filename( boost::lexical_cast<std::string>(file_index) + ".ts");
		boost::shared_ptr<media_file> new_meida_file_sptr = media_file::create(get_io_service(), filename, SHARED_OBJ_FROM_THIS);
		media_file_set_.insert(std::make_pair(file_index, new_meida_file_sptr));
	}

	media_file_sptr = media_file_set_[file_index];
	if( !media_file_sptr->is_opened())
	{
		media_file_sptr->open(seqno, *o_per_file_block_count_, *o_block_size_, media_file::DEFAULT_CACHE_PIECE, (*o_max_file_count_==1?true:false) );
	}

	new_media_piece_sptr = media_piece::create(seqno, buf);

	TIMESHIFT_DBG(;
		//cout<<"new_media_piece_sptr->seqno(): "<<new_media_piece_sptr->seqno()<<endl;
		);
	media_file_sptr->write_meida_piece(new_media_piece_sptr);

	o_last_write_seqno_ = seqno;
}

void timeshiht_channel::reset_channel_limit(uint32_t file_size)
{
	// 目前不可以支持在存储过程中 修改单个文件的大小
	// 动态修改单个存储文件大小，会导致无法计算seqno的piece应该写入的文件序号，除非放弃所有该channel_id下已经储存的数据

	//TIMESHIFT_DBG(
	//cout<<"reset_channel_limit: "<<file_size<<endl;
	//);
	//o_per_file_size_ = file_size;
}

timeshiht_channel::uint32_t timeshiht_channel::calc_file_index(seqno_t seqno)
{
	seqno_t	seq_diff = p2common::seqno_minus(seqno, *o_first_seqno_);
	return (  seq_diff % (*o_timeshift_total_block_) ) / (*o_per_file_block_count_);
}

void timeshiht_channel::create_channel_directory()
{
	if( !boost::filesystem::exists(timeshift_dir_))
	{
		error_code ec;
		bool ret;

		TIMESHIFT_DBG(;
		cout<<"create_directory: "<<timeshift_dir_<<endl;
		);


#if BOOST_VERSION > 104600
		ret = create_directory(timeshift_dir_, ec);
#else
		ret = create_directory(timeshift_dir_);
#endif		
		if( !ret)
		{
			TIMESHIFT_ERR(
				cout<<"create_directory error: "<<endl;
			);
			channel_open_handler(ec);
			return ;
		}
	}

	if( !boost::filesystem::exists(channel_dir_))
	{
		error_code ec;
		bool ret;

		TIMESHIFT_DBG(;
		cout<<"create_directory: "<<channel_dir_<<endl;
		);

#if BOOST_VERSION > 104600
		ret = create_directory(channel_dir_, ec);
#else
		ret = create_directory(channel_dir_);
#endif
		if( !ret)
		{
			TIMESHIFT_ERR(;
				cout<<"create_directory channel_dir: "<<channel_dir_<<" error: "<<ret<<endl;
			);
			channel_open_handler(ec);
			return ;
		}
	}

	// 这里认为目录创建成功，就算是channel_open 成功了
	channel_open_handler( make_error_code(boost::system::errc::success));
}

bool timeshiht_channel::check_disk_space()
{
	return true;
}

NAMESPACE_END(asfio)
