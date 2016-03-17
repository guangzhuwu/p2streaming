#ifndef ASFIO_OS_API_HPP
#define ASFIO_OS_API_HPP

#include <p2engine/p2engine.hpp>

#include <p2engine/push_warning_option.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <boost/filesystem.hpp>
#include <p2engine/pop_warning_option.hpp>


namespace asfio{

	using namespace p2engine;

#ifdef WIN32
#	define ftell64(a)     _ftelli64(a)
#	define fseek64(a, b, c) _fseeki64(a, b, c)
	typedef __int64 off_type;
#elif SPARC
#	define ftell64(a)     ftello(a)
#	define fseek64(a, b, c) fseeko(a, b, c)
	typedef off64_t off_type;
#elif __APPLE__
#	define ftell64(a)     ftello(a)
#	define fseek64(a, b, c) fseeko(a, b, c)
#	define fstat64(a, b)   fstat(a, b)
#	define stat64 stat
	typedef off_t off_type;
#elif ANDROID
#	define ftell64(a)     ftello(a)
#	define fseek64(a, b, c) fseeko(a, b, c)
	typedef off_t off_type;
#else
#	define ftell64(a)     ftello64(a)
#	define fseek64(a, b, c) fseeko64(a, b, c)
	typedef off64_t off_type;
#endif

	typedef off_type size_type;

#if !defined ANDROID
	BOOST_STATIC_ASSERT(sizeof(ftell64(0))==8);
	BOOST_STATIC_ASSERT(sizeof(off_type)==8);
#endif

#ifdef WIN32
#include <io.h>
	inline int ftruncate64(FILE* fp, size_type _Size)
	{
		return ::_chsize_s(_fileno(fp), _Size);
	}
#else
	inline int ftruncate64(FILE* fp, size_type _Size)
	{
#	if !defined ANDROID
		return ::ftruncate64(fileno(fp), _Size);
#	else
		return ::ftruncate(fileno(fp), _Size);
#	endif
	}
#endif

	namespace detail
	{
		inline void set_error(error_code& ec)
		{
#if defined(BOOST_WINDOWS) || defined(__CYGWIN__)
			ec = boost::system::error_code(GetLastError(), 
				boost::asio::error::get_system_category());
#else
			ec = boost::system::error_code(errno, 
				boost::asio::error::get_system_category());
#endif
		}

		inline void set_no_error(error_code& ec)
		{
			ec.clear();
		}

		static const boost::uint8_t BMAP[]={0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01};
	}
	inline bool is_bit(const void*p, int bit)
	{
		using namespace detail;

		int Bn=bit/8;
		int bn=bit%8;
		const boost::uint8_t* pchar=(const boost::uint8_t*)p;
		return boost::uint8_t(pchar[Bn]&BMAP[bn])!=boost::uint8_t(0);
	}
	inline void set_bit(void*p, int bit, bool v)
	{
		using namespace detail;

		int Bn=bit/8;
		int bn=bit%8;
		boost::uint8_t* pchar=(boost::uint8_t*)p;
		if (v)
			pchar[Bn]|=BMAP[bn];
		else
			pchar[Bn]&=~BMAP[bn];
	}

	inline FILE* fileopen(const std::string& _Filename, const char * _Mode, error_code& ec)
	{
		FILE* fp = NULL;
#ifdef _MSC_VER
		fopen_s(&fp, _Filename.c_str(), _Mode);
#else
		fp=fopen(_Filename.c_str(), _Mode);
#endif
		if (!fp)
			detail::set_error(ec);
		else
			detail::set_no_error(ec);

		return fp;
	}

	inline off_type filesize(FILE* fp, error_code&ec)
	{
		off_type ret=fseek64(fp, 0, SEEK_END);
		if(ret
			||(ret=ftell64(fp))<0
			)
		{
			detail::set_error(ec);
			return -1;
		}
		else
			detail::set_no_error(ec);
		return ret;
	}

#ifdef WIN32
	inline FILE* fileopen(const std::wstring& path, const char* mode, error_code& ec)
	{
		std::string smode(mode);
		FILE* fp = NULL;
		_wfopen_s(&fp, path.c_str(), std::wstring(smode.begin(), smode.end()).c_str());
		if (!fp)
			detail::set_error(ec);
		else
			detail::set_no_error(ec);

		return fp;
	}

	inline int fileflush(FILE* fp, error_code& ec)
	{
		int ret=fflush(fp);
		int i=0;

		if(ret)
			detail::set_error(ec);
		else
			detail::set_no_error(ec);

		return ret;
	}
#	include <windows.h>
#	include <direct.h>
	inline int32_t get_file_block_size(const boost::filesystem::path& path)
	{
		std::string strRoot = path.root_path().string();
		struct _stat file_stat;
		if(_stat(strRoot.c_str(), &file_stat))
			return 1024;

		struct _diskfree_t dsk_stat;
		if(_getdiskfree(file_stat.st_dev+1, &dsk_stat))
			return 1024;

		return dsk_stat.sectors_per_cluster * dsk_stat.bytes_per_sector;
	}

	inline bool is_support_sparse_files(const boost::filesystem::path& path)
	{
		std::string strRoot = path.root_path().string();
		DWORD fileFlag = 0;

		if(!GetVolumeInformation(strRoot.c_str(), NULL, 0, NULL, NULL, &fileFlag, NULL, 0))
			return false;

		if(fileFlag&FILE_SUPPORTS_SPARSE_FILES)
			return true;
		else
			return false;
	}

#else
	inline int fileflush(FILE* fp, error_code& ec)
	{
		int ret=fflush(fp);
		int i=0;

		if(ret)
		{
			detail::set_error(ec);
			return ret;
		}
		ret = fsync(fileno(fp));
		if(ret)
			detail::set_error(ec);
		else
			detail::set_no_error(ec);

		return ret;
	}
#	include <sys/types.h> 
#	include <sys/stat.h> 
#	include <unistd.h> 
	inline int32_t get_file_block_size(const boost::filesystem::path& path)
	{
		std::string strRoot = path.root_path().string();
		struct stat file_stat;
		int ret = stat(strRoot.c_str(), &file_stat); 
		if(ret)
			return 1024;
		else 
			return file_stat.st_blksize;
	}
#endif // WIN32

	int filewrite(const void* buf, std::size_t bufLen, FILE* fp, 
		         off_type offset, int32_t block_size, error_code& ec);

	inline FILE* fileopen(const boost::filesystem::path& _Filename, 
		const char * _Mode, error_code& ec)
	{
		return fileopen(native_path_string(_Filename) , _Mode, ec);
	}

	inline int fileclose(FILE*& fp, error_code& ec)
	{
		if (fp)
		{
			FILE* fpcopy=fp;fp=NULL;
			int rst=fclose(fpcopy);
			if(rst)
				detail::set_error(ec);
			else
				detail::set_no_error(ec);

			return rst;
		}
		return 0;
	}
	inline int fileclose(FILE*& fp)
	{
		error_code ec;
		return fileclose(fp, ec);
	}

	inline int filewrite(const void* buf, std::size_t bufLen, FILE* fp, error_code& ec)
	{
		int ret=fwrite(buf, 1, bufLen, fp);
		if(ret!=bufLen)
			detail::set_error(ec);
		else
			detail::set_no_error(ec);

		return ret;
	}
	inline int fileread(void* buf, std::size_t readLen, FILE* fp, off_type offset, 
		error_code& ec)
	{
		int ret=fseek64(fp, offset, SEEK_SET);
		if(ret
			||(ret=fread(buf, 1, readLen, fp))<0
			)
		{
			detail::set_error(ec);
		}
		else
			detail::set_no_error(ec);
		return ret;
	}
	inline int fileread(void* buf, std::size_t readLen, FILE* fp, error_code& ec)
	{
		int ret=fread(buf, 1, readLen, fp);
		if(ret<0)
			detail::set_error(ec);
		else
			detail::set_no_error(ec);
		return ret;
	}

#ifdef WIN32
	typedef struct _stati64 stat_type;
#else
	typedef struct stat stat_type;
#endif
	void stat_file(const std::string& fpath, stat_type& s, error_code& ec, 
		int follow_links=false);

}

#endif//ASFIO_OS_API_HPP
