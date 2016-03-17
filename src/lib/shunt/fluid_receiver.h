#ifndef _SHUNT_FLUID_RECEIVER_H__
#define _SHUNT_FLUID_RECEIVER_H__

#include "shunt/receiver.h"

extern "C"{
	struct libvlc_instance_t;
}

namespace p2shunt{

	class fluid_receiver
		:public receiver
	{
		typedef fluid_receiver this_type;

	protected:
		fluid_receiver(io_service& ios, int defaultDelayMsec);
		void process_received_data(const safe_buffer& data, bool isDatagramProtocal);
		void do_stop();

	private:
		void on_distribute_timer();
		void que_packet();
		void push_packet_to_que(long long dts, const safe_buffer& buf);
		void smoother_distribute(const safe_buffer& data);
		void check_discontinuities(long long& dtsNow);
		bool is_discontinuities(long long dtsFront, long long dts)
		{
			return dts<dtsFront||(dts-dtsFront)>500;
		}

	protected:
		typedef p2engine::rough_timer timer;

		enum{USE_UNKNOWN, USE_DTS, USE_SMOOTHER} use_what_;
		smoother smoother_;
		std::deque<safe_buffer>packet_que_tmp_;
		std::deque<std::pair<long long, safe_buffer> >packet_que_;
		int dts_stream_id_;
		boost::optional<int> average_pkt_interval_;
		long long last_rx_dts_, last_tx_dts_;
		int discontinuities_;//DTSÍ»±ä´ÎÊý
		boost::optional<long long> time_offset_; 
		boost::optional<int> buffered_duration_; 
		boost::shared_ptr<timer> do_distribute_timer_;
		safe_buffer buf_;
		int delay_;
	};

	class unicast_fluid_receiver
		:public fluid_receiver
	{
		typedef unicast_fluid_receiver this_type;
		SHARED_ACCESS_DECLARE;

	public:
		static shared_ptr create(io_service& ios)
		{
			return shared_ptr(new this_type(ios), 
				shared_access_destroy<this_type>());
		}

		virtual bool is_connected()const{return true;};
		virtual bool updata(const std::string& url, error_code& ec);
		virtual void stop();

	protected:
		unicast_fluid_receiver(io_service& ios);
		~unicast_fluid_receiver(){stop();}

		void handle_receive_from(const error_code& error, size_t bytes_recvd);
		void async_receive();
		virtual bool is_valid_endpoint();

	protected:
		asio::ip::udp::socket socket_;
		asio::ip::udp::endpoint remote_endpoint_;
		asio::ip::address local_address_;
		enum { max_length = 65536 };
		char data_[max_length];
	};

	class multicast_fluid_receiver
		:public unicast_fluid_receiver
	{
		typedef multicast_fluid_receiver this_type;
		SHARED_ACCESS_DECLARE;
	public:
		static shared_ptr create(io_service& ios)
		{
			return shared_ptr(new this_type(ios), 
				shared_access_destroy<this_type>());
		}
		virtual bool updata(const std::string& url, error_code& ec);

	protected:
		multicast_fluid_receiver(io_service& ios);

		virtual bool is_valid_endpoint()
		{
			return true;
		}
	private:
		asio::ip::udp::endpoint mulicast_endpoint_;
	};

	class http_fluid_receiver
		:public fluid_receiver
	{
		typedef http_fluid_receiver this_type;

		typedef http::basic_http_connection<http::http_connection_base> http_connection;
		typedef http::basic_http_acceptor<http_connection, http_connection>	http_acceptor;

		SHARED_ACCESS_DECLARE;
	public:
		static shared_ptr create(io_service& ios)
		{
			return shared_ptr(new this_type(ios), 
				shared_access_destroy<this_type>());
		}
		virtual void stop();	
		virtual bool is_connected()const{return socket_&&socket_->is_connected();};
		virtual bool updata(const std::string& url, error_code& ec);

	protected:
		http_fluid_receiver(io_service& ios);

		void on_connected(http_connection*, const error_code& ec);
		void on_disconnected(http_connection*, const error_code& ec);
		void on_received_request_header(http_connection*, const http::response&);
		void on_received_data(http_connection*, safe_buffer data);

	private:
		boost::shared_ptr<http_connection> socket_;
		uri uri_;
	};


	class vlc_fluid_receiver
		:public unicast_fluid_receiver
	{
		typedef vlc_fluid_receiver this_type;
		SHARED_ACCESS_DECLARE;
	public:
		static shared_ptr create(io_service& ios)
		{
			return shared_ptr(new this_type(ios),
				shared_access_destroy<this_type>());
		}
		virtual void stop();
		virtual bool updata(const std::string& url, error_code& ec);

	protected:
		vlc_fluid_receiver(io_service& ios);
		virtual ~vlc_fluid_receiver();

	private:
		int receiver_impl_port_;
		libvlc_instance_t* vlc_instance_;
		uri vlc_in_uri_;
	};

}

#endif//_SHUNT_STREMA_ADAPTOR_RECEIVER_H__
