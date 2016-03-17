#include "httpdownload/http_download_memory.hpp"

#include "p2engine/raw_buffer.hpp"
#include "p2engine/io.hpp"
namespace http_download
{

	http_download_memory::~http_download_memory()
	{
		if(!bFinish_)
		{
			failed_down();
		}
	}

	void http_download_memory::start(const std::string& _url)
	{
		http_download_base::start(_url);
		recbuf_.clear();
	}

	void  http_download_memory::connect_overtime()
	{
		http_download_base::connect_overtime();

		failed_down();
	}

	void http_download_memory::__write(const char* buf, uint32_t len)
	{
		try
		{
			safe_buffer_io bufio(&recbuf_);
			bufio.write(buf, len);

			if(100 == status_)
			{
				finish_down();
			}
		}
		catch (std::exception& e)
		{
			std::cout<<"http_download_memory error:"<<e.what()<<std::endl;
		}
	}
	void  http_download_memory::on_disconnected(const error_code& ec)
	{
		if (-1 == content_length_)
		{
			finish_down();
		}
		http_download_base::on_disconnected(ec);
	}

	void http_download_memory::finish_down()
	{
		bFinish_ = true;	

		if (!partial_downloal_signal_.empty())
		{
			partial_downloal_signal_(ini_start_pos_, end_pos_, recbuf_);
		}

		if (!all_downloal_signal_.empty())
		{
			std::string content;
			content.append(buffer_cast<char*>(recbuf_), buffer_size(recbuf_));
			all_downloal_signal_(content);
		}
	}

	void http_download_memory::failed_down()
	{
		bFinish_ = true;

		if(!all_downloal_signal_.empty())
			all_downloal_signal_("");
	}
};