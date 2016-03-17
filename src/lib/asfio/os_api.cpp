#include "asfio/os_api.hpp"

namespace asfio{

	void stat_file(const std::string& fpath, stat_type& ret, error_code& ec, int follow_links)
	{
		ec.clear();
#ifdef BOOST_WINDOWS_API
		// apparently windows doesn't expect paths
		// to directories to ever end with a \ or /
		std::string f=fpath;
		if (!f.empty() && (f[f.size() - 1] == '\\'
			|| f[f.size() - 1] == '/'))
			f.resize(f.size() - 1);
#else
		const std::string& f=fpath;
#endif

#ifdef BOOST_WINDOWS_API
		if (_stati64(f.c_str(), &ret) < 0)
		{
			ec.assign(errno, boost::system::get_generic_category());
			return;
		}
#else
		int retval;
		if (!follow_links)
			retval = ::lstat(f.c_str(), &ret);
		else
			retval = ::stat(f.c_str(), &ret);
		if (retval < 0)
		{
			ec.assign(errno, boost::system::get_generic_category());
			return;
		}
#endif // TORRENT_WINDOWS

	}

	int filewrite(const void* buf, std::size_t bufLen, FILE* fp, 
		off_type offset, int32_t block_size, error_code& ec)
	{
		int ret= 0;

		if(block_size <= 0)
		{
			ret = fseek64(fp, offset, SEEK_SET);
		}
		else 
		{
			off_type file_size = filesize(fp, ec);
			if(ec)
				return -1;

			off_type tem_off = file_size;
			ret = fseek64(fp, 0, SEEK_END);

			if(ret)
			{
				detail::set_error(ec);
				return ret;
			}

			while(offset-tem_off > (int32_t)block_size)
			{
				if(ret = fseek64(fp, block_size-1, SEEK_CUR)
					|| (ret=fwrite("\0", 1, 1, fp))!=1)
				{
					detail::set_error(ec);
					return ret;
				}

				tem_off += (int32_t)block_size;
			}

			ret = fseek64(fp, offset - tem_off, SEEK_CUR);
		}

		if(ret
			||(ret=fwrite(buf, 1, bufLen, fp))!=bufLen
			)
		{
			detail::set_error(ec);
		}
		else
			detail::set_no_error(ec);

		return ret;
	}
}