#include "asfio/asfile.hpp"
#include "asfio/async_dskcache.h"
#include <bitset>
#include <boost/dynamic_bitset.hpp>

using namespace aio;
const int MAX_PIECE=10000;
class disk_cache_test
{
public:
	disk_cache_test(const std::string& file_name,
		size_t fileLen,size_t chunkLen,size_t pieceLen,size_t memCacheSize
		)
		:cache_impl_(async_dskcache::create(ios_))
		,work_(ios_)
	{
		cache_impl_->open(
			file_name,fileLen,chunkLen,pieceLen,memCacheSize,
			boost::bind(&disk_cache_test::on_opened,this,_1)
			);
	}
	void run()
	{
		ios_.run();
	}

protected:
	void on_opened(error_code ec)
	{
		std::string chunkMap;
		int from=0;
		int to=MAX_PIECE-1;
		cache_impl_->get_piece_map("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",from,to,chunkMap);
		std::cout<<( boost::dynamic_bitset<>(chunkMap.begin(),chunkMap.end()))<<std::endl;

		for (int i=0;i<MAX_PIECE;++i)
		{
			std::cout<<(cache_impl_->has_piece("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",i))<<"  ";
		}

		write_cnt_=0;
		if (!ec)
		{
			std::string dummy=boost::lexical_cast<std::string>(write_cnt_)+"\n\0";
			safe_buffer buf;
			safe_buffer_io io(&buf);
			io.write(dummy.c_str(),dummy.length()+1);

			cache_impl_->write_piece("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",1024*1024,write_cnt_,buf,
				boost::bind(&disk_cache_test::on_piece_write,this,_1,_2)
				);

		}
	}

	void on_piece_write(size_t l,error_code ec)
	{
		if (write_cnt_>MAX_PIECE)
			return;
		if (!ec)
		{
			cache_impl_->read_piece("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",write_cnt_++,
				boost::bind(&disk_cache_test::on_piece_read,this,_1,_2)
				);


			std::string dummy=boost::lexical_cast<std::string>(write_cnt_);
			safe_buffer buf;
			safe_buffer_io io(&buf);
			io.write(dummy.c_str(),dummy.length()+1);

			cache_impl_->write_piece("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",1024*1024,write_cnt_,buf,
				boost::bind(&disk_cache_test::on_piece_write,this,_1,_2)
				);
		}
		else
		{
			std::cout<<"on_piece_write, ec.message="<<ec.message()<<std::endl;
		}
	}

	void on_piece_read(safe_buffer buf,error_code ec)
	{
		if (!ec)
		{
			std::cout<<buf.size()<<"  "<<buffer_cast<char*>(buf)<<"\n";
		}
		else
		{
			std::cout<<"on_piece_read, ec.message="<<ec.message()<<"\n";
		}
	}
private:
	io_service ios_;
	boost::shared_ptr<async_dskcache> cache_impl_;
	io_service::work work_;
	int write_cnt_;
};

int main(int argc, char* argv[])
{

	{
		std::string filePath("c:\\cache_test");
		size_t fileLen=1*1024*1024*1024;
		size_t chunkLen=5*1024*1024;
		size_t pieceLen=1500;
		size_t memCacheSize=3*1024*1024;

		chunkLen=(chunkLen/pieceLen)*pieceLen;
		fileLen=(fileLen/chunkLen)*chunkLen;
		disk_cache_test tester(filePath,fileLen,chunkLen,pieceLen,memCacheSize);
		tester.run();
	}

	system("pause");

	return 0;
}
