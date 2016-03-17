#include "common/upload_capacity_detector.h"

#include <p2engine/push_warning_option.hpp>
#include <string>
#include <boost/asio/ip/icmp.hpp>
#include <boost/lexical_cast.hpp>
#include <p2engine/pop_warning_option.hpp>

#include <p2engine/p2engine.hpp>

#if !defined(POOR_CPU)

#if defined(CAPACITY_CACHE)
#include "SimpleIni.h"
#endif

using namespace p2engine;
// Packet header for IPv4.
//
// The wire format of an IPv4 header is:
// 
// 0               8               16                             31
// +-------+-------+---------------+------------------------------+      ---
// |       |       |               |                              |       ^
// |version|header |    type of    |    total length in bytes     |       |
// |  (4)  | length|    service    |                              |       |
// +-------+-------+---------------+-+-+-+------------------------+       |
// |                               | | | |                        |       |
// |        identification         |0|D|M|    fragment offset     |       |
// |                               | |F|F|                        |       |
// +---------------+---------------+-+-+-+------------------------+       |
// |               |               |                              |       |
// | time to live  |   protocol    |       header checksum        |   20 bytes
// |               |               |                              |       |
// +---------------+---------------+------------------------------+       |
// |                                                              |       |
// |                      source IPv4 address                     |       |
// |                                                              |       |
// +--------------------------------------------------------------+       |
// |                                                              |       |
// |                   destination IPv4 address                   |       |
// |                                                              |       v
// +--------------------------------------------------------------+      ---
// |                                                              |       ^
// |                                                              |       |
// /                        options (if any)                      /    0 - 40
// /                                                              /     bytes
// |                                                              |       |
// |                                                              |       v
// +--------------------------------------------------------------+      ---
class ipv4_header
{
public:
	ipv4_header() { std::fill(rep_, rep_ + sizeof(rep_), 0); }

	unsigned char version() const { return (rep_[0] >> 4) & 0xF; }
	unsigned short header_length() const { return (rep_[0] & 0xF) * 4; }
	unsigned char type_of_service() const { return rep_[1]; }
	unsigned short total_length() const { return decode(2, 3); }
	unsigned short identification() const { return decode(4, 5); }
	bool dont_fragment() const { return (rep_[6] & 0x40) != 0; }
	bool more_fragments() const { return (rep_[6] & 0x20) != 0; }
	unsigned short fragment_offset() const { return decode(6, 7) & 0x1FFF; }
	unsigned int time_to_live() const { return rep_[8]; }
	unsigned char protocol() const { return rep_[9]; }
	unsigned short header_checksum() const { return decode(10, 11); }

	boost::asio::ip::address_v4 source_address() const
	{
		boost::asio::ip::address_v4::bytes_type bytes
			= { { rep_[12], rep_[13], rep_[14], rep_[15] } };
		return boost::asio::ip::address_v4(bytes);
	}

	boost::asio::ip::address_v4 destination_address() const
	{
		boost::asio::ip::address_v4::bytes_type bytes
			= { { rep_[16], rep_[17], rep_[18], rep_[19] } };
		return boost::asio::ip::address_v4(bytes);
	}

	friend std::istream& operator>>(std::istream& is, ipv4_header& header)
	{
		is.read(reinterpret_cast<char*>(header.rep_), 20);
		if (header.version() != 4)
			is.setstate(std::ios::failbit);
		std::streamsize options_length = header.header_length() - 20;
		if (options_length < 0 || options_length > 40)
			is.setstate(std::ios::failbit);
		else
			is.read(reinterpret_cast<char*>(header.rep_) + 20, options_length);
		return is;
	}

private:
	unsigned short decode(int a, int b) const
	{ return (rep_[a] << 8) + rep_[b]; }

	unsigned char rep_[60];
};

// ICMP header for both IPv4 and IPv6.
//
// The wire format of an ICMP header is:
// 
// 0               8               16                             31
// +---------------+---------------+------------------------------+      ---
// |               |               |                              |       ^
// |     type      |     code      |          checksum            |       |
// |               |               |                              |       |
// +---------------+---------------+------------------------------+    8 bytes
// |                               |                              |       |
// |          identifier           |       sequence number        |       |
// |                               |                              |       v
// +-------------------------------+------------------------------+      ---

class icmp_header
{
public:
	enum { echo_reply = 0, destination_unreachable = 3, source_quench = 4, 
		redirect = 5, echo_request = 8, time_exceeded = 11, parameter_problem = 12, 
		timestamp_request = 13, timestamp_reply = 14, info_request = 15, 
		info_reply = 16, address_request = 17, address_reply = 18 };

	icmp_header() { std::fill(rep_, rep_ + sizeof(rep_), 0); }

	unsigned char type() const { return rep_[0]; }
	unsigned char code() const { return rep_[1]; }
	unsigned short checksum() const { return decode(2, 3); }
	unsigned short identifier() const { return decode(4, 5); }
	unsigned short sequence_number() const { return decode(6, 7); }

	void type(unsigned char n) { rep_[0] = n; }
	void code(unsigned char n) { rep_[1] = n; }
	void checksum(unsigned short n) { encode(2, 3, n); }
	void identifier(unsigned short n) { encode(4, 5, n); }
	void sequence_number(unsigned short n) { encode(6, 7, n); }

	friend std::istream& operator>>(std::istream& is, icmp_header& header)
	{ return is.read(reinterpret_cast<char*>(header.rep_), 8); }

	friend std::ostream& operator<<(std::ostream& os, const icmp_header& header)
	{ return os.write(reinterpret_cast<const char*>(header.rep_), 8); }

private:
	unsigned short decode(int a, int b) const
	{ return (rep_[a] << 8) + rep_[b]; }

	void encode(int a, int b, unsigned short n)
	{
		rep_[a] = static_cast<unsigned char>(n >> 8);
		rep_[b] = static_cast<unsigned char>(n & 0xFF);
	}

	unsigned char rep_[8];
};

template <typename Iterator>
void compute_checksum(icmp_header& header, 
	Iterator body_begin, Iterator body_end)
{
	unsigned int sum = (header.type() << 8) + header.code()
		+ header.identifier() + header.sequence_number();

	Iterator body_iter = body_begin;
	while (body_iter != body_end)
	{
		sum += (static_cast<unsigned char>(*body_iter++) << 8);
		if (body_iter != body_end)
			sum += static_cast<unsigned char>(*body_iter++);
	}

	sum = (sum >> 16) + (sum & 0xFFFF);
	sum += (sum >> 16);
	header.checksum(static_cast<unsigned short>(~sum));
}

//使用icmp ping来探测上行带宽
using boost::asio::ip::icmp;

class pinger
	:public boost::enable_shared_from_this<pinger>
{
	typedef pinger this_type;
	SHARED_ACCESS_DECLARE;

public:
	static shared_ptr create(boost::asio::io_service& io_service, const std::string& destination)
	{
		return shared_ptr(new this_type(io_service, destination), 
			shared_access_destroy<this_type>()
			);
	}

protected:
	pinger(boost::asio::io_service& io_service, const std::string& destination)
		: resolver_(io_service), socket_(io_service)
		, timer_(io_service), sequence_number_(0), num_replies_(0)
		, uploac_capacity_(-1), stop_(false), desthost_(destination)
	{
	}
	virtual ~pinger()
	{
	}

public:
	void start(){
		socket_.get_io_service().post(
			make_alloc_handler(boost::bind(&pinger::__start, shared_from_this()))
			);
	}

	//<0表示未知
	int get_upload_capacity()
	{
		return uploac_capacity_;
	}

private:
	enum{SEND_CNT=20};
	enum{PACKET_SIZE=1400};
	void __start()
	{
		icmp::resolver::query query(icmp::v4(), desthost_, "");
		try
		{
			socket_.open(icmp::v4());
			icmp::resolver::iterator itr=resolver_.resolve(query);
			for(;itr!=icmp::resolver::iterator();++itr)
			{
				destination_ = *itr;
				if(!is_any(destination_.address())
					&&!is_local(destination_.address())
					&&!is_loopback(destination_.address())
					)
				{
					start_send();
					start_receive();
					break;
				}
			}
		}
		catch (...)
		{
			return;
		}
	}
	void start_send()
	{
		std::string body;
		body.resize(PACKET_SIZE, 'a');

		// Create an ICMP header for an echo request.
		icmp_header echoRequest;
		echoRequest.type(icmp_header::echo_request);
		echoRequest.code(0);
		echoRequest.identifier(get_identifier());
		echoRequest.sequence_number(++sequence_number_);
		compute_checksum(echoRequest, body.begin(), body.end());

		// Encode the request packet.
		boost::asio::streambuf request_buffer;
		std::ostream os(&request_buffer);
		os << echoRequest << body;

		// Send the request.
		time_sent_ = boost::posix_time::microsec_clock::universal_time();
		error_code ec;
		for (int i=0;i<SEND_CNT;i++)
		{
			socket_.send_to(request_buffer.data(), destination_, 0, ec);
		}

		// Wait up to five seconds for a reply.
		num_replies_ = 0;
		timer_.expires_from_now(boost::posix_time::seconds(5), ec);
		timer_.async_wait(make_alloc_handler(
			boost::bind(&pinger::handle_timeout, shared_from_this(), _1)
			));
	}

	void handle_timeout(error_code ec)
	{
		if (!ec)
		{
			calc();
		}
		else
		{
		}
		stop_=true;
		socket_.close(ec);
	}
	void calc()
	{
		if (num_replies_ >=2)
		{
			int64_t duration=(time_last_recvd_-time_first_recvd_).total_microseconds();
			if (duration>0)
			{
				int64_t capacity=(int64_t)PACKET_SIZE*(num_replies_-1)/duration*1000000;
				uploac_capacity_=(int)capacity;

				stop_=true;
				error_code ec;
				timer_.cancel(ec);
			}
		}
	}

	void start_receive()
	{
		socket_.async_receive(boost::asio::buffer(reply_buffer_), 
			make_alloc_handler(boost::bind(&pinger::handle_receive, shared_from_this(), _1, _2))
			);
	}

	void handle_receive(const boost::system::error_code& error, std::size_t length)
	{
		if (!error)
		{
			if (num_replies_==0)
				time_first_recvd_=system_time::precise_local_time();
			else
				time_last_recvd_=system_time::precise_local_time();
			++num_replies_;
			if (num_replies_>=SEND_CNT/2)
			{
				calc();
			}
			if(!stop_)
			{
				start_receive();
			}
		}
	}

	static unsigned short get_identifier()
	{
#if defined(BOOST_WINDOWS)
		return static_cast<unsigned short>(::GetCurrentProcessId());
#else
		return static_cast<unsigned short>(::getpid());
#endif
	}

private:
	bool stop_;
	icmp::resolver resolver_;
	icmp::endpoint destination_;
	icmp::socket socket_;
	boost::asio::deadline_timer timer_;
	unsigned short sequence_number_;
	boost::posix_time::ptime time_sent_;
	boost::array<char, 65536> reply_buffer_;
	std::size_t num_replies_;
	boost::posix_time::ptime time_first_recvd_;
	boost::posix_time::ptime time_last_recvd_;
	int uploac_capacity_;
	std::string desthost_;
};

class upload_capacity_detector
{
	class upload_capacity_detector_impl
		:public boost::enable_shared_from_this<upload_capacity_detector_impl>
	{
		typedef upload_capacity_detector_impl this_type;
		SHARED_ACCESS_DECLARE;

	public:
		static shared_ptr create()
		{
			return shared_ptr(new this_type(), 
				shared_access_destroy<this_type>()
				);
		}

	protected:
		upload_capacity_detector_impl()
		{
#if defined(CAPACITY_CACHE)
#	ifdef WINDOWS_OS
			capacity_file_="upload_capacity";
#	else
			capacity_file_="/temp/upload_capacity";
#	endif
#endif
			speed_=-1;
		}
		virtual ~upload_capacity_detector_impl()
		{
			if (thread_)
			{
				error_code ec;
				ios_->stop();
				thread_->join();
			}
		}

	public:
		void start()
		{
			if (speed_>0)
				return;

			if (thread_)
			{
				error_code ec;
				ios_->stop();
				thread_->join();
			}

#if defined(CAPACITY_CACHE)
			if(ini_.LoadFile(capacity_file_)>=0)
			{
				const char * pszValue = ini_.GetValue(capacity_file_, "timestamp", NULL /*default*/);
				time_t t=time(NULL);
				if(pszValue)
					t=atoi(pszValue);
				if (t+3600>time(NULL))
				{
					const char * pszValue = ini_.GetValue(capacity_file_, "speed", NULL /*default*/);
					if (pszValue)
					{
						speed_=atoi(pszValue);
					}
				}
			}
#endif			
			if (speed_<0)
			{
				thread_.reset(new boost::thread(
					boost::bind(&upload_capacity_detector_impl::__start, shared_from_this())
					));
			}
		}

		//<0表示未知
		int get_upload_capacity()
		{
			if (speed_>0)
				return speed_;//探测过了
			if (!pinger_)
				return -1;//未启动探测
			speed_=pinger_->get_upload_capacity();

#if defined(CAPACITY_CACHE)
			if (speed_>0)//写如文件保存
			{
				char buf[128]={'\0'};
				std::string tms=boost::lexical_cast<std::string>(time(NULL));
				std::string speeds=boost::lexical_cast<std::string>(speed_);
				ini_.SetValue(capacity_file_, "timestamp", tms.c_str());
				ini_.SetValue(capacity_file_, "speed", speeds.c_str());
				ini_.SaveFile(capacity_file_);
			}
#endif
			return speed_;
		}
	private:
		void __start()
		{
			try
			{
				if (!pinger_)
				{
					ios_.reset(new boost::asio::io_service);
					pinger_=pinger::create(*ios_, "www.google.com");
					pinger_->start();
					ios_->run();
				}
			}
			catch (...)
			{
			}
		}
	private:
		boost::shared_ptr<pinger> pinger_;
		boost::scoped_ptr<boost::asio::io_service> ios_;
		boost::scoped_ptr<boost::thread> thread_;
		int speed_;
#if defined(CAPACITY_CACHE)
		CSimpleIni ini_;
		const char* capacity_file_;
#endif
	};
public:
	upload_capacity_detector()
		:impl_(upload_capacity_detector_impl::create())
	{
		impl_->start();
	}
	//<0表示未知
	int get_upload_capacity()
	{
		if (!impl_)
			return -1;
		return impl_->get_upload_capacity();
	}
private:
	boost::shared_ptr<upload_capacity_detector_impl> impl_;
};
#endif

namespace p2common{

	int get_upload_capacity()
	{
#if !defined(POOR_CPU)
		try
		{
			static upload_capacity_detector upload_capacity_detector_;
			return upload_capacity_detector_.get_upload_capacity();
		}
		catch (...)
		{
			return 64*1024;
		}
#else
		return 1024;
#endif
	}

}
