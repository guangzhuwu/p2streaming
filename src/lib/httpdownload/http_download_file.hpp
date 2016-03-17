#ifndef _http_download_file_
#define _http_download_file_

#include "httpdownload/http_download_base.hpp"

namespace http_download{
	class http_download_file: public http_download_base
	{
		typedef http_download_file this_type;
		SHARED_ACCESS_DECLARE;
	
	public:
		static shared_ptr create(io_service& ios)
		{
			return boost::shared_ptr<this_type>(
				new this_type(ios), shared_access_destroy<this_type>()
				);
		}
		virtual void start(const std::string& _url);
		virtual void __write(const char* buf, uint32_t len);
		void set_filename(const std::string& _filename);
	
	protected:
		http_download_file(io_service& ios):http_download_base(ios), pf_(NULL){};
		~http_download_file(){};
	
	private:
		std::string filename_;
		FILE* pf_;

	};
}
#endif //_http_download_file_