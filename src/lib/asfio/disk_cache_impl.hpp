#ifndef __ASFILE_DSK_CACHE_IMPL_HPP__
#define __ASFILE_DSK_CACHE_IMPL_HPP__

#include <p2engine/push_warning_option.hpp>
#include <memory>
#include <string>
#include <boost/unordered_map.hpp>
#include <boost/filesystem/path.hpp>
#include <p2engine/pop_warning_option.hpp>


#include "asfio/dispatch_helper.hpp"
#include "asfio/os_api.hpp"
//extern FILE*	g_fpppp;

namespace asfio{
	using namespace p2engine;

	/************************************************************************/
	/* cache interface disk cache 实现版本                                  */
	/************************************************************************/
	class disk_cache_impl
		:public backstage_running_base
	{
		typedef disk_cache_impl this_type;
		SHARED_ACCESS_DECLARE;

		enum{INVALID_CHUNK_ID=0xffff};
		enum{MAX_CHUNK_ID=0xfffe};

		typedef int32_t chunk_id;
		typedef int32_t piece_id;
		typedef uint32_t seqno_t;

		/************************************************************************/
		/* 
		//缓存文件的结构为：
		1.param部分，主要存储chunk长度、piece长度、缓存文件大小等参数
		2.chunk的信息区，如channelID、影片文件长度等
		3.chunk数据区
		struct
		{
		param m_param;
		//以下是chunk info行，根据缓冲文件大小有不同的行数
		channel_uuid m_channel_id;
		size_type m_channel_length;//频道文件的长度
		chunk_id  m_orig_chunk_id;//在原始文件中的chunk号
		uint16_t m_real_piece_count;
		int m_read_times;
		char m_piece_bitset[bitset_len];
		};
		*/
		/************************************************************************/

		template<typename intType>
		struct verifiable_int
		{
			intType data;
			intType data_verify;

			//verifiable_int():data(intType()), data_verify(~intType()){}
			//verifiable_int(intType v){write(v);}

			intType read()const{
				BOOST_STATIC_ASSERT(sizeof(verifiable_int<intType>)==2*sizeof(intType));
				if(data!=bswap<intType>()(data_verify))
				{
					boost::system::system_error e(boost::system::errc::invalid_argument, 
						boost::system::generic_category(), 
						"verifiable_int check error");
					boost::throw_exception(e);
				}
				return data;
			}
			void write(intType v){
				BOOST_STATIC_ASSERT(sizeof(verifiable_int<intType>)==2*sizeof(intType));
				data=v;
				data_verify=bswap<intType>()(v);
			}
			bool is_valid()const
			{
				return data==bswap<intType>()(data_verify);
			}
		};
		struct param{
			enum{SIZE_ON_DSK=128};
			verifiable_int<size_type> m_data[SIZE_ON_DSK/sizeof(verifiable_int<size_type>)];

			param()
			{
				chunk_cnt_per_file(-1);
				file_len(-1);
				chunk_len(-1);
				piece_len(-1);
			}
			param(const char*& dskDataPtr, bool& error)
			{
				BOOST_ASSERT(dskDataPtr);
				const char* p=dskDataPtr;
				memcpy(&m_data[0], dskDataPtr, sizeof(m_data));
				dskDataPtr+=sizeof(m_data);
				BOOST_ASSERT((int)(p-dskDataPtr)==SIZE_ON_DSK);

				error=!is_valid();
			}

			bool is_valid()const
			{
				return m_data[1].is_valid()&&m_data[2].is_valid()
					&&m_data[3].is_valid()&&m_data[4].is_valid();
			}
			char* version(){return (char*)&m_data[0];};

			size_type chunk_cnt_per_file()const{return m_data[1].read();};
			size_type file_len()const{return m_data[2].read();};
			size_type chunk_len()const{return m_data[3].read();};
			size_type piece_len()const{return m_data[4].read();};

			void chunk_cnt_per_file(size_type v){return m_data[1].write(v);};
			void file_len(size_type v){return m_data[2].write(v);};
			void chunk_len(size_type v){return m_data[3].write(v);};
			void piece_len(size_type v){return m_data[4].write(v);};

		private:
			param( const param& );
			const param& operator=( const param& );
		};

		struct channel_uuid: object_allocator{
			typedef  verifiable_int<int16_t> length_type;

			enum{MAX_CHANNEL_ID_SIZE=20};
			enum{element_cnt=(MAX_CHANNEL_ID_SIZE+sizeof(length_type)+sizeof(uint32_t)-1)/sizeof(uint32_t)};
			enum {SIZE_ON_DSK=element_cnt*sizeof(uint32_t)};

			BOOST_STATIC_ASSERT(MAX_CHANNEL_ID_SIZE%sizeof(uint32_t)==0);

			uint32_t m_id[element_cnt];//m_id[element_cnt-1]中存储实际长度

#define  UUID_LEN_PTR length_type* lenPtr=(length_type*)(&m_id[element_cnt-1])

			channel_uuid()
			{
				memset(m_id, 0, sizeof(m_id));
			}
			channel_uuid(const std::string& channelID)
			{
				UUID_LEN_PTR;
				int len=std::min<int>(MAX_CHANNEL_ID_SIZE, channelID.length());
				memcpy(m_id, &channelID[0], len);
				if (sizeof(m_id)>len+sizeof(length_type))//中间补0
					memset(((char*)m_id)+len, 0, sizeof(m_id)-len-sizeof(length_type));
				lenPtr->write(len);//将长度记录下来
			}
			channel_uuid(const channel_uuid& channelID)
			{
				memcpy(m_id, channelID.m_id, sizeof(m_id));
			}
			channel_uuid& operator =(const channel_uuid& channelID)
			{
				if (this!=&channelID)
					memcpy(m_id, channelID.m_id, sizeof(m_id));
				return *this;
			}

			channel_uuid(const char*& dskDataPtr)
			{
				UUID_LEN_PTR;
				memcpy(m_id, dskDataPtr, sizeof(m_id));
				dskDataPtr+=sizeof(m_id);
				if(!lenPtr->is_valid()||lenPtr->read()<0)
				{
					*this=invalid_uuid();
				}
				else
				{
					int len=lenPtr->read();
					if (sizeof(m_id)>len+sizeof(length_type))//中间补0
						memset(((char*)m_id)+len, 0, sizeof(m_id)-len-sizeof(length_type));
				}
			}

			bool operator<(const channel_uuid& channelID)const
			{
				if (this==&channelID)
					return false;
				for (int i=0;i<element_cnt;++i)
				{
					if (m_id[i]!=channelID.m_id[i])
						return m_id[i]<channelID.m_id[i];
				}
				return false;
			}
			bool operator==(const channel_uuid& channelID)const
			{
				if (this==&channelID)
					return true;
				for (int i=0;i<element_cnt;++i)
				{
					if (m_id[i]!=channelID.m_id[i])
						return false;
				}
				return true;
			}
			bool operator!=(const channel_uuid& channelID)const
			{
				return !(*this==channelID);
			}
			struct hash
			{
				std::size_t operator()(const channel_uuid&id) const
				{
					int64_t rst=0;
					for (int i=0;i<channel_uuid::element_cnt;++i)
						rst^=id.m_id[i];
					return boost::hash<int64_t>()(rst);
				}
			};

			static const channel_uuid& invalid_uuid()
			{
				const static channel_uuid uid;
				return uid;
			}

			std::size_t size()const{return sizeof(m_id);}

			void to_string(std::string&s)const
			{
				UUID_LEN_PTR;
				if (lenPtr->is_valid())
					s.assign((const char*)m_id, lenPtr->read());
				else
					s.clear();
			}

			char& operator[](std::size_t n){return ((char*)m_id)[n];}
			const char& operator[](std::size_t n)const{return ((char*)m_id)[n];}

#undef UUID_LEN_PTR
		};

		struct channel: object_allocator, basic_intrusive_ptr<channel>{
			verifiable_int<int32_t> m_orig_piece_count;//原始影片文件的piece数目

			enum{SIZE_ON_DSK=(int)(
				sizeof(verifiable_int<int32_t>)//m_orig_size
				)
			};

			static std::pair<int, int> orig_size_offset_len()
			{
				return std::make_pair(0
					, (int)sizeof(verifiable_int<int32_t>));
			}

			channel(size_type totalPieceCnt, size_type chunkCntPerCacheFile, 
				size_type pieceCntPerChunk)
			{
				m_orig_piece_count.write((int)totalPieceCnt);
				m_orig_chunk_count=int((totalPieceCnt+pieceCntPerChunk-1)/pieceCntPerChunk);
				m_cache_chunk_count=(int)chunkCntPerCacheFile;
				reset();
			}
			channel(const char*& dskDataPtr, size_type chunkCntPerCacheFile, 
				size_type pieceCntPerChunk, bool& error)
			{
				BOOST_ASSERT(dskDataPtr);
				const char* p=dskDataPtr;
				error=false;

				memcpy(&m_orig_piece_count, dskDataPtr, sizeof(m_orig_piece_count));
				dskDataPtr+=sizeof(m_orig_piece_count);
				if (!m_orig_piece_count.is_valid())
				{
					error=true;
					return;
				}
				m_orig_chunk_count=int((m_orig_piece_count.read()+pieceCntPerChunk-1)/pieceCntPerChunk);
				m_cache_chunk_count=(int)chunkCntPerCacheFile;
				if (m_orig_chunk_count<=0||m_orig_chunk_count>MAX_CHUNK_ID)
				{
					error=true;
					return;
				}
				reset();

				BOOST_ASSERT((int)(dskDataPtr-p)==SIZE_ON_DSK);
				(void)(p);
			}

			void reset(){
				m_orgi_to_cache_chunk.reset(new uint16_t[m_orig_chunk_count]);
				for(int i=0;i<m_orig_chunk_count;++i)
					m_orgi_to_cache_chunk[i]=(uint16_t)INVALID_CHUNK_ID;
				m_cache_to_orig_chunk.reset(new uint16_t[m_cache_chunk_count]);
				for(int i=0;i<m_cache_chunk_count;++i)
					m_cache_to_orig_chunk[i]=(uint16_t)INVALID_CHUNK_ID;
				m_need_recalc_chunkmap=true;
			}

			//以下字段不会存储到硬盘
			boost::scoped_array<uint16_t> m_orgi_to_cache_chunk;
			boost::scoped_array<uint16_t> m_cache_to_orig_chunk;
			std::set<uint16_t> m_cached_chunks;
			std::vector<char> m_chunkmap;
			int m_orig_chunk_count;
			int m_cache_chunk_count;
			bool m_need_recalc_chunkmap;
		};

		struct chunk: object_allocator, basic_intrusive_ptr<chunk>{
			enum{MAX_PIECE_COUNT=5*1024};
			enum{bitset_len=MAX_PIECE_COUNT/8};

			verifiable_int<int16_t> m_orig_chunk_id;
			verifiable_int<int16_t> m_cached_piece_count;
			verifiable_int<int32_t> m_read_times;
			char m_piece_bitset[bitset_len];

			enum{SIZE_ON_DSK=(int)(
				+sizeof(verifiable_int<int16_t>)//m_orig_chunk_id
				+sizeof(verifiable_int<int16_t>)//m_real_piece_count
				+sizeof(verifiable_int<int32_t>)//m_read_times
				+bitset_len//m_piece_bitset
				)
			};

			static std::pair<int, int> orig_chunk_id_offset_len()
			{
				return std::make_pair(0
					, (int)sizeof(verifiable_int<int16_t>));
			}
			static std::pair<int, int> cached_piece_count_offset_len()
			{
				return std::make_pair((int)(sizeof(verifiable_int<int16_t>))
					, (int)sizeof(verifiable_int<int16_t>));
			}
			static std::pair<int, int> m_read_times_offset_len()
			{
				return std::make_pair((int)(sizeof(verifiable_int<int16_t>)+sizeof(verifiable_int<int16_t>))
					, (int)sizeof(verifiable_int<int32_t>));
			}
			static std::pair<int, int> cache_piece_bitset_offset_len(int bit)
			{
				return std::make_pair( int((sizeof(verifiable_int<int16_t>)+sizeof(verifiable_int<int16_t>)+sizeof(verifiable_int<int32_t>))+bit/8)
					, (int)sizeof(char));
			}

			chunk(chunk_id origChunkID, int cachedPieceCount){
				m_orig_chunk_id.write(origChunkID);
				m_cached_piece_count.write(cachedPieceCount);
				m_read_times.write(0);
				memset(m_piece_bitset, 0, sizeof(m_piece_bitset));
			}
			chunk(const char*& dskDataPtr, bool& error){
				BOOST_ASSERT(dskDataPtr);
				memset(m_piece_bitset, 0, sizeof(m_piece_bitset));

				const char* p=dskDataPtr;

				memcpy(&m_orig_chunk_id, dskDataPtr, sizeof(m_orig_chunk_id));
				dskDataPtr+=sizeof(m_orig_chunk_id);
				memcpy(&m_cached_piece_count, dskDataPtr, sizeof(m_cached_piece_count));
				dskDataPtr+=sizeof(m_cached_piece_count);
				memcpy(&m_read_times, dskDataPtr, sizeof(m_read_times));
				dskDataPtr+=sizeof(m_read_times);
				memcpy(m_piece_bitset, dskDataPtr, sizeof(m_piece_bitset));
				dskDataPtr+=sizeof(m_piece_bitset);

				BOOST_ASSERT((int)(dskDataPtr-p)==SIZE_ON_DSK);

				error=m_orig_chunk_id.is_valid()&&m_cached_piece_count.is_valid()&&m_read_times.is_valid();
			}
		};

		enum {CLOSED, OPENING, OPENED};//open state
		enum {PARAM_LINE_LEN=param::SIZE_ON_DSK};
		enum {CHUNK_INFO_LINE_LEN=channel_uuid::SIZE_ON_DSK+channel::SIZE_ON_DSK+chunk::SIZE_ON_DSK};
		enum {HEADER_LEN=PARAM_LINE_LEN+(4*1024*1024/CHUNK_INFO_LINE_LEN)*CHUNK_INFO_LINE_LEN};//4MB存储chunkinfor

		class channel_map
			:private boost::unordered_map<channel_uuid, boost::intrusive_ptr<channel>, channel_uuid::hash>
		{
			typedef boost::unordered_map<channel_uuid, boost::intrusive_ptr<channel>, channel_uuid::hash> base_type;
		public:
			typedef  base_type::const_local_iterator const_local_iterator;
			typedef  base_type::local_iterator local_iterator;
			typedef  base_type::const_iterator const_iterator;
			typedef  base_type::iterator iterator;

			using base_type::empty;
			using base_type::size;
			using base_type::max_size;
			using base_type::begin;
			using base_type::end;
			using base_type::cbegin;
			using base_type::cend;
			using base_type::insert;
			using base_type::operator[];
			iterator erase(const_iterator itr)
			{
				if (last_rw_channel_&&*last_rw_channel_==itr)
					last_rw_channel_.reset();
				return base_type::erase(itr);
			}
			size_type erase(const key_type& key)
			{
				if (last_rw_channel_&&(*last_rw_channel_)->first==key)
					last_rw_channel_.reset();
				return base_type::erase(key);
			}
			iterator find(const key_type& key)
			{
				if (last_rw_channel_)
				{
					const iterator& itr=*last_rw_channel_;
					if (itr!=end()&&itr->first==key)
					{
						return itr;
					}
				}
				iterator itr=base_type::find(key);
				if (itr!=end())
					last_rw_channel_=itr;
				return itr;
			}
			const_iterator find(const key_type& key) const
			{
				return const_iterator(
					(const_cast<channel_map*>(this))->find(key)
					);
			}
			void clear()
			{
				last_rw_channel_.reset();
				base_type::clear();
			}

		private:
			mutable boost::optional<iterator> last_rw_channel_;
		};
		//typedef boost::unordered_map<channel_uuid, boost::intrusive_ptr<channel>, channel_uuid::hash> channel_map;
		typedef boost::unordered_map<channel_uuid, chunk_id, channel_uuid::hash> chunkid_map;
		typedef boost::recursive_timed_mutex mutex_type;
		struct chunk_read_info 
		{
			int m_read_times;
			int m_cache_chunk_id;
		};

#if 0
		/************************************************************************/
		/* add zyliu 2012-2-6                                                   */
		/************************************************************************/
		typedef struct ck_cache
		{
			enum{INVALID_SEQNO=0xffffffff};
			enum{INVALID_CHUNK_ID=0xffff};
			enum{COUNT_TO_WRITE_DSK=64};

			ck_cache():cache_chunk_id(INVALID_CHUNK_ID)
				, cache_count(0), max_seqno(INVALID_SEQNO), min_seqno(INVALID_SEQNO)
			{
				//static int i = 0;
				//index = i++;
				//if(!g_fpppp)
				//g_fpppp = fopen("lllllog.txt", "w");
				//BOOST_ASSERT(g_fpppp);
			}
			bool is_match_chunk_id(int chunkID)
			{
				return cache_chunk_id==chunkID?true:false;
			}
			void reset(disk_cache_impl* p_disk_cache_impl, int chunkID, error_code& ec);
			void write_to_cache(disk_cache_impl* p_disk_cache_impl, int chunkID, 
				uint32_t cache_total, uint32_t seqno, error_code& ec);
			void write_to_disk(disk_cache_impl* p_disk_cache_impl, error_code& ec);
			void clear_cache_status(void);

			int32_t		cache_chunk_id;			// 为该cache_chunk_id提供缓存服务
			int32_t		cache_count;			// cache但是未写入 dsk的 piece 数
			int32_t		cached_count_total;		// cache一个piece 后该 chunk 已经记录的总piece数
			uint32_t	max_seqno;				// cache未写入的最大 seqno
			uint32_t	min_seqno;				// cache未写入的最小 seqno
			//int		index;
		}ck_cache;

		class chunk_info_write_cache
		{
		public:
			chunk_info_write_cache():last_opt_cache(NULL), p_disk_cache_impl(NULL){}
			void cache_write( int cache_chunk_id, uint32_t cache_total, uint32_t seqno, 
				error_code& ec);
			void set_disk_cache_impl_ptr(disk_cache_impl* p)
			{ 
				p_disk_cache_impl = p;
			}
		protected:
			boost::timed_mutex	mutex_;
			ck_cache			ck_cache_[2];
			ck_cache*			last_opt_cache;
			disk_cache_impl*	p_disk_cache_impl;
		};
		/************************************************************************/
		/* add zyliu 2012-2-6                                                   */
		/************************************************************************/
#endif

	public:
		typedef struct{
			std::string channel_id;
			int health;
		}channel_info_type;

		disk_cache_impl(io_service& ios);
		virtual ~disk_cache_impl();

		bool is_complete_packet()const{return true;}
		bool is_open()const {return  open_state_==OPENED;}
		bool has_piece(const std::string& channelID, piece_id seqno)const
		{
			if (mutex_.timed_lock(millisec(20)))
			{
				this_type* thisPtr=const_cast<this_type*>(this);
				bool rst= thisPtr->__piece_offset(channelID, seqno)>=0;
				mutex_.unlock();
				return rst;
			}
			return false;
		}
		void get_all_channels_info(std::vector<channel_info_type>& infos, 
			std::vector<channel_info_type>& deletedInfos)const;
		void get_chunk_map(const std::string& channelID, std::string& chunkMap);
		void get_piece_map(const std::string& channelID, int& from, int& to, std::string& pieceMap);
		void pop_piece_erased(const std::string& channelID, seqno_t& from, seqno_t& to, bool pop=false);

		template<typename OpenHandler>
		void open(const boost::filesystem::path& file_name, 
			size_type fileLen, size_type chunkLen, size_type pieceLen, size_type memCacheSize, 
			const OpenHandler& handler)
		{
			set_cancel();
			next_op_stamp();

			dispatch_open_helper(*this, 
				boost::bind(&this_type::do_open, this, file_name, fileLen, chunkLen, pieceLen, memCacheSize, _1), 
				handler
				);
		}

		template<typename ReadHandler>
		void read_piece(const std::string& channel_name, piece_id seqno, 
			const ReadHandler& handler)
		{
			dispatch_read_piece_helper(*this, 
				boost::bind(&this_type::__do_read_piece, this, channel_name, seqno, _1, _2), 
				handler
				);
		}

		template<typename WriteHandler>
		void write_piece(const std::string& channelID, 
			size_type totalPieceCnt, piece_id seqno, const safe_buffer& buf, 
			const WriteHandler& handler)
		{
			dispatch_write_piece_helper(*this, 
				boost::bind(&this_type::__do_write_piece, this, channelID, totalPieceCnt, seqno, buf, _1, _2), 
				handler
				);
		}

		const boost::filesystem::path& path()const
		{
			return path_;
		}
	protected:
		void do_open(const boost::filesystem::path& file_name, size_type fileLen, 
			size_type chunkLen, size_type pieceLen, size_type memCacheSize, 
			error_code& ec
			);

		void do_read_piece(const std::string& channel_name, piece_id seqno, 
			safe_buffer& buf, error_code& ec)
		{
			try{
				__do_read_piece(channel_name, seqno, buf, ec);
			}
			catch(...)
			{
				using namespace boost::system::errc;
				ec=make_error_code(boost::system::errc::io_error);
			}
		}

		void do_write_piece(const std::string& channelID, 
			size_type totalFileSize, piece_id seqno, const safe_buffer& buf, 
			std::size_t& writelen, error_code& ec)
		{
			try{
				__do_write_piece(channelID, totalFileSize, seqno, buf, writelen, ec);
			}
			catch(...)
			{
				using namespace boost::system::errc;
				ec=make_error_code(boost::system::errc::io_error);
			}
		}

		void __do_read_piece(const std::string& channel_name, piece_id seqno, 
			safe_buffer& buf, error_code& ec);

		void __do_write_piece(const std::string& channelID, 
			size_type totalFileSize, piece_id seqno, const safe_buffer& buf, 
			std::size_t& writelen, error_code& ec);

		void __do_open(const boost::filesystem::path& file_name, size_type fileLen, 
			size_type chunkLen, size_type pieceLen, size_type memCacheSize, 
			error_code& ec
			);

	private:
		/*
		检查磁盘空间是否够写file header
		*/
		bool can_write_cache_header();
		/*
		检查空间是否可文件后写入新的trunk
		*/
		bool can_write_new_chunk();
		void __load(error_code& ec);
		void __close();
		void __erase_chunk(error_code& ec, int cacheChunkID=-1);
		void __erase_channel(error_code& ec, const channel_uuid& channelID);
		void __reset_chunk(error_code& ec, int cacheChunkID);
		int __find_eraseble_chunk();
		/*
		查找与cacheChunkID关系最不密切的cacheChunkID
		*/
		int __find_eraseble_chunk(const channel_uuid& channelID, int origChunkID);
		size_type __piece_offset(const channel_uuid& channelID, piece_id seqno, channel** cl=NULL);
		char __set_seqno_bitset(chunk_id chunkID, int seqno, bool v);
		char* __get_seqno_bitset( chunk_id chunkID, int seqno);
		bool __is_seqno_bitset(chunk_id chunkID, int seqno);
		size_type __offse_in_cache_by_cache_chunkid(chunk_id cachChunkID)
		{
			return HEADER_LEN+(cachChunkID*param_.chunk_len());
		}
		size_type __offse_in_chunk(int seqno)
		{
			int cachePieceID=seqno%piece_cnt_per_chunk_;
			return cachePieceID*param_.piece_len();
		}
		disk_cache_impl::chunk_id __seqno_to_orig_chunkid(int seqno)
		{
			size_type origChunkID=(seqno*param_.piece_len())/param_.chunk_len();
			BOOST_ASSERT(origChunkID>=0);
			return (chunk_id)origChunkID;
		}
		disk_cache_impl::chunk_id __seqno_to_cache_chunkid(const channel& c, int seqno)
		{
			int origChunkID=__seqno_to_orig_chunkid(seqno);
			if (origChunkID>=c.m_orig_chunk_count||origChunkID<0)
				return INVALID_CHUNK_ID;
			int cacheChunkID=c.m_orgi_to_cache_chunk[origChunkID];
			return cacheChunkID;
		}

		size_type __modify_filesize(const boost::filesystem::path& filePath, 
			size_type fileLen, size_type chunkLen, size_type pieceLen, error_code& ec
			);
	protected:
		mutable mutex_type mutex_;

		FILE * fp_;
		boost::filesystem::path  path_;
		int32_t                  file_block_size_;

		boost::unordered_set<channel_uuid, channel_uuid::hash> deleted_channels_;
		channel_map channels_; 
		mutable boost::optional<channel_map::iterator> last_rw_channel_;
		std::vector<boost::intrusive_ptr<chunk> > chunks_;
		std::set<int> hole_chunks_;
		timed_keeper_map<channel_uuid, chunk_id> erased_chunks_;

		param param_;
		int piece_cnt_per_chunk_;
		int memcache_size_;
		atomic<int> open_state_;
		int used_chunks_cnt_;
	};

}

#endif//__ASFILE_DSK_CACHE_IMPL_HPP__
