#include "asfio/disk_cache_impl.hpp"
#include "asfio/os_api.hpp"

#define  DISKCACHE_DEBUG_SCOPE(x) //x  

#define  CACHE_VERSION "Ver04"//Ver+2numbers


//#define USE_CHUNK_CACHE
//FILE*	g_fpppp = NULL;
namespace {
	static void left_shift(std::string& str, int n)
	{
		int byte_size=(int)str.size();
		if (n > byte_size * 8) n = byte_size;
		int num_bytes = n / 8;
		if (num_bytes >= byte_size)
		{
			std::memset(&str[0], 0, byte_size);
			return;
		}
		char* char_ptr=(char*)&str[0];
		if (num_bytes > 0)
		{
			std::memmove(char_ptr, char_ptr + num_bytes, byte_size - num_bytes);
			std::memset(char_ptr + byte_size - num_bytes, 0, num_bytes);
			n -= num_bytes * 8;
		}
		if (n > 0)
		{
			for (int i = 0; i < byte_size - 1; ++i)
			{
				char_ptr[i] <<= n;
				char_ptr[i] |= char_ptr[i+1] >> (8 - n);
			}
		}
	}

}

NAMESPACE_BEGIN(asfio);

#define ____TRY_LOCK(TimeMsec, ecPtr)\
	mutex_type::scoped_timed_lock lock(mutex_, boost::defer_lock_t());\
	____RE_LOCK(TimeMsec, ecPtr);

#define ____RE_LOCK(TimeMsec, ecPtr)\
	BOOST_ASSERT(!lock.owns_lock());\
	DISKCACHE_DEBUG_SCOPE(printf("LOCKing ---->, line=%d\n", __LINE__););\
	lock.timed_lock(boost::posix_time::millisec(TimeMsec));\
	if (!lock.owns_lock())\
{\
	DISKCACHE_DEBUG_SCOPE(printf("LOCK timed_out, line=%d\n", __LINE__));\
	if(ecPtr)\
	*reinterpret_cast<error_code*>(ecPtr)=boost::asio::error::timed_out;\
	return;\
}else\
	DISKCACHE_DEBUG_SCOPE(printf("LOCKed <-------, line=%d\n", __LINE__));


#define ____UN_LOCK()\
	BOOST_ASSERT(lock.owns_lock());\
	lock.unlock();

#define __GET_SPACE_INFO() \
	boost::filesystem::path parentPath=path_.parent_path();\
	if (parentPath.empty())\
	parentPath=boost::filesystem::current_path();\
	boost::filesystem::space_info spaceInfo=boost::filesystem::space(parentPath);



disk_cache_impl::disk_cache_impl(io_service& ios)
	:backstage_running_base(ios)
	, fp_(NULL)
	, memcache_size_(0)
	, piece_cnt_per_chunk_(0)
	, open_state_(CLOSED)
	, used_chunks_cnt_(0)
	, file_block_size_(-1)
{
	//chunk_info_write_cache_.set_disk_cache_impl_ptr(this);
}

disk_cache_impl::~disk_cache_impl()
{
	mutex_type::scoped_lock lock(mutex_);
	if (fp_)
	{
		error_code ec;
		fileflush(fp_, ec);
		fileclose(fp_);
	}
}

void disk_cache_impl::get_all_channels_info(std::vector<channel_info_type>& infos, 
	std::vector<channel_info_type>& deletedInfos
	)const
{
	infos.clear();

	____TRY_LOCK(10, NULL);

	BOOST_ASSERT(OPENED==open_state_);

	channel_info_type info;
	infos.reserve(channels_.size());
	for (BOOST_AUTO(itr, channels_.begin());itr!=channels_.end();++itr)
	{
		const channel& c=*(itr->second);
		BOOST_ASSERT(c.m_orig_chunk_count>0);
		int health=(int)(((double)(c.m_cached_chunks.size())/(double)c.m_orig_chunk_count)*100.0);

		itr->first.to_string(info.channel_id);
		info.health=std::min<int>(health, 100);
		infos.push_back(info);
	}
	for (BOOST_AUTO(itr, deleted_channels_.begin());itr!=deleted_channels_.end();++itr)
	{
		itr->to_string(info.channel_id);
		info.health=-1;
		deletedInfos.push_back(info);
	}
	const_cast<boost::unordered_set<channel_uuid, channel_uuid::hash>& >(deleted_channels_).clear();
}

void disk_cache_impl::get_chunk_map(const std::string& channelID, std::string& chunkMap)
{
	chunkMap.clear();

	____TRY_LOCK(10, NULL);

	BOOST_ASSERT(OPENED==open_state_);

	BOOST_AUTO(itr, channels_.find(channel_uuid(channelID)));
	if(itr!=channels_.end())
	{
		tick_type now=system_time::tick_count();
		channel& c=*(itr->second);
		if (!c.m_need_recalc_chunkmap)
		{
			chunkMap.assign(&c.m_chunkmap[0], c.m_chunkmap.size());
			return;
		}
		if (!c.m_cached_chunks.empty())
		{
			int maxChunk=*c.m_cached_chunks.rbegin();
			int mapLen=(maxChunk+8)/8;//if maxChunk=0, mapLen=1
			c.m_chunkmap.resize(mapLen, '\0');
			for (BOOST_AUTO(i, c.m_cached_chunks.begin());
				i!=c.m_cached_chunks.end();
				++i)
			{
				set_bit(&c.m_chunkmap[0], *i, true);
			}
			chunkMap.assign(&c.m_chunkmap[0], c.m_chunkmap.size());
		}
		c.m_need_recalc_chunkmap=false;
	}

}

void disk_cache_impl::get_piece_map(const std::string& channelID, 
	int& from, int& to, 
	std::string& pieceMap
	)
{
	pieceMap.clear();

	____TRY_LOCK(10, NULL);

	if (from<0||to<0||from>=to||OPENED != open_state_)
		return;

	BOOST_ASSERT(OPENED==open_state_);
	channel_uuid channelUUID(channelID);
	BOOST_AUTO(itr, channels_.find(channelUUID));
	if(itr!=channels_.end())
	{
		//8���ֽڶ���
		if (from%8!=0)
			from=(from/8)*8;

		channel& c=*(itr->second);
		if (!c.m_orig_piece_count.is_valid())
		{
			BOOST_ASSERT(0);
			error_code ec;
			__erase_channel(ec, channelUUID);
			return;
		}
		int origPieceCount=c.m_orig_piece_count.read();
		if (to>=origPieceCount)
			to=origPieceCount-1;
		if (to<0||from>=to)
			return;

		int realfrom=from;
		int realto=to;
		int gap=0;

		for (int i=0;i<2;++i)
		{
			if (realto/piece_cnt_per_chunk_!=realfrom/piece_cnt_per_chunk_)
				realto=((realfrom/piece_cnt_per_chunk_)+1)*piece_cnt_per_chunk_-1;
			BOOST_ASSERT(realto/piece_cnt_per_chunk_==realfrom/piece_cnt_per_chunk_);

			chunk_id cacheChunkID=__seqno_to_cache_chunkid(c, realfrom);
			BOOST_ASSERT(realfrom%8==0);
			if (cacheChunkID==INVALID_CHUNK_ID||cacheChunkID>=(int)chunks_.size())
				return ;
			if (!chunks_[cacheChunkID])
				return;
			chunk& ck=*chunks_[cacheChunkID];
			int offset1=realfrom%piece_cnt_per_chunk_;
			int offset2=realto%piece_cnt_per_chunk_;

			if (gap==0||pieceMap.empty())
			{
				pieceMap.append(&ck.m_piece_bitset[offset1/8], &ck.m_piece_bitset[offset2/8]+1);
			}
			else
			{
				std::string tmp(&ck.m_piece_bitset[offset1/8], &ck.m_piece_bitset[offset2/8]+1);
				if (!tmp.empty())
				{
					char c=tmp[0];
					c>>=(8-gap);
					*(pieceMap.rbegin())|=c;
					left_shift(tmp, gap);
					pieceMap+=tmp;
				}
			}

			if (realto>=to)
			{
				to=realto;
				return;
			}
			int nextTo=realto+1;
			realfrom=nextTo;
			realto=to;

			if (realfrom%8!=0)
				realfrom=((realfrom+8-1)/8)*8;
			gap=realfrom-nextTo;
		}
		to=realto;
	}
}

void disk_cache_impl::pop_piece_erased(const std::string& channelID, seqno_t& from, 
	seqno_t& to, bool pop/*=false*/)
{
	____TRY_LOCK(10, NULL);

	channel_uuid id(channelID);
	BOOST_AUTO(itr, erased_chunks_.find(id));
	if(itr == erased_chunks_.end())
		return;
	size_type origChunkID= itr->second;
	from =static_cast<seqno_t>((origChunkID * param_.chunk_len())/param_.piece_len()) ;
	to = static_cast<seqno_t>(from + param_.chunk_len()/param_.piece_len());
	if(pop)
		erased_chunks_.erase(itr);
}

void disk_cache_impl::do_open(const boost::filesystem::path& file_name, size_type fileLen, 
	size_type chunkLen, size_type pieceLen, size_type memCacheSize, 
	error_code& ec
	)
{
	____TRY_LOCK(10, &ec);
	try
	{
		__do_open(file_name, fileLen, chunkLen, pieceLen, memCacheSize, ec);
		BOOST_ASSERT(ec||used_chunks_cnt_+hole_chunks_.size()==param_.chunk_cnt_per_file());
	}
	catch (...)
	{
		BOOST_ASSERT(fp_);
		fileclose(fp_);
	}
}

size_type disk_cache_impl::__modify_filesize(const boost::filesystem::path& filePath, 
	size_type fileLen, size_type chunkLen, size_type pieceLen, error_code& ec
	)
{
	BOOST_ASSERT(!fp_);
	BOOST_ASSERT(chunkLen/pieceLen<=chunk::MAX_PIECE_COUNT);//for piece bitset 
	BOOST_ASSERT(chunkLen%pieceLen==0);
	ec.clear();
	fileLen=std::min((std::numeric_limits<size_type>::max)()-HEADER_LEN, fileLen);
	if (fileLen/chunkLen>(HEADER_LEN-PARAM_LINE_LEN)/CHUNK_INFO_LINE_LEN)
		fileLen=chunkLen*(HEADER_LEN-PARAM_LINE_LEN)/CHUNK_INFO_LINE_LEN;
	if (fileLen%chunkLen!=0)
		fileLen=(fileLen/chunkLen)*chunkLen;

	return fileLen;
}

void disk_cache_impl::__do_open(const boost::filesystem::path& file_name, 
	size_type fileLen, size_type chunkLen, 
	size_type pieceLen, 
	size_type memCacheSize, 
	error_code& ec
	)
{
#ifdef WIN32
	if(is_support_sparse_files(file_name))
	{
		file_block_size_ = get_file_block_size(file_name);
	}
	else
	{
		file_block_size_ = -1;
	}
#else
	file_block_size_ = get_file_block_size(file_name);
#endif

	if(OPENED==open_state_)
	{
		ec=asio::error::already_open;
		return;
	}
	else if(OPENING==open_state_)
	{
		ec=asio::error::in_progress;
		return;
	}

	open_state_=OPENING;

	fileLen=__modify_filesize(file_name, fileLen, chunkLen, pieceLen, ec);
	if (fileLen<=0)
	{
		using namespace boost::system::errc;
		ec=make_error_code(result_out_of_range);
		__close();
		std::cout<<"do open file len error: "<<ec.message()
			<<"----line------"<<__LINE__<<std::endl;
		return;
	}

	DISKCACHE_DEBUG_SCOPE(
		std::cout<<fileLen+HEADER_LEN<<std::endl;
	std::cout<<(std::numeric_limits<long>::max)()<<std::endl;
	std::cout<<fileLen/chunkLen<<std::endl;
	std::cout<<(HEADER_LEN-PARAM_LINE_LEN)/CHUNK_INFO_LINE_LEN<<std::endl;
	);
	BOOST_ASSERT(fileLen+HEADER_LEN<=(std::numeric_limits<size_type>::max)());//for fseek64 
	BOOST_ASSERT(fileLen/chunkLen<=(HEADER_LEN-PARAM_LINE_LEN)/CHUNK_INFO_LINE_LEN);//for chunk info
	BOOST_ASSERT(chunkLen/pieceLen<=chunk::MAX_PIECE_COUNT);//for piece bitset 
	BOOST_ASSERT(fileLen%chunkLen==0);
	BOOST_ASSERT(chunkLen%pieceLen==0);

	channels_.clear();
	chunks_.clear();
	hole_chunks_.clear();
	erased_chunks_.clear();

	path_=file_name;
	piece_cnt_per_chunk_=static_cast<int>(chunkLen/pieceLen);
	memcache_size_=static_cast<int>(memCacheSize);
	param_.file_len(fileLen);
	param_.chunk_len(chunkLen);
	param_.piece_len(pieceLen);
	param_.chunk_cnt_per_file(fileLen/chunkLen);

	const channel_uuid& invalidID=channel_uuid::invalid_uuid();
	size_type expectFileSize=fileLen+HEADER_LEN;

	try
	{
		fp_ = fileopen(path_, "rb+", ec);
		if (fp_)
		{
			param oldParam;
			size_type file_size=filesize(fp_, ec);
			if (file_size<0)
			{
				goto BAD_CACH_FILE;
			}

			if(fileread(&oldParam.m_data[0], param::SIZE_ON_DSK, fp_, 0, ec)<0
				||oldParam.version()[5]!='\0'||!oldParam.is_valid()
				||strcmp(oldParam.version(), CACHE_VERSION)!=0
				||oldParam.file_len()<=0 || oldParam.chunk_len()*oldParam.chunk_cnt_per_file()!=oldParam.file_len()
				||oldParam.chunk_len()!=param_.chunk_len()||oldParam.piece_len()!=param_.piece_len()
				)
			{
				goto BAD_CACH_FILE;
			}

			if (file_size>expectFileSize)
				ftruncate64(fp_, expectFileSize);

			if (oldParam.file_len()!=param_.file_len())
			{
				if(filewrite(&param_.m_data[0], param::SIZE_ON_DSK, fp_, 0, file_block_size_, ec)<0)
				{
					goto BAD_CACH_FILE;
				}

				int trimChunkIDMin=(int)oldParam.chunk_cnt_per_file();
				int trimChunkIDMax=(int)param_.chunk_cnt_per_file();
				if (trimChunkIDMin>trimChunkIDMax)
					std::swap(trimChunkIDMin, trimChunkIDMax);
				for(int i=std::max(trimChunkIDMin-1, 0);i<trimChunkIDMax;++i)
				{
					__reset_chunk(ec, i);
					if (ec)
					{
						goto BAD_CACH_FILE;
					}
				}

				fileflush(fp_, ec);

				if(ec)
				{
					std::cout<<"---------load cache file, update cache data error--------"<<__LINE__<<std::endl;
					goto BAD_CACH_FILE;
				}
			}

			//load
			fileclose(fp_);

			__load(ec);

			if (!ec)
				open_state_ = OPENED;
			return;
		}
	}
	catch (...)
	{
		std::cout<<" do open cache file , catch error :"<<__FILE__<<" line :"<<__LINE__<<std::endl;
	}

BAD_CACH_FILE:
	fileclose(fp_);

	if(boost::filesystem::status(path_).type() 
		!= boost::filesystem::file_not_found)
	{
		boost::filesystem::remove(path_);
	}

	//////////////////////////////////////////////////////////////
	// create a new disk file.
	//////////////////////////////////////////////////////////////
	ec.clear();

	size_type chunk_cnt_per_file=param_.chunk_cnt_per_file();
	size_type file_size;

	if(!can_write_cache_header())
	{
		std::cout<<"free space is not enough"<<std::endl;
		goto FAILED_OPEN;
	}

	strcpy(param_.version(), CACHE_VERSION);
	fp_ = fileopen(path_, "wb+", ec);
	if (!fp_||filewrite(&param_.m_data[0], param::SIZE_ON_DSK, fp_, ec)<0)
		goto FAILED_OPEN;

	chunk_cnt_per_file=param_.chunk_cnt_per_file();
	for(int i=0;i<chunk_cnt_per_file;++i)
	{
		__reset_chunk(ec, i);
		if (ec)
			goto FAILED_OPEN;
	}

	file_size = filesize(fp_, ec);
	if(ec)
		goto FAILED_OPEN;

	if(file_size < HEADER_LEN)
		filewrite("\0", 1, fp_, HEADER_LEN, file_block_size_, ec);

	if(ec)
		goto FAILED_OPEN;

	if(fileflush(fp_, ec)<0)
		goto FAILED_OPEN;

	for (int i=0;i<chunk_cnt_per_file;++i)
		hole_chunks_.insert(i);

	chunks_.resize((size_t)chunk_cnt_per_file);
	open_state_ = OPENED;
	return;

FAILED_OPEN:
	__close();
	std::cout<<" FAILED_OPEN "<<" file :"<<__FILE__<<" line :"<<__LINE__<<std::endl;
}

void disk_cache_impl::__close()
{
	used_chunks_cnt_=0;
	channels_.clear();
	chunks_.clear();
	hole_chunks_.clear();
	erased_chunks_.clear();
	open_state_=CLOSED;
	fileclose(fp_);
}

void disk_cache_impl::__do_read_piece(const std::string& channel_name, 
	piece_id seqno, safe_buffer& buf, error_code& ec)
{
	____TRY_LOCK(10, &ec);

	if (open_state_!=OPENED)
	{
		using namespace boost::system::errc;
		ec=make_error_code(bad_file_descriptor);

		if(in_probability(0.1))
		{
			try
			{
				error_code err;
				__do_open(path_, param_.file_len(), param_.chunk_len(), param_.piece_len(), 
					1*1024*1024, err);
			}
			catch (...)
			{
			}
		}

		if (open_state_!=OPENED)
			return;
	}
	channel* cl=NULL;
	channel_uuid channelUid(channel_name);
	off_type offset=-1;
	if (open_state_!=OPENED
		||!fp_
		||(offset=__piece_offset(channelUid, seqno, &cl))<0
		)
	{
		ec=asio::error::not_found;
	}
	else
	{
		BOOST_ASSERT(offset>=HEADER_LEN);
		buf.resize((int)param_.piece_len());

		FILE * fp=fp_;
		____UN_LOCK();
		if (fileread(buffer_cast<char*>(buf), buf.size(), fp, offset, ec)<0)
		{
			____RE_LOCK(10, &ec);
			__close();
		}
		else
		{
			____RE_LOCK(10, &ec);

			char* p=buffer_cast<char*>(buf);

			int32_t pieceLen;
			verifiable_int<int32_t> pieceRealLen, seqno32;//do not use read_int32_ntoh
			memcpy(&pieceRealLen, p, sizeof(pieceRealLen));
			p+=sizeof(pieceRealLen);
			memcpy(&seqno32, p, sizeof(seqno32));
			p+=sizeof(seqno32);
			if (!pieceRealLen.is_valid()||!seqno32.is_valid()
				||(pieceLen=pieceRealLen.read())<0||seqno32.read()!=seqno
				||pieceLen>int(param_.piece_len()-(sizeof(pieceRealLen)+sizeof(seqno32)))
				)
			{
				int cacheChunkID=__seqno_to_cache_chunkid(*cl, seqno);
				__set_seqno_bitset(cacheChunkID, seqno, false);
				ec=asio::error::not_found;
				return;
			}
			buf=buf.buffer_ref(sizeof(pieceRealLen)+sizeof(seqno32), pieceLen);
		}
	}
}

void disk_cache_impl::__do_write_piece(const std::string& channelID, 
	size_type totalPieceCnt, piece_id seqno, const safe_buffer& buf, 
	std::size_t& writelen, error_code& ec)
{
	using namespace boost::system::errc;

	bool needWriteChannelID=false;
	bool needWriteChunkBitset=false;
	channel* cl=NULL;
	chunk* ck=NULL;
	channel_uuid channelUuid(channelID);
	writelen=0;

	____TRY_LOCK(10, &ec);

	if (param_.chunk_cnt_per_file()<=0)
	{
		ec=make_error_code(bad_file_descriptor);
		return;
	}

	if (open_state_!=OPENED)
	{
		ec=make_error_code(bad_file_descriptor);

		if(in_probability(0.1))
		{
			DISKCACHE_DEBUG_SCOPE(std::cout<<"in_probability __do_open, "<<__LINE__<<std::endl;);
			error_code err;
			__do_open(path_, param_.file_len(), param_.chunk_len(), param_.piece_len(), 
				1*1024*1024, err);
			DISKCACHE_DEBUG_SCOPE(std::cout<<"__do_open finished "<<__LINE__<<std::endl;);
		}

		if (open_state_!=OPENED)
		{
			return;
		}
	}

	BOOST_AUTO(itr, channels_.find(channelUuid));
	if (itr!=channels_.end())
	{
		last_rw_channel_=itr;
		cl=(itr->second).get();
		BOOST_ASSERT(cl);
		if (!cl->m_orig_piece_count.is_valid())
		{
			DISKCACHE_DEBUG_SCOPE(std::cout<<"!cl->m_orig_piece_count.is_valid() __erase_channel, "<<__LINE__<<std::endl;);
			error_code ec;
			__erase_channel(ec, channelUuid);
			return;
		}
		if(seqno >= cl->m_orig_piece_count.read())
		{
			std::cout<<"error: cache write seqno is larger than the channel piece count "<<std::endl;
			return;
		}

		int cacheChunkID=__seqno_to_cache_chunkid(*cl, seqno);
		if (cacheChunkID!=INVALID_CHUNK_ID)
			ck=chunks_[cacheChunkID].get();
	}

	if (!ck)
	{
		if (used_chunks_cnt_==param_.chunk_cnt_per_file())
		{
			__erase_chunk(ec);
			if(ec||used_chunks_cnt_==param_.chunk_cnt_per_file()||param_.chunk_cnt_per_file()==0)
				return;
			BOOST_ASSERT(used_chunks_cnt_<param_.chunk_cnt_per_file());
			____UN_LOCK();
			DISKCACHE_DEBUG_SCOPE(std::cout<<"recursion __do_write_piece "<<__LINE__<<std::endl;);
			__do_write_piece(channelID, totalPieceCnt, seqno, buf, writelen, ec);
			return;
		}
		else
		{
			size_type fileSize=filesize(fp_, ec);

			if (ec)
				return;

			if(hole_chunks_.empty()
				||(fileSize-HEADER_LEN-(*hole_chunks_.begin()+1)*param_.chunk_len()<0)
				)
			{
				____UN_LOCK();

				if(can_write_new_chunk())
				{
					filewrite("\0", 1, fp_, fileSize+param_.chunk_len()-1, file_block_size_, ec);

					if(!ec)
						fileflush(fp_, ec);

					if(ec)
						return;

					fileSize = filesize(fp_, ec);

					____RE_LOCK(10, &ec);
				}
				else
				{
					____RE_LOCK(10, &ec);

					if (used_chunks_cnt_==0)
					{
						return;
					}

					ec.clear();
					int origChunkID=__seqno_to_orig_chunkid(seqno);
					int erase_cache_chunk_id = __find_eraseble_chunk(channelUuid, origChunkID);
					__erase_chunk(ec, erase_cache_chunk_id);

					if(ec||used_chunks_cnt_==param_.chunk_cnt_per_file()||param_.chunk_cnt_per_file()==0)
					{
						std::cout<<" ------do write piece return-------line---"<<__LINE__<<std::endl;
						return;
					}

					BOOST_ASSERT(used_chunks_cnt_<param_.chunk_cnt_per_file());

					____UN_LOCK();
					DISKCACHE_DEBUG_SCOPE(std::cout<<"recursion __do_write_piece "<<__LINE__<<std::endl;);
					__do_write_piece(channelID, totalPieceCnt, seqno, buf, writelen, ec);
					return;
				}
			}

			if (hole_chunks_.empty()
				||(fileSize-HEADER_LEN-(*hole_chunks_.begin() + 1)*param_.chunk_len()<0)
				)
			{
				std::cout<<"do_write_piece---line---"<<__LINE__<<std::endl;
				return;
			}

			chunk_id cacheChunkID=*hole_chunks_.begin();
			hole_chunks_.erase(hole_chunks_.begin());
			if (!cl)
			{
				boost::intrusive_ptr<channel> chl(new channel(totalPieceCnt, 
					param_.chunk_cnt_per_file(), piece_cnt_per_chunk_));
				itr=(channels_.insert(std::make_pair(channelUuid, chl))).first;
				cl=chl.get();
			}
			int origChunkID=__seqno_to_orig_chunkid(seqno);
			cl->m_cache_to_orig_chunk[cacheChunkID]=origChunkID;
			cl->m_orgi_to_cache_chunk[origChunkID]=cacheChunkID;

			chunks_[cacheChunkID].reset(new chunk(__seqno_to_orig_chunkid(seqno), 0));
			ck=chunks_[cacheChunkID].get();
			++used_chunks_cnt_;
		}
		cl->m_cached_chunks.insert(__seqno_to_orig_chunkid(seqno));
		cl->m_need_recalc_chunkmap=true;
		needWriteChannelID=true;
		needWriteChunkBitset=true;
	}

	BOOST_ASSERT(cl&&ck);

	int origChunkID=__seqno_to_orig_chunkid(seqno);
	int cacheChunkID=__seqno_to_cache_chunkid(*cl, seqno);
	cl->m_cache_to_orig_chunk[cacheChunkID]=origChunkID;
	cl->m_orgi_to_cache_chunk[origChunkID]=cacheChunkID;

	char bitset=0;
	if (!__is_seqno_bitset(cacheChunkID, seqno))
	{
		if (!ck->m_cached_piece_count.is_valid())
		{
			BOOST_ASSERT(0);
			error_code ec;
			__erase_chunk(ec, cacheChunkID);
			return;
		}
		ck->m_cached_piece_count.write(ck->m_cached_piece_count.read()+1);
		bitset=__set_seqno_bitset(cacheChunkID, seqno, true);
		BOOST_ASSERT(used_chunks_cnt_<=(int)chunks_.size());
		needWriteChunkBitset=true;
	}

	size_type offset= __offse_in_cache_by_cache_chunkid(cacheChunkID)+__offse_in_chunk(seqno);
	verifiable_int<int32_t> dataLen, seqno32;
	dataLen.write((int32_t)buffer_size(buf));
	seqno32.write((int32_t)seqno);

	FILE* fp=fp_;
	____UN_LOCK();
	BOOST_ASSERT(dataLen.read()>0);

	if(fseek64(fp, offset, SEEK_SET)
		||fwrite(&dataLen, 1, sizeof(dataLen), fp)!=sizeof(dataLen)
		||fwrite(&seqno32, 1, sizeof(seqno32), fp)!=sizeof(seqno32)
		||fwrite(buffer_cast<const char*>(buf), 1, buffer_size(buf), fp)!=buffer_size(buf)
		)
	{
		writelen=0;

		detail::set_error(ec);
		std::cout<<"---do write piece write buf error----"<<ec.message()
			<<"----line----"<<__LINE__<<std::endl;

		__close();
		return;
	}

	BOOST_ASSERT(offset+sizeof(dataLen)+sizeof(seqno32)+buffer_size(buf)
		<=HEADER_LEN+(cacheChunkID+1)*param_.chunk_len());

	if (needWriteChannelID)
	{
		____RE_LOCK(10, &ec);
		BOOST_AUTO(_m_orig_piece_count, cl->m_orig_piece_count);
		BOOST_AUTO(_m_orig_chunk_id, ck->m_orig_chunk_id);
		BOOST_AUTO(_m_cached_piece_count, ck->m_cached_piece_count);
		BOOST_AUTO(_m_read_times, ck->m_read_times);
		char _m_piece_bitset[sizeof(ck->m_piece_bitset)];
		memcpy(_m_piece_bitset, ck->m_piece_bitset, sizeof(ck->m_piece_bitset));
		____UN_LOCK();
		int pos=(PARAM_LINE_LEN+cacheChunkID*CHUNK_INFO_LINE_LEN);

		if(fseek64(fp, pos, SEEK_SET)
			||fwrite(&channelUuid[0], 1, channel_uuid::SIZE_ON_DSK, fp)!=channel_uuid::SIZE_ON_DSK
			||fwrite(&_m_orig_piece_count, 1, sizeof(_m_orig_piece_count), fp)!=sizeof(_m_orig_piece_count)
			||fwrite(&(_m_orig_chunk_id), 1, sizeof(_m_orig_chunk_id), fp)!=sizeof(_m_orig_chunk_id)
			||fwrite(&(_m_cached_piece_count), 1, sizeof(_m_cached_piece_count), fp)!=sizeof(_m_cached_piece_count)
			||fwrite(&(_m_read_times), 1, sizeof(_m_read_times), fp)!=sizeof(_m_read_times)
			||fwrite(&(_m_piece_bitset), 1, sizeof(_m_piece_bitset), fp)!=sizeof(_m_piece_bitset)
			)
		{
			detail::set_error(ec);
			std::cout<<"---do write piece write channel error----"<<ec.message()
				<<"----line----"<<__LINE__<<std::endl;

			writelen=0;
			__close();	
			return;
		}
	}
	else if (needWriteChunkBitset)
	{
		if (bitset==char(0xff)||seqno==totalPieceCnt)
		{
			____RE_LOCK(10, &ec);

			size_type basePos=(PARAM_LINE_LEN+cacheChunkID*CHUNK_INFO_LINE_LEN)
				+channel_uuid::SIZE_ON_DSK
				+channel::SIZE_ON_DSK;
			std::pair<int, int> offset_len=chunk::cached_piece_count_offset_len();
			size_type pos=basePos+offset_len.first;
			BOOST_AUTO(_m_cached_piece_count, ck->m_cached_piece_count);

			____UN_LOCK();
			ec.clear();
			filewrite(&_m_cached_piece_count, offset_len.second, fp, pos, file_block_size_, ec);
			if(ec)
				std::cout<<"do write piece write cache piece count error: "<<ec.message()
				<<"---------line-------"<<__LINE__<<std::endl;

			____RE_LOCK(10, &ec);
			if (ec)
			{
				writelen=0;
				__close();
				return;
			}

			offset_len=chunk::cache_piece_bitset_offset_len(seqno%piece_cnt_per_chunk_);
			pos=basePos+offset_len.first;
			____UN_LOCK();
			filewrite(&bitset, offset_len.second, fp, pos, file_block_size_, ec);
			____RE_LOCK(10, &ec);
			if (ec)
			{
				std::cout<<"do write piece write bitset error: "<<ec.message()
					<<"---------line-------"<<__LINE__<<std::endl;

				__close();
				writelen=0;
				return;
			}
		}
	}

	writelen=buf.length();
}

bool disk_cache_impl::can_write_cache_header()
{
	try{
		__GET_SPACE_INFO();
		if ((int64_t)spaceInfo.available > HEADER_LEN + 3*param_.chunk_len())
			return true;
	}
	catch(...){}

	return false;
}

bool disk_cache_impl::can_write_new_chunk()
{
	try{
		__GET_SPACE_INFO();
		if ((int64_t)spaceInfo.available > param_.chunk_len())
			return true;
	}
	catch(...){}

	return false;
}

void disk_cache_impl::__load(error_code& ec)
{
	param oldParam;
	boost::scoped_array<char> buf(new char[CHUNK_INFO_LINE_LEN]);
	size_type file_size=-1;

	fp_ = fileopen(path_, "rb+", ec);
	if(!fp_||(file_size = filesize(fp_, ec))<0)
		goto BAD_CACH_FILE;

	if(fileread(&oldParam.m_data[0], param::SIZE_ON_DSK, fp_, 0, ec)<0)
		goto BAD_CACH_FILE;

	BOOST_ASSERT(oldParam.file_len()==param_.file_len());
	BOOST_ASSERT(oldParam.chunk_len()==param_.chunk_len());
	BOOST_ASSERT(oldParam.piece_len()==param_.piece_len());
	BOOST_ASSERT(oldParam.chunk_cnt_per_file()==param_.chunk_cnt_per_file());

	chunks_.resize((int)param_.chunk_cnt_per_file());
	for (int i=0;i<(int)param_.chunk_cnt_per_file();++i)
	{
		if(fileread(&buf[0], CHUNK_INFO_LINE_LEN, fp_, ec)<0)
			goto BAD_CACH_FILE;
		const char* p= &buf[0];
		channel_uuid id(p);
		if (id!=channel_uuid::invalid_uuid())
		{
			boost::intrusive_ptr<channel>& chl=channels_[id];
			if (!chl)
			{
				bool error=false;
				chl.reset(new channel(p, param_.chunk_cnt_per_file(), piece_cnt_per_chunk_, error));
				if (error)
				{
					channels_.erase(id);
					hole_chunks_.insert(i);
					continue;
				}
			}
			else
			{
				p+=channel::SIZE_ON_DSK;
			}

			bool good=true;
			boost::intrusive_ptr<chunk> chk(new chunk(p, good));
			if (!good)
			{
				if(chl->m_cached_chunks.empty())
				{
					channels_.erase(id);
					hole_chunks_.insert(i);
				}
				continue;
			}
			int16_t chkOrigChunkID;
			if (!chk->m_orig_chunk_id.is_valid()
				||(chkOrigChunkID=chk->m_orig_chunk_id.read())<0
				||chl->m_orig_chunk_count<=chkOrigChunkID
				)
			{
				if(chl->m_cached_chunks.empty())
				{
					channels_.erase(id);
					hole_chunks_.insert(i);
				}
				continue;
			}

			chunks_[i]=chk;
			++used_chunks_cnt_;

			chl->m_cache_to_orig_chunk[i]=chkOrigChunkID;
			chl->m_orgi_to_cache_chunk[chkOrigChunkID]=i;
			chl->m_cached_chunks.insert(chkOrigChunkID);
			chl->m_need_recalc_chunkmap=true;
		}
		else
		{
			hole_chunks_.insert(i);
		}
	}
	return;

BAD_CACH_FILE:
	__close();
	std::cout<<"--------BAD_CACH_FILE--------line:"<<__LINE__<<std::endl;
}

void disk_cache_impl::__erase_channel(error_code& ec, const channel_uuid& channelID  )
{
	BOOST_AUTO(itr, channels_.find(channelID));
	if (itr!=channels_.end())
	{
		channel& c=*(itr->second);
		std::vector<int> cacheChunkIDs;
		cacheChunkIDs.reserve(c.m_cached_chunks.size());
		for (BOOST_AUTO(chunkItr, c.m_cached_chunks.begin());
			chunkItr!=c.m_cached_chunks.end();
			++chunkItr)
		{
			int origChunkID=*chunkItr;
			if (origChunkID>0&&origChunkID<c.m_orig_chunk_count)
			{			
				int cacheChunkID=c.m_orgi_to_cache_chunk[origChunkID];
				cacheChunkIDs.push_back(cacheChunkID);
			}
		}
		BOOST_FOREACH(int cacheChunkID, cacheChunkIDs)
		{
			DISKCACHE_DEBUG_SCOPE(std::cout<<"__erase_chunk "<<cacheChunkID<<" , line="<<__LINE__<<std::endl;);
			__erase_chunk(ec, cacheChunkID);
		}

		itr=channels_.find(channelID);
		if (itr!=channels_.end())
			channels_.erase(itr);
	}
}
void disk_cache_impl::__erase_chunk(error_code& ec, int cacheChunkID)
{
	if (0 == used_chunks_cnt_)
		return;

	if (cacheChunkID<0)
		cacheChunkID=__find_eraseble_chunk();

	BOOST_ASSERT(cacheChunkID>=0);

	DISKCACHE_DEBUG_SCOPE(
		std::cout<<"XXXXXXX---erase chunk: "<<cacheChunkID<<std::endl;
	);

	if (cacheChunkID>param_.chunk_cnt_per_file())
	{
		return;
	}

	channel_uuid id;
	if(fileread(&id[0], channel_uuid::SIZE_ON_DSK, 
		fp_, PARAM_LINE_LEN+cacheChunkID*CHUNK_INFO_LINE_LEN, ec)<0
		)
	{
		std::cout<<" __erase_chunk error "<<__LINE__
			<<" error : "<<ec.message()<<std::endl;

		__close();
		return;
	}

	BOOST_AUTO(itr, channels_.find(id));
	if (itr!=channels_.end())
	{
		channel& c=*(itr->second);
		if (cacheChunkID<c.m_orig_chunk_count
			&&c.m_cache_to_orig_chunk[cacheChunkID]<c.m_orig_chunk_count
			)
		{
			erased_chunks_.try_keep(std::make_pair(id, c.m_cache_to_orig_chunk[cacheChunkID])
				, seconds(5));
			c.m_cached_chunks.erase(c.m_cache_to_orig_chunk[cacheChunkID]);
			c.m_need_recalc_chunkmap=true;
			c.m_orgi_to_cache_chunk[c.m_cache_to_orig_chunk[cacheChunkID] ]=INVALID_CHUNK_ID;
			c.m_cache_to_orig_chunk[cacheChunkID]=INVALID_CHUNK_ID;

			if (c.m_cached_chunks.empty())
			{
				deleted_channels_.insert(itr->first);
				channels_.erase(itr);
			}
		}
	}

	chunks_[cacheChunkID].reset();
	hole_chunks_.insert(cacheChunkID);

	__reset_chunk(ec, cacheChunkID);

	if (ec) 
	{
		std::cout<<" __erase_chunk error: "<<ec.message()
			<<"line :"<<__LINE__<<std::endl;
		__close();
		return;
	}
	--used_chunks_cnt_;
}

void disk_cache_impl::__reset_chunk(error_code& ec, int cacheChunkID)
{
	char zerobuf[CHUNK_INFO_LINE_LEN];
	memset(zerobuf, 0, CHUNK_INFO_LINE_LEN);
	filewrite(zerobuf, CHUNK_INFO_LINE_LEN, 
		fp_, PARAM_LINE_LEN+cacheChunkID*CHUNK_INFO_LINE_LEN, file_block_size_, ec);
}

int disk_cache_impl::__find_eraseble_chunk()
{
	size_t minChunkCnt=0xffffff;
	BOOST_AUTO(theItr, channels_.end());
	for(BOOST_AUTO(itr, channels_.begin());itr!=channels_.end();++itr)
	{
		if (last_rw_channel_&&itr==*last_rw_channel_)
			continue;
		if (minChunkCnt>itr->second->m_cached_chunks.size()
			||minChunkCnt==itr->second->m_cached_chunks.size()&&in_probability(0.5)
			)
		{
			minChunkCnt=itr->second->m_cached_chunks.size();
			theItr=itr;
			BOOST_ASSERT(theItr->second);
		}
	}
	if (theItr!=channels_.end()&&theItr->second->m_cached_chunks.size())
	{
		const channel &c=*(theItr->second);
		if (*c.m_cached_chunks.begin()<c.m_orig_chunk_count)
			return c.m_orgi_to_cache_chunk[*c.m_cached_chunks.begin()];
	}
	return random(0, (int)chunks_.size()-(int)hole_chunks_.size());
}

int disk_cache_impl::__find_eraseble_chunk(const channel_uuid& channelID, int origChunkID)
{
	size_t minChunkCnt=0xffffff;
	BOOST_AUTO(theItr, channels_.end());
	for(BOOST_AUTO(itr, channels_.begin());itr!=channels_.end();++itr)
	{
		if (itr->first==channelID)
			continue;

		if (minChunkCnt>itr->second->m_cached_chunks.size()
			||minChunkCnt==itr->second->m_cached_chunks.size()&&in_probability(0.5)
			)
		{
			minChunkCnt=itr->second->m_cached_chunks.size();
			theItr=itr;
			BOOST_ASSERT(theItr->second);
		}
	}

	if (theItr!=channels_.end()&&theItr->second->m_cached_chunks.size())
	{
		const channel &c=*(theItr->second);
		if (*c.m_cached_chunks.begin()<c.m_orig_chunk_count)
			return c.m_orgi_to_cache_chunk[*c.m_cached_chunks.begin()];
	}

	theItr = channels_.find(channelID);
	if(channels_.end() != theItr)
	{
		const channel &c=*(theItr->second);
		const std::set<uint16_t>& cache_chunks = c.m_cached_chunks;
		if(cache_chunks.size() > 0)
		{
			int sel_trunk_id = *cache_chunks.begin();
			BOOST_FOREACH(uint16_t cache_id, cache_chunks)
			{
				if(abs(sel_trunk_id - origChunkID)
					<abs(cache_id- origChunkID))
					sel_trunk_id = cache_id;
			}
			return c.m_orgi_to_cache_chunk[sel_trunk_id];;
		}

	}

	return -1;
}

size_type disk_cache_impl::__piece_offset(const channel_uuid& channelID, piece_id seqno, 
	channel** cl)
{
	if (cl) 
		*cl=NULL;
	BOOST_AUTO(itr, channels_.find(channelID));
	if (itr!=channels_.end())
	{
		last_rw_channel_=itr;
		const channel& c=*(itr->second);
		int origChunkID= __seqno_to_orig_chunkid(seqno);
		if (origChunkID>=c.m_orig_chunk_count)
		{
			BOOST_ASSERT(0);
			return -1;
		}
		int cacheChunkID=c.m_orgi_to_cache_chunk[origChunkID];
		if (cacheChunkID!=INVALID_CHUNK_ID)
		{
			if (!__is_seqno_bitset(cacheChunkID, seqno))
				return -1;
			int cachePieceID=seqno%piece_cnt_per_chunk_;
			if (cl)
				*cl=const_cast<channel*>(&c);
			return __offse_in_cache_by_cache_chunkid(cacheChunkID)+__offse_in_chunk(seqno);
		}
	}
	return -1;
}

char disk_cache_impl::__set_seqno_bitset(chunk_id chunkID, int seqno, bool v)
{
	int cachePieceID=seqno%piece_cnt_per_chunk_;
	set_bit(chunks_[chunkID]->m_piece_bitset, cachePieceID, v);
	return chunks_[chunkID]->m_piece_bitset[cachePieceID/8];
}

bool disk_cache_impl::__is_seqno_bitset(chunk_id chunkID, int seqno)
{
	int cachePieceID=seqno%piece_cnt_per_chunk_;
	return is_bit(chunks_[chunkID]->m_piece_bitset, cachePieceID);
}

char* disk_cache_impl::__get_seqno_bitset(chunk_id chunkID, int seqno)
{
	int cachePieceID=seqno%piece_cnt_per_chunk_;
	return &(chunks_[chunkID]->m_piece_bitset[cachePieceID/8]);
}

#if 0
/************************************************************************/
/* add zyliu 2012-2-6   {{{                                             */
/************************************************************************/

void disk_cache_impl::ck_cache::reset(disk_cache_impl* p_disk_cache_impl, int chunk_id, 
	error_code& ec)
{
	BOOST_ASSERT(chunk_id != cache_chunk_id);
	//fprintf(g_fpppp, "new chunk_id: %u  old_chunk_id: %u\n", chunk_id, cache_chunk_id);
	if( cache_chunk_id != INVALID_CHUNK_ID && cache_count != 0)
	{
		//fprintf(g_fpppp, "fflush cache\n");
		write_to_disk(p_disk_cache_impl, ec);
	}
	cache_chunk_id = chunk_id;
	clear_cache_status();
	//cache_count = 0;
	//max_seqno = INVALID_SEQNO;
	//min_seqno = INVALID_SEQNO;
}

void disk_cache_impl::ck_cache::write_to_cache(disk_cache_impl* p_disk_cache_impl, 
	int chunk_id, uint32_t cache_total, uint32_t seqno, error_code& ec)
{
	BOOST_ASSERT(p_disk_cache_impl);

	if( max_seqno == INVALID_SEQNO || min_seqno == INVALID_SEQNO)
	{
		max_seqno = min_seqno = seqno;
	}
	else
	{
		if( seqno > max_seqno)
		{
			max_seqno = seqno;
		}
		else if( seqno < min_seqno)
		{
			min_seqno = seqno;
		}
	}
	cache_count++;
	cached_count_total = cache_total;
	if( cache_count >= COUNT_TO_WRITE_DSK)
	{
		write_to_disk(p_disk_cache_impl, ec);
	}
	else
	{
		DISKCACHE_DEBUG_SCOPE(;
		DISKCACHE_DEBUG_SCOPE std::cout<<"index: "<<index<<" cache_count: "<<cache_count<<std::endl;
		//fprintf(g_fpppp, "index: %u cache_count: %u  seqno: %u\n", index, cache_count, seqno);
		);
	}
}

void disk_cache_impl::ck_cache::write_to_disk(disk_cache_impl* p_disk_cache_impl, error_code& ec)
{
	BOOST_ASSERT(p_disk_cache_impl);

	//chunk*	ck=NULL;
	//if (cache_chunk_id != INVALID_CHUNK_ID){
	//	ck=p_disk_cache_impl->chunks_[cache_chunk_id].get();
	//}
	//BOOST_ASSERT(ck);
	if (p_disk_cache_impl->is_open())
	{
		char* write_bitset_begin = p_disk_cache_impl->__get_seqno_bitset(cache_chunk_id, min_seqno);
		char* write_bitset_end = p_disk_cache_impl->__get_seqno_bitset(cache_chunk_id, max_seqno);
		BOOST_ASSERT(write_bitset_begin);
		BOOST_ASSERT(write_bitset_end);
		int write_len = (int)(write_bitset_end - write_bitset_begin) + 1;

		size_type basePos=(PARAM_LINE_LEN+cache_chunk_id*CHUNK_INFO_LINE_LEN)+channel_uuid::SIZE_ON_DSK+channel::SIZE_ON_DSK;
		std::pair<int, int> offset_len=chunk::cached_piece_count_offset_len();
		size_type pos=basePos+offset_len.first;

		uint32_t _m_cached_piece_count = cached_count_total;
		filewrite(&(_m_cached_piece_count), offset_len.second, p_disk_cache_impl->fp_, pos, ec);

		offset_len=chunk::cache_piece_bitset_offset_len(min_seqno%p_disk_cache_impl->piece_cnt_per_chunk_);
		pos=basePos+offset_len.first;
		filewrite(write_bitset_begin, write_len, p_disk_cache_impl->fp_, pos, ec);
	}

	clear_cache_status();
}
void disk_cache_impl::ck_cache::clear_cache_status(void)
{
	cache_count = 0;
	min_seqno = INVALID_SEQNO;
	max_seqno = INVALID_SEQNO;
}

void disk_cache_impl::chunk_info_write_cache::cache_write( int cache_chunk_id, 
	uint32_t cache_total, uint32_t seqno, error_code& ec)
{
	BOOST_ASSERT(p_disk_cache_impl);

	____TRY_LOCK(10, &ec);

	ck_cache*	p_ck_cache = NULL;
	if( last_opt_cache)
	{
		if( last_opt_cache->is_match_chunk_id(cache_chunk_id))
		{
			p_ck_cache = last_opt_cache;
			____UN_LOCK();
		}
		else
		{
			if( last_opt_cache == &ck_cache_[0])
			{
				p_ck_cache = &ck_cache_[1];
			}
			else
			{
				p_ck_cache = &ck_cache_[0];
			}
			____UN_LOCK();

			if( !p_ck_cache->is_match_chunk_id(cache_chunk_id))
			{
				p_ck_cache->reset(p_disk_cache_impl, cache_chunk_id, ec);
			}
		}
	}
	else
	{
		p_ck_cache = &ck_cache_[0];
		____UN_LOCK();
		p_ck_cache->reset(p_disk_cache_impl, cache_chunk_id, ec);
	}

	BOOST_ASSERT(p_ck_cache);
	p_ck_cache->write_to_cache(p_disk_cache_impl, cache_chunk_id, cache_total, seqno, ec);

	____RE_LOCK(10, &ec);
	last_opt_cache = p_ck_cache;
}
/************************************************************************/
/* }}}add zyliu 2012-2-6                                                */
/************************************************************************/
#endif//0

NAMESPACE_END(asfio);


