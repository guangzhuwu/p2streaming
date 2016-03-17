#ifndef _http_download_memory
#define _http_download_memory

#include "httpdownload/http_download_base.hpp"

namespace http_download{

	class http_download_memory : public http_download_base
	{
		typedef http_download_memory this_type;
		typedef boost::function<void(const std::string&)>  all_download_callback;
		typedef boost::function<void(int64_t, int64_t, safe_buffer)> partial_download_callback;
		SHARED_ACCESS_DECLARE;
	public:
		static shared_ptr create(io_service& ios)
		{
			return boost::shared_ptr<this_type>(
				new this_type(ios), shared_access_destroy<this_type>()
				);
		}

		void register_down_finish_handler(const all_download_callback& h)
		{
			all_downloal_signal_ = h;
		}

		void register_down_media_finish_handler(partial_download_callback h)
		{
			partial_downloal_signal_ = h;
		}

		virtual void start(const std::string& _url);
		virtual void connect_overtime();
		virtual void __write(const char* buf, uint32_t len);
	protected:
		http_download_memory(io_service& ios) :http_download_base(ios), recbuf_()
		{
			bFinish_ = false;
		};

		~http_download_memory();
		virtual void on_disconnected(const error_code& ec);
		void finish_down();
		void failed_down();
	private:
		safe_buffer recbuf_;
		bool        bFinish_;
		all_download_callback  all_downloal_signal_;
		partial_download_callback partial_downloal_signal_;
	};
};

#endif //_http_download_memory