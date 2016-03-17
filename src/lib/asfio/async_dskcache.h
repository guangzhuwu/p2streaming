#ifndef abstract_async_file_h__
#define abstract_async_file_h__

#include <p2engine/p2engine.hpp>
#include "asfio/disk_cache_impl.hpp"
#include "asfio/new_timeshift_io.hpp"

namespace asfio{
	/************************************************************************/
	/*                      cache interface define                          */
	/************************************************************************/
	template<typename ImplService>
	class basic_async_dskcache
	{
		typedef basic_async_dskcache<ImplService> this_type;
		SHARED_ACCESS_DECLARE;
	
	protected:
		typedef ImplService impl_type;

		basic_async_dskcache(io_service& ios)
			:impl_(ios)
		{
		}
		virtual ~basic_async_dskcache(void)
		{
		}

	public:
		typedef uint32_t piece_id;
		typedef uint32_t chunk_id;
		typedef uint32_t seqno_t;
		typedef typename impl_type::channel_info_type channel_info_type;

		static boost::shared_ptr<this_type> create(io_service& ios)
		{
			return boost::shared_ptr<this_type>(new this_type(ios), shared_access_destroy<this_type>());
		}
		
		bool is_complete_packet()const{return impl_.is_complete_packet();}

		bool is_open()const {return impl_.is_open();};

		bool has_piece(const std::string& channelID, piece_id seqno)const 
		{
			return impl_.has_piece(channelID, seqno);
		}
		
		template<typename OpenHandler>
		void open(const boost::filesystem::path& file_name, 
			boost::int64_t fileLen, size_t chunkLen, size_t pieceLen, size_t memCacheSize, 
			const OpenHandler& handler)
		{
			impl_.open<OpenHandler>(file_name, fileLen, chunkLen, pieceLen, memCacheSize, handler);
		}

		void get_all_channels_info(std::vector<channel_info_type>& infos, 
			std::vector<channel_info_type>& deletedInfos)
		{
			impl_.get_all_channels_info(infos, deletedInfos);
		}
		void get_chunk_map(const std::string& channelID, std::string& chunkMap)
		{
			impl_.get_chunk_map(channelID, chunkMap);
		}
		void get_piece_map(const std::string& channelID, int& from, int& to, std::string& pieceMap)
		{
			impl_.get_piece_map(channelID, from, to, pieceMap);
		}
		void pop_piece_erased(const std::string& channelID, seqno_t& from, seqno_t& to, bool pop=false)
		{
			impl_.pop_piece_erased(channelID, from, to, pop);
		}

		template<typename ReadHandler>
		void read_piece(const std::string& channel_name, 
			piece_id seqno, 
			const ReadHandler& handler)
		{
			impl_.read_piece<ReadHandler>(channel_name, seqno, handler);
		}

		template<typename WriteHandler>
		void write_piece(const std::string& channel_name, 
			size_t totalPieceCnt, 
			piece_id seqno, const safe_buffer& buf, 
			const WriteHandler& handler)
		{
			impl_.write_piece<WriteHandler>(channel_name, totalPieceCnt, seqno, buf, handler);
		}

		const boost::filesystem::path& path()const
		{
			return impl_.path();
		}
	protected:
		impl_type impl_;
	};

	/************************************************************************/
	/* cache interface asfile特化版本                                       */
	/************************************************************************/
	template<>
	class basic_async_dskcache<aiofile>
	{
		typedef basic_async_dskcache<aiofile> this_type;
		SHARED_ACCESS_DECLARE;

	protected:
		typedef aiofile impl_type;

		basic_async_dskcache(io_service& ios)
			:impl_(ios), fileLen_(-1)
		{
		}
		virtual ~basic_async_dskcache()
		{
		}

	public:
		typedef uint32_t piece_id;
		typedef uint32_t chunk_id;
		typedef struct{
			std::string channel_id;
			int health;
		}channel_info_type;

		static boost::shared_ptr<this_type> create(io_service& ios)
		{
			return boost::shared_ptr<this_type>(new this_type(ios), 
				shared_access_destroy<this_type>());
		}

		bool is_complete_packet()const{return false;}
		bool is_open()const {return impl_.is_open();};
		bool has_piece(const std::string& channelID, piece_id seqno)const 
		{
			if (!is_open())
				return false;
			return seqno*pieceLen_<fileLen_;
		}

		template<typename OpenHandler>
		void open(const boost::filesystem::path& file_name, 
			int64_t fileLen, size_t chunkLen, size_t pieceLen, size_t memCacheSize, 
			const OpenHandler& handler)
		{
			file_name_=file_name;
			chunkLen_=chunkLen;
			pieceLen_=pieceLen;
			memCacheSize_=memCacheSize;
			fileLen_=fileLen;

			impl_.async_open(file_name, p2engine::read_only, handler);
		}

		void get_chunk_map(const std::string&, std::string& chunkMap)
		{
			chunkMap.clear();
			if (fileLen_<=0)
				return;
			chunkMap.assign(static_cast<int>((fileLen_+chunkLen_-1)/chunkLen_), ~char(0));
		}

		void get_piece_map(const std::string& channelID, int& from, int& to, std::string& pieceMap)
		{
			pieceMap.clear();
			if (to<=from)
			{
				to=from;
				return;
			}
			pieceMap.assign((to-from+8-1)/8, ~char(0));
		}

		template<typename ReadHandler>
		void read_piece(const std::string& channel_name, 
			piece_id seqno, const ReadHandler& handler)
		{
			if (fileLen_<=0)
				return;

			off_type offset=seqno*pieceLen_;
			if (offset>fileLen_)
			{
				impl_.get_io_service().post(
					boost::bind(boost::protect(handler), safe_buffer(), boost::asio::error::eof)
					);
				return;
			}
			int piecelen=(int)std::min(fileLen_-offset, pieceLen_);
			safe_buffer buf((int)piecelen);
			boost::asio::async_read_at(impl_, offset, 
				buf.to_asio_mutable_buffers_1(), 
				boost::asio::transfer_at_least(piecelen), 
				boost::bind(&this_type::on_read<BOOST_TYPEOF(boost::protect(handler))>, 
				this, boost::protect(handler), buf, _1, _2)
				);
		}

		template<typename WriteHandler>
		void write_piece(const std::string& channel_name, 
			size_t totalPieceCnt, 
			piece_id seqno, const safe_buffer& buf, 
			const WriteHandler& handler)
		{
			off_type offset=seqno*pieceLen_;
			/*safe_buffer buf((int)pieceLen_);*/
			boost::asio::async_write_at(impl_, offset, buf.to_asio_const_buffers_1(), 
				boost::asio::transfer_all(), handler);
		}

		const boost::filesystem::path& path()const
		{
			return file_name_;
		}

	private:
		void get_all_channels_info(std::vector<channel_info_type>& infos, 
			std::vector<channel_info_type>& deletedInfos);

		template<typename ReadHandler>
		void on_read( const ReadHandler& handler, safe_buffer buf, error_code e, size_t len)
		{
			if (e)
			{
				if (e == boost::asio::error::bad_descriptor
					||e==boost::asio::error::eof
					)
				{
					p2engine::error_code err;
					impl_.close(err);
					open(file_name_, fileLen_, chunkLen_, pieceLen_, memCacheSize_, 
						boost::bind(&this_type::dymmy_handler, _1));
				}
			}
			if (len>0) e.clear();
			buf.resize(len);
			handler(buf, e);
		}
		static void dymmy_handler(error_code ec){}

	protected:
		impl_type impl_;
		int64_t fileLen_;
		int64_t chunkLen_, pieceLen_, memCacheSize_;
		boost::filesystem::path file_name_;
	};

	typedef basic_async_dskcache<disk_cache_impl> async_dskcache;
	typedef basic_async_dskcache<aiofile>         async_filecache;

	class media_cache_service
		: public basic_engine_object
	{
		typedef media_cache_service this_type;
		typedef uint32_t  seqno_t;
		SHARED_ACCESS_DECLARE;
	public:
		static this_type::shared_ptr create(io_service& ios)
		{
			return this_type::shared_ptr(new this_type(ios), shared_access_destroy<this_type>());
		}

	public:
		template<typename OpenHandler>
		void open(const boost::filesystem::path& name, 
			const boost::filesystem::path& channel_dir, 
			const std::string& channel_id, 
			uint64_t max_duration, /*缓存的最大时间，单位为second*/
			uint64_t fileLen/*单个缓存文件的大小，byte*/, 
			const OpenHandler& handler)
		{
			file_name_=name;
			max_duration_ = max_duration;
			file_len_ = fileLen;

			dskcache_service_ = timeshiht_channel::create(get_io_service());
			dskcache_service_->channel_open(name, channel_dir, channel_id, file_num_, 
				file_len_, handler);
		}
		
		template<typename WriteHandler>
		void write_piece(seqno_t seqno, const safe_buffer& buf, 
			const WriteHandler& handler)
		{
			dskcache_service_->channel_write_piece(seqno, buf, handler);
		}

		void recal_cache_limit(int new_pkt_rate, int format_size)
		{	
			if(new_pkt_rate <= channel_pkt_rate_)
				return;

			channel_pkt_rate_ = new_pkt_rate;
			//根据码率计算对应时间的文件大小
			uint64_t total_size = channel_pkt_rate_ * format_size* max_duration_;
			file_num_ = static_cast<int>(total_size / file_len_ + 0.5);
			dskcache_service_->reset_channel_limit(file_num_);
		}

		const boost::filesystem::path& path()const
		{
			return file_name_;
		}

	protected:
		media_cache_service(io_service& ios)
			: basic_engine_object(ios)
			, channel_pkt_rate_(0)
			, file_num_(1)
		{
		}
		~media_cache_service(){}
		
	private:
		boost::shared_ptr<timeshiht_channel> dskcache_service_;
		int channel_pkt_rate_;
		uint64_t max_duration_;
		uint64_t file_len_;
		int file_num_;
		boost::filesystem::path file_name_;
	};
};
#endif // abstract_async_file_h__
