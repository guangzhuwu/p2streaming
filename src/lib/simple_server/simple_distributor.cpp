#include "simple_server/simple_distributor.h"
#include "simple_server/distributor_scheduling.h"

#if !defined(_DEBUG_SCOPE) && defined(NDEBUG)
#define  SIMPLE_DISTRI_DBG(x) 
#else 
#define  SIMPLE_DISTRI_DBG(x) /*x*/
#endif

using namespace p2simple;
using namespace p2common;

void simple_distributor::register_message_handler(peer_connection_sptr conn)
{
#define REGISTER_HANDLER(msgType, handler)\
	conn->register_message_handler(msgType, boost::bind(&this_type::handler, this, conn.get(), _1));

	REGISTER_HANDLER(global_msg::join_channel, on_recvd_join_channel);
	REGISTER_HANDLER(global_msg::media_request, on_recvd_media_request);
	REGISTER_HANDLER(peer_peer_msg::buffermap_request, on_recvd_buffermap_request);


#undef REGISTER_HANDLER
}

simple_distributor::simple_distributor(io_service& net_svc, distributor_scheduling* scheduling)
	:basic_engine_object(net_svc)
	, running_(false)
	, cas_string_("")
	, media_packet_rate_(millisec(4000))
	, incomming_packet_rate_(millisec(4000))
	, film_duration_(-1)
	, file_length_(-1)
	, pull_distrib_smoother_(millisec(500), millisec(300), millisec(50), millisec(50), millisec(50), 480*1024/8, net_svc)
	, type_(NONE)
	, scheduling_(scheduling)
{
	set_obj_desc("simple_distributor");
}

simple_distributor::~simple_distributor()
{
	__stop();
}

void simple_distributor::stop()
{
	__stop();
}

void simple_distributor::__stop()
{
	if (!running_)
		return;
	running_=false;

	close_acceptor();
	sockets_.clear();
}

void simple_distributor::__start(const std::string& domain, 
	peer_info& local_info)
{
	__stop();

	running_=true;

	error_code ec;
	endpoint edp; 
	edp.port(local_info.internal_udp_port());
	start_acceptor(edp, domain, ec);
	endpoint udp_edp=urdp_acceptor_->local_endpoint(ec);
	if(!ec)
	{
		if (!local_info.has_internal_udp_port()||local_info.internal_udp_port()==0)
		{
			local_info.set_internal_udp_port(udp_edp.port());
			//local_info.set_internal_ip(udp_edp.address().to_v4().to_ulong());
		}
	}
	endpoint tcp_edp=trdp_acceptor_->local_endpoint(ec);
	if (!ec)
	{
		local_info.set_internal_tcp_port(tcp_edp.port());
		if (!local_info.has_external_tcp_port()||local_info.external_tcp_port()==0)
		{
			local_info.set_external_tcp_port(tcp_edp.port());
			//local_info.set_external_ip(tcp_edp.address().to_v4().to_ulong());
		}
	}

	DEBUG_SCOPE(
		std::cout<<"simple distritube ip:"<<tcp_edp<<" domain: "<<p2common::string_to_hex(domain)<<std::endl;
		);
}

void simple_distributor::start(peer_info& local_info)
{
	SIMPLE_DISTRI_DBG(
		std::cout<<"-----My ID----:"<<p2common::string_to_hex(local_info.peer_id())<<std::endl;
		);
	std::string domain=cache_server_demain+"/"+local_info.peer_id();
	__start(domain, local_info);
}

void simple_distributor::start_assistant(peer_info& local_info, channel_session* channel_info)
{
	if(!channel_info)
		return;
	channel_file_ = channel_info;

	SIMPLE_DISTRI_DBG(
		std::cout<<"-----My PARERENT  ID----:"<<string_to_hex(local_info.peer_id())<<std::endl;
		std::cout<<"-----My ASSISTAND ID----:"<<string_to_hex(channel_info->assist_id_.to_string())<<std::endl;
	);
	std::string domain=cache_server_demain+"/"+channel_info->assist_id_.to_string();
	upgrade(SERVER_ASSIST);
	//__start(domain, local_info);
}

void simple_distributor::on_accepted(peer_connection_sptr conn, const error_code& ec)
{
	if (!ec)
	{
		pending_sockets_.try_keep(conn, seconds(10));
		register_message_handler(conn);
		conn->register_disconnected_handler(boost::bind(&this_type::on_disconnected, this, conn.get(), _1));
		conn->ping_interval(SIMPLE_DISTRBUTOR_PING_INTERVAL);
		conn->keep_async_receiving();
	}
}

void simple_distributor::on_disconnected(peer_connection* conn, const error_code&ec)
{
	__on_disconnected(conn, ec);

	SIMPLE_DISTRI_DBG(
		std::cout<<"XXXXXXXXXXXXXXXX"<<(tick_type)tick_now()
		         <<" :simple distributor: disconnected, "
				 <<ec.message()<<std::endl;
	);
	sockets_.erase(conn->shared_obj_from_this<peer_connection>());
}

void simple_distributor::on_recvd_media_request(peer_connection* conn, 
	safe_buffer buf)
{
	media_request_msg msg;
	if (!parser(buf, msg))
		return;

	SIMPLE_DISTRI_DBG(
		if(in_probability(0.01))
			std::cout<<(tick_type)tick_now()<<" :simple distributor: media request(seqno="<<msg.seqno(0)<<")\n";
	);
	incomming_packet_rate_+= 1;

	BOOST_AUTO(connSptr, conn->shared_obj_from_this<peer_connection>());
	pull_distrib_smoother_.push((int64_t)reinterpret_cast<const void*>(this), 
		boost::bind(&this_type::read_request_media, this, connSptr, msg), 
		1500
		);

	////ASYNC read
	//if(film_duration_>0)//server全部响应, 
	//	read_request_media(conn, msg);
	//else//neighbor以概率响应
	//{
	//	double localToRemoteLostRate=conn->local_to_remote_lost_rate();
	//	if (in_probability(1.0-localToRemoteLostRate))
	//		read_request_media(conn, msg);
	//}
}

void simple_distributor::read_request_media(peer_connection_sptr conn, const media_request_msg& msg)
{
	if(cas_string_.empty())
		cas_string_ = security_policy::generate_shared_key(msg.channel_id(), "");
	
	//尝试让handler去处理
	//最后执行的read_request_media的simple_distributor::delegate_handler_必定为NULL
	//不会形成死循环调用
	//实际上是形成一个distributor的链条, 请求的seqno不在dis1则移动到dis2，直到所有的dis都检查了一遍是不是自己负责的区间
	//目前的问题是，链条中某个disX已经处理了请求，其它的dis不知道，
	//始终进行了判断[调用simple_distributor::read_request_media]，浪费了操作时间。
	//只有一个文件时, delegate_handler_为NULL
	if(delegate_handler_)
		delegate_handler_->read_request_media(conn, msg);
}

void simple_distributor::send_media_packet(peer_connection_sptr conn, 
	const std::string& channelID, seqno_t seqno, bool isCompletePkt, 
	const safe_buffer& buf, const error_code& ec)
{
	if (!conn || !conn->is_connected())
		return;

	if(ec) 
	{
		no_piece_msg msg;
		msg.set_seqno(seqno);
		conn->async_send_unreliable(serialize(msg), global_msg::no_piece);
		return;
	}

	timestamp_t now=timestamp_now();

	int cpuUsage=get_sys_cpu_usage(now);
	if(cpuUsage>90||cpuUsage>80&&in_probability(0.6))
		return;

	media_packet_rate_+=buffer_size(buf);
	if(scheduling_)
		scheduling_->out_speed_meter()+=buffer_size(buf);

	//asfile读取的1400大小, disk_cache读取的包含overhead，1432大小, 这里应该区分
	TODO("这个判断还不够准确(最后一个片段)");
	if(isCompletePkt) //读取的缓存
	{
		media_packet pkt(buf);
		boost::uint64_t seqnoVec;
		char* pvec=(char*)&seqnoVec;
		for (size_t j=0;j<sizeof(seqnoVec)/sizeof(seqno_t);++j)
		{
			write_int_hton<seqno_t>(seqno, pvec);
		}
		pkt.set_recent_seqno_map(seqnoVec);
		conn->send_media_packet(buf, seqno, now);
		
		SIMPLE_DISTRI_DBG(
		error_code err;
			std::cout<<"--send cache data to neighbor:"<<conn->remote_endpoint(err)<<", seq("<<seqno<<")\n";
		);
		return;
	}

	//////////////////////////////////////////////////////////////////////////
	media_packet pkt;
	safe_buffer_io io(&pkt.buffer());
	io.prepare(buf.length()+pkt.buffer().length());

	pkt.set_seqno(seqno);
	pkt.set_time_stamp(seqno);
	//pkt.set_pts_pre_stamp(0);
	pkt.set_session_id(0);
	pkt.set_hop(0);
	pkt.set_packet_rate(packet_rate());//

	boost::uint64_t seqnoVec;
	char* pvec=(char*)&seqnoVec;
	for (size_t j=0;j<sizeof(seqnoVec)/sizeof(seqno_t);++j)
	{
		write_int_hton<seqno_t>(seqno+seqno_t(j+100), pvec);
	}
	pkt.set_recent_seqno_map(seqnoVec);
	pkt.set_is_push(0);

	io.write(buffer_cast<char*>(buf), buffer_size(buf));
	security_policy::cas_mediapacket(pkt, cas_string_);

	boost::int64_t sig = security_policy::signature_mediapacket(pkt, channelID);
	pkt.set_anti_pollution_signature(sig);

	//std::cout<<buf.length()<<"\n";
	conn->send_media_packet(pkt.buffer(), seqno, now);
}

int simple_distributor::packet_rate()const
{
	return (int)media_packet_rate_.bytes_per_second();
}
int simple_distributor::bit_rate()const
{
	if(bit_rate_)//如果有解析出来的bitRate，则用这个值
		return *bit_rate_;

	if(film_duration_>1000)
		return ((file_length_/1024)/(film_duration_/1000))*8;
	return 0;
}
int simple_distributor::out_kbps()const
{
	return (int)(media_packet_rate_.bytes_per_second())*8/1024;//kb
}

int simple_distributor::p2p_efficient()const
{
	if(!incomming_packet_rate_.bytes_per_second())
		return 100;

	return 100*(packet_rate()/incomming_packet_rate_.bytes_per_second());
}
void simple_distributor::on_recvd_join_channel(peer_connection* conn, 
	safe_buffer buf)
{
	SIMPLE_DISTRI_DBG(std::cout<<"on_recvd_join_channel msg"<<std::endl;);
	join_channel_msg msg;
	if (!parser(buf, msg))
		return;
	peer_connection_sptr sock = conn->shared_obj_from_this<peer_connection>();
	sock->channel_id(msg.channel_id());
	sockets_.insert(sock);
	pending_sockets_.erase(sock);
}

void simple_distributor::on_recvd_buffermap_request(
	peer_connection*conn, safe_buffer buf)
{
}

p2simple::off_type simple_distributor::get_duration()
{	
	return film_duration_;
}
p2simple::off_type simple_distributor::get_length()
{
	return file_length_;
}

void simple_distributor::cal_channel_file_info(const boost::filesystem::path& file)
{
	film_duration_ = p2common::get_duration(file, bit_rate_);
	file_length_ = p2common::get_length(file);

}
void simple_distributor::set_len_duration(p2simple::off_type len, p2simple::off_type dur)
{
	file_length_ = len;
	film_duration_ = dur;
}

void simple_distributor::__send_cache_media(peer_connection_sptr conn, const std::string& ID/*peerID or channelID*/
						, seqno_t seqno, const safe_buffer& buf)
{
	if(conn&&conn->is_connected())
	{
		safe_buffer sbuf;
		safe_buffer_io io(&sbuf);
		io<<int32_t(ID.size());
		io<<ID;
		io<<seqno_t(seqno);
		io<<buf;

		conn->async_send_reliable(sbuf, global_msg::cache_media);
	}
}