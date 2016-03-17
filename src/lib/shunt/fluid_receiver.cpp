#include "shunt/fluid_receiver.h"

#include <vlc/vlc.h>
#include <boost/regex.hpp>
#include <boost/asio/ip/multicast.hpp>

#include "common/tsparse.h"

#pragma comment(lib, "libvlc")
#pragma comment(lib, "libvlccore")

namespace p2shunt{
	enum{ MAX_PKT_SIZE = 1500 };

	fluid_receiver::fluid_receiver(io_service& ios, int defaultDelayMsec)
		: receiver(ios)
		, smoother_(millisec(10000), millisec(500), millisec(5000), millisec(50), millisec(50), 160 * 1024 / 8, ios)
		, last_rx_dts_(NOPTS_VALUE)
		, discontinuities_(0)
		, use_what_(USE_SMOOTHER)
	{
		if (g_back_fetch_duration || g_delay_guarantee)
		{
			int delay = (g_back_fetch_duration ? *g_back_fetch_duration : 0)
				+ (g_delay_guarantee ? *g_delay_guarantee : 0);
			delay_ = delay;
		}
		else
		{
			delay_ = defaultDelayMsec;
		}
	}

	void fluid_receiver::do_stop()
	{
		smoother_.stop();
		if (do_distribute_timer_)
		{
			do_distribute_timer_->cancel();
			do_distribute_timer_.reset();
		}
	}

	void fluid_receiver::process_received_data(const safe_buffer& data, 
		bool isDatagramProtocal)
	{
		if (isDatagramProtocal)
		{
			buf_ = data;
			que_packet();
		}
		else
		{
			int bufLen = (int)buf_.size();
			int recvdLen = (int)data.size();
			int copyLen = std::min(recvdLen, PIECE_SIZE - bufLen);
			const char* p = buffer_cast<char*>(data);
			while (copyLen&&copyLen <= recvdLen)
			{
				if (buf_.size() == 0)
					buf_.reserve(MAX_PKT_SIZE);
				safe_buffer_io io(&buf_);
				io.write(p, copyLen);
				if (buf_.size() == PIECE_SIZE)
				{
					que_packet();
					buf_.recreate(MAX_PKT_SIZE);
					buf_.clear();
				}
				p += copyLen;
				bufLen = (int)buf_.size();
				recvdLen -= copyLen;
				copyLen = std::min(recvdLen, PIECE_SIZE - bufLen);
			}
		}
	}

	void fluid_receiver::que_packet()
	{
		if (use_what_ == USE_UNKNOWN)
		{
			if (!do_distribute_timer_)
			{
				do_distribute_timer_ = timer::create(get_io_service());
				do_distribute_timer_->set_obj_desc("p2shunt::fluid_receiver::do_distribute_timer_");
				do_distribute_timer_->register_time_handler(boost::bind(&this_type::on_distribute_timer, this));
				do_distribute_timer_->async_keep_waiting(millisec(delay_), millisec(10));
			}
		}

		if (use_what_ != USE_SMOOTHER)
		{
			ts_t ts;
			ts_parse parse;
			const unsigned char* p = buffer_cast<const unsigned char*>(buf_);
			const int* streamID = (last_rx_dts_ == NOPTS_VALUE) ? NULL : &dts_stream_id_;
			if (parse.get_dts(p, buf_.size(), &ts, streamID) > 0 && (ts.pes.dts != NOPTS_VALUE))
			{
				//std::cout<<(int)ts.pes.stream_id<<" ----------- \n";
				long long dts = ts.pes.dts / 90LL;
				DEBUG_SCOPE(;
				if (in_probability(0.001))//测试抗跳变能力
				{
					std::cout << "--------测试抗跳变能力---------haha \n";
					dts += random(-5000, 5000);
				}
				);

				if (last_rx_dts_ == NOPTS_VALUE)
				{
					dts_stream_id_ = ts.pes.stream_id;

					push_packet_to_que(dts, buf_);

					//pkt before this pkt, abandon...
					packet_que_tmp_.clear();
				}
				else
				{
					int cnt = (int)packet_que_tmp_.size();
					int interval = int((dts - last_rx_dts_) / (cnt + 1));
					long long theT = last_rx_dts_;
					if (is_discontinuities(last_rx_dts_, theT + interval))
					{//产生了突变，使用原来的平均码率
						if (average_pkt_interval_)
						{
							interval = *average_pkt_interval_ * 4 / 5;
						}
					}
					else
					{
						if (!average_pkt_interval_)
							average_pkt_interval_ = interval;
						else
							average_pkt_interval_ = (interval + *average_pkt_interval_) / 2;
					}
					while (!packet_que_tmp_.empty())
					{
						theT += interval;
						push_packet_to_que(theT, packet_que_tmp_.front());
						packet_que_tmp_.pop_front();
					}
					push_packet_to_que(dts, buf_);
				}

			}
			else// if (last_rx_dts_ != NOPTS_VALUE)
			{
				packet_que_tmp_.push_back(buf_);
			}
		}
		else if (use_what_ != USE_DTS)
		{
			smoother_.push(0,
				boost::bind(&this_type::smoother_distribute, this, buf_),
				buf_.size()
				);
		}

	}

	void fluid_receiver::push_packet_to_que(long long dts, const safe_buffer& buf)
	{
		if (last_rx_dts_ != NOPTS_VALUE
			&&is_discontinuities(last_rx_dts_, dts)
			)
		{
			discontinuities_++;
			DEBUG_SCOPE(
				std::cout << " ---------------     "
				<< last_rx_dts_ << " , " << dts << " , " << (last_rx_dts_ - dts) << "\n";
			);
		}
		packet_que_.push_back(std::make_pair(dts, buf));
		last_rx_dts_ = dts;
	}

	void fluid_receiver::on_distribute_timer()
	{
		if (use_what_ == USE_UNKNOWN)
		{
			if (!packet_que_.empty())
			{
				use_what_ = USE_DTS;
				smoother_.stop();
			}
			else
			{
				use_what_ = USE_SMOOTHER;
				if (do_distribute_timer_)
					do_distribute_timer_->cancel();
				do_distribute_timer_.reset();
				return;
			}
		}

		BOOST_ASSERT(do_distribute_timer_);
		if (packet_que_.empty())
			return;

		tick_type now = tick_now();
		if (!time_offset_)
		{
			if (0 == discontinuities_)
			{
				int duration = (int)(packet_que_.back().first - packet_que_.front().first);
				if (duration < delay_ * 5 / 6)
					return;
				buffered_duration_ = duration;
			}
			time_offset_ = (long long)now - (long long)(packet_que_.front().first);
			last_tx_dts_ = packet_que_.front().first;
		}

		long long dtsNow = (long long)now - *time_offset_;
		if (USE_DTS == use_what_ && (int)packet_que_.size() > ((delay_ / 1000) * 2) * 400)
		{
			discontinuities_ = 0;
			use_what_ = USE_UNKNOWN;
			last_rx_dts_ = NOPTS_VALUE;
			average_pkt_interval_.reset();
			time_offset_.reset();
			packet_que_.clear();
			packet_que_tmp_.clear();
			do_distribute_timer_->cancel();
			do_distribute_timer_.reset();
			return;
		}

		for (; !packet_que_.empty();)
		{
			check_discontinuities(dtsNow);
			if (packet_que_.front().first <= dtsNow)
			{
				const safe_buffer& buf = packet_que_.front().second;

				last_tx_dts_ = packet_que_.front().first;
				speed_meter_ += buffer_size(buf);
				packet_speed_meter_ += 1;
				instantaneous_speed_meter_ += buffer_size(buf);
				instantaneous_packet_speed_meter_ += 1;

				this->media_handler(buf);
				packet_que_.pop_front();//---!!(pop)
			}
			else
			{
				break;
			}
		}

		if (packet_que_.size() > 1 && discontinuities_ == 0 && buffered_duration_)
		{
			int duration = (int)(packet_que_.back().first - packet_que_.front().first);
			if (duration > *buffered_duration_ * 3 / 2)
				*time_offset_ -= 2;
			else if (duration < *buffered_duration_ * 2 / 3)
				*time_offset_ += 2;
			DEBUG_SCOPE(
				if (in_probability(0.01))
					std::cout << duration << "\n";
			);
		}
	}

	void fluid_receiver::check_discontinuities(long long& dtsNow)
	{
		BOOST_ASSERT(!packet_que_.empty());

		long long dts = packet_que_.front().first;
		if (is_discontinuities(last_tx_dts_, dts))
		{
			DEBUG_SCOPE(
				std::cout << "WWWWWWWWWWWWWWWWWWWWWWWWWWWarning, discontinuities found!!!\a\n";
			);
			--discontinuities_;
			BOOST_ASSERT(discontinuities_ >= 0);
			tick_type now = tick_now();
			buffered_duration_ = (int)(packet_que_.back().first - dts);
			time_offset_ = (long long)now - dts;
			dtsNow = (long long)now - *time_offset_ + 1;//+1是为了确保front包超时而发出去
		}
	}

	void fluid_receiver::smoother_distribute(const safe_buffer& data)
	{
		if (use_what_ == USE_UNKNOWN)
		{
			if (!packet_que_.empty())
			{
				use_what_ = USE_DTS;
				smoother_.stop();
				return;
			}
			else
			{
				use_what_ = USE_SMOOTHER;
			}
		}

		BOOST_ASSERT(!do_distribute_timer_);
		this->media_handler(data);
	}

	//////////////////////////////////////////////////////////////////////////
	//unicast_fluid_receiver
	unicast_fluid_receiver::unicast_fluid_receiver(io_service& ios)
		: fluid_receiver(ios, 10 * 1000)
		, socket_(ios)
	{
	}

	bool unicast_fluid_receiver::updata(const std::string& url, error_code& ec)
	{
		const boost::regex reg("udp://(\\d+\\.\\d+\\.\\d+\\.\\d+)?:(\\d+)");
		boost::smatch what;
		if (boost::regex_match(url, what, reg))
		{
			std::string ip = what[1];
			if (ip.empty())
				ip = "0.0.0.0";
			int port = boost::lexical_cast<int>(what[2]);
			asio::ip::udp::endpoint edp(asio::ip::address::from_string(ip, ec), port);
			if (edp == socket_.local_endpoint(ec))
			{
				return true;
			}

			if (socket_.is_open())
				socket_.close(ec);
			socket_.open(edp.protocol(), ec);
			if (!ec)
			{
				asio::socket_base::non_blocking_io non_blocking_io(true);
				socket_.io_control(non_blocking_io, ec);
				socket_.set_option(asio::socket_base::reuse_address(true), ec);
				socket_.set_option(asio::socket_base::receive_buffer_size(1024 * 512), ec);
				disable_icmp_unreachable(socket_.native());
				ec.clear();
			}
			socket_.bind(edp, ec);
			if (!ec)
			{
				error_code err;
				local_address_ = socket_.local_endpoint(err).address();
				async_receive();
				return true;
			}
			else
			{
				error_code err;
				socket_.close(err);
				return false;
			}
		}
		else
		{
			return false;
		}

	}

	void unicast_fluid_receiver::stop()
	{
		do_stop();
		this->media_handler.clear();
		socket_.close();
	}

	void unicast_fluid_receiver::async_receive()
	{
		buf_.recreate(MAX_PKT_SIZE);
		error_code ec;
		if (socket_.available(ec) && !ec)
		{
			size_t len = socket_.receive_from(
				asio::buffer(buffer_cast<char*>(buf_), buf_.size()),
				remote_endpoint_, 0, ec
				);
			handle_receive_from(ec, len);
			return;
		}
		else
		{
			socket_.async_receive_from(
				asio::buffer(buffer_cast<char*>(buf_), buf_.size()), remote_endpoint_,
				make_alloc_handler(boost::bind(&this_type::handle_receive_from, SHARED_OBJ_FROM_THIS, _1, _2))
				);
		}
	}

	bool unicast_fluid_receiver::is_valid_endpoint()
	{
		//不信任公网IP来的数据包
		asio::ip::address addr = remote_endpoint_.address();
		return (is_local(local_address_) || is_loopback(local_address_)
			|| is_loopback(addr) || is_local(addr)
			);
	}

	void unicast_fluid_receiver::handle_receive_from(const error_code& error,
		size_t bytes_recvd)
	{
		if (!socket_.is_open())
			return;

		if (!error)
		{
			asio::ip::address addr = remote_endpoint_.address();
			if (bytes_recvd >= 188
				&& is_valid_endpoint()
				)
			{
				speed_meter_ += bytes_recvd;
				packet_speed_meter_ += 1;
				instantaneous_speed_meter_ += bytes_recvd;
				instantaneous_packet_speed_meter_ += 1;

				buf_.resize(bytes_recvd);
				process_received_data(buf_, true);
			}
		}
		else if (error == asio::error::message_size)
		{
			error_code err;
			socket_.receive(asio::buffer(data_, sizeof(data_)), 0, err);//too long，drop it
		}
		else
		{
			return;
		}
		async_receive();
	}


	//////////////////////////////////////////////////////////////////////////
	//multicast_fluid_receiver
	multicast_fluid_receiver::multicast_fluid_receiver(io_service& ios)
		: unicast_fluid_receiver(ios)
	{
	}

	bool multicast_fluid_receiver::updata(const std::string& url, error_code& ec)
	{
		const boost::regex reg("udp://(\\d+\\.\\d+\\.\\d+\\.\\d+)?:(\\d+)");
		boost::smatch what;
		if (boost::regex_match(url, what, reg))
		{
			std::string ip = what[1];
			if (ip.empty())
				ip = "0.0.0.0";
			int port = boost::lexical_cast<int>(what[2]);
			asio::ip::udp::endpoint edp(asio::ip::address::from_string(ip, ec), port);
			if (edp == mulicast_endpoint_)
			{
				return false;
			}
			mulicast_endpoint_ = edp;

			if (socket_.is_open())
				socket_.close(ec);
			socket_.open(edp.protocol(), ec);
			if (!ec)
			{
				asio::socket_base::non_blocking_io non_blocking_io(true);
				socket_.io_control(non_blocking_io, ec);
				socket_.set_option(asio::ip::multicast::join_group(edp.address()), ec);
				socket_.set_option(asio::socket_base::reuse_address(true), ec);
				socket_.set_option(asio::socket_base::receive_buffer_size(512 * 1024), ec);
				disable_icmp_unreachable(socket_.native());
				ec.clear();
			}
			socket_.bind(udp::endpoint(address(), port), ec);
			if (!ec)
			{
				async_receive();
				return true;
			}
			else
			{
				error_code err;
				socket_.close(err);
				return false;
			}
		}
		else
		{
			return false;
		}

	}

	//////////////////////////////////////////////////////////////////////////
	//http_fluid_receiver
	http_fluid_receiver::http_fluid_receiver(io_service& ios)
		: fluid_receiver(ios, 60 * 1000)
	{
	}

	void http_fluid_receiver::stop()
	{
		do_stop();
		if (socket_)
		{
			socket_->close();
			socket_.reset();
		}
	}

	bool http_fluid_receiver::updata(const std::string& url, error_code& ec)
	{
		uri u(url, ec);
		if (ec)
			return false;
		if (u == uri_&&socket_)
			return false;
		uri_ = u;
		buf_.reset();
		if (socket_&&socket_->is_open())
			socket_->close(ec);
		socket_ = http_connection::create(get_io_service());
		socket_->async_connect(uri_.host(), uri_.port());
		socket_->register_connected_handler(boost::bind(&this_type::on_connected, this, socket_.get(), _1));
		return true;
	}

	void http_fluid_receiver::on_connected(http_connection* conn, const error_code& ec)
	{
		if (conn != socket_.get())
			return;
		if (ec)
		{
			socket_.reset();
			return;
		}

		socket_->register_disconnected_handler(boost::bind(
			&this_type::on_disconnected, this, socket_.get(), _1
			));
		socket_->register_response_handler(boost::bind(
			&this_type::on_received_request_header, this, socket_.get(), _1
			));
		socket_->register_data_handler(boost::bind(
			&this_type::on_received_data, this, socket_.get(), _1
			));
		http::request req("GET", uri::escape(uri_.to_string(uri::all_components&~uri::host_component&~uri::protocol_component)));
		req.host(uri_.host(), uri_.port());
		req.set(http::HTTP_ATOM_User_Agent, "VLC / 2.1.3 LibVLC / 2.1.3");
		req.set("Icy-MetaData", "1");
		req.range(0, -1);
		req.keep_alive(false);

		std::cout << req << std::endl;

		safe_buffer buf;
		safe_buffer_io io(&buf);
		io << req;

		socket_->async_send(buf);
	}

	void http_fluid_receiver::on_disconnected(http_connection* conn, const error_code& ec)
	{
		if (conn == socket_.get())
		{
			socket_.reset();
			buf_.reset();

			error_code ec2;
			updata(uri_.to_string(), ec2);
		}
	}

	void http_fluid_receiver::on_received_request_header(http_connection* conn,
		const http::response& r)
	{
		std::cout << r << std::endl;
		if (conn != socket_.get())
			socket_.reset();
	}

	void http_fluid_receiver::on_received_data(http_connection*, safe_buffer data)
	{
		std::string s(buffer_cast<const char*>(data), data.length());
		std::cout << s << std::endl;
		process_received_data(data, false);
	}


	//////////////////////////////////////////////////////////////////////////
	//vlc_fluid_receiver
	vlc_fluid_receiver::vlc_fluid_receiver(io_service& ios)
		: unicast_fluid_receiver(ios)
		, vlc_instance_(NULL)
		, receiver_impl_port_(0)
	{

	}

	vlc_fluid_receiver::~vlc_fluid_receiver()
	{
		if (vlc_instance_)
		{
			libvlc_vlm_release(vlc_instance_);
			libvlc_release(vlc_instance_);
			vlc_instance_ = NULL;
		}
	}

	bool vlc_fluid_receiver::updata(const std::string& url, error_code& ec)
	{
		uri u(url, ec);
		if (ec)
			return false;
		if (u == vlc_in_uri_)
			return true;

		if (receiver_impl_port_ == 0)
			receiver_impl_port_ = random<int>(50000, 55000);
		for (int i = 0; i < 512; ++i)
		{
			std::string recvUrl = "udp://127.0.0.1:" + boost::lexical_cast<std::string>(receiver_impl_port_);
			error_code err;
			unicast_fluid_receiver::updata(recvUrl, err);
			if (err)
			{
				receiver_impl_port_ = random<int>(50000, 55000);
				continue;
			}

			if (vlc_instance_)
			{
				int r = ::libvlc_vlm_change_media(vlc_instance_, "vlc_fluid_receiver"
					, url.c_str()
					, recvUrl.c_str()
					, 0, NULL, true, false
					);
				if (r < 0)
					return false;
			}
			else
			{
				const char * vlc_args[] =
				{
					"-I", "dummy", /* Don't use any interface */
					"--ignore-config", /* Don't use VLC's config */
					//    "--extraintf=logger", /* Log everything */
					"--verbose=0",
					"--no-osd"
				};
				vlc_instance_ = ::libvlc_new(SIZEOF_ARRAY(vlc_args), vlc_args);
				if (!vlc_instance_)
				{
					return false;
				}
				int r = ::libvlc_vlm_add_broadcast(vlc_instance_, "vlc_fluid_receiver"
					, url.c_str()
					, recvUrl.c_str()
					, 0, NULL, true, false
					);
				if (r < 0)
					return false;
			}
			::libvlc_vlm_play_media(vlc_instance_, "vlc_fluid_receiver");
			vlc_in_uri_ = u;
			return true;
		}
		return false;
	}

	void vlc_fluid_receiver::stop()
	{
		unicast_fluid_receiver::stop();
		if (vlc_instance_)
		::libvlc_vlm_stop_media(vlc_instance_, "vlc_fluid_receiver");
	}
}
