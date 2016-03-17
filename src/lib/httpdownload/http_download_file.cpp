#include "httpdownload/http_download_file.hpp"
#include "asfio/os_api.hpp"

#include <p2engine/push_warning_option.hpp>
#include <boost/filesystem.hpp>
#include <p2engine/pop_warning_option.hpp>

namespace http_download{

	void http_download_file::start(const std::string& _url)
	{
		http_download_base::start(_url);
		if (pf_)
		{
			asfio::fileclose(pf_);
			pf_ = NULL;
		}
	}

	void http_download_file::__write(const char* buf, uint32_t len)
	{
		boost::system::error_code error;
		if (!pf_ && boost::filesystem::status(filename_.c_str()).type() == boost::filesystem::file_not_found)
		{
			pf_ = asfio::fileopen(filename_, "wb+", error);
		}
		else if (!pf_)
		{
			pf_ = asfio::fileopen(filename_, "ab+", error);
		}
		asfio::filewrite(buf, len, pf_, error);
		if (100 == status_)
		{
			asfio::fileclose(pf_);
		}
		http_download_base::__write(buf, len);
	}

	void http_download_file::set_filename(const std::string& _filename)
	{
		filename_ = _filename;
	}

};