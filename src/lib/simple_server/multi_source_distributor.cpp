#include "simple_server/multi_source_distributor.h"

using namespace p2simple;

multi_source_distributor::~multi_source_distributor()
{	
	if(connect_timer_)
	{
		connect_timer_->cancel();
		connect_timer_.reset();
	}
}

void multi_source_distributor::register_message_handler(peer_connection_sptr conn)
{
	simple_distributor::register_message_handler(conn);
#define REGISTER_HANDLER(msgType, handler)\
	conn->register_message_handler(msgType, boost::bind(&this_type::handler, this, conn.get(), _1));

	REGISTER_HANDLER(global_msg::cache_media, on_recvd_media_packet);
	REGISTER_HANDLER(global_msg::no_piece, on_recvd_missing_pieces_report);

#undef REGISTER_HANDLER
}

void multi_source_distributor::start(peer_info& local_info)
{
	base_distributor_type::start(local_info);

	for (int i=0;i<local_info.cache_server_ipport_size();++i)
	{
		cache_server_ipport_.push_back(local_info.cache_server_ipport(i));
	}
	//主动去连接memory_distributor
	connect();

	if(!connect_timer_)
	{
		connect_timer_=timer::create(get_io_service());
		connect_timer_->register_time_handler(boost::bind(&this_type::connect, this));
	}
	if(!timeout_timer_)
	{
		timeout_timer_=timer::create(get_io_service());
		timeout_timer_->register_time_handler(boost::bind(&this_type::on_request_time_out, this));
		timeout_timer_->async_keep_waiting(milliseconds(1300), milliseconds(600));
	}
}

void multi_source_distributor::connect()
{
	const std::string domain=cache_server_demain+"/MEMORY_CACHE";

	endpoint remoteEdp=endpoint_from_string<endpoint>("127.0.0.1:9876");
	if(!cache_server_ipport_.empty())
		remoteEdp=endpoint_from_string<endpoint>(cache_server_ipport_.back());
	TODO("支持多个cache server");


	error_code ec;
	if(peer_socket_)
	{
		peer_socket_->close();
		peer_socket_.reset();
	}

	peer_socket_=trdp_peer_connection::create(get_io_service(), true);
	peer_socket_->open(endpoint(), ec);
	peer_socket_->register_connected_handler(
		boost::bind(&this_type::on_connected, this, peer_socket_.get(), _1)
		);
	peer_socket_->async_connect(remoteEdp, domain);
}

void multi_source_distributor::on_connected(peer_connection* conn, const error_code& ec)
{
	if(ec)
	{	
		DEBUG_SCOPE(
			std::cout<<"multi_source_distributor::connect error: "<<ec.message()<<std::endl;
		);
		connect_timer_->cancel();
		connect_timer_->async_wait(seconds(1));
		return;
	}
	if (conn!=peer_socket_.get())
		return;

	if(connect_timer_)
		connect_timer_->cancel();

	register_message_handler(conn->shared_obj_from_this<peer_connection>());
	conn->register_disconnected_handler(boost::bind(&this_type::on_cache_svr_disconnected, this, conn, _1));
}

void multi_source_distributor::on_cache_svr_disconnected(peer_connection* conn, const error_code& ec)
{
	DEBUG_SCOPE(
		std::cout<<"multi_source_distributor::on_cache_svr_disconnected, "<<ec.message()<<std::endl;
	);
	connect_timer_->async_wait(seconds(1));
}

void multi_source_distributor::__on_disconnected(peer_connection* conn, const error_code& ec)
{
	base_distributor_type::__on_disconnected(conn, ec);

	conn_set_.conn_index().erase(conn->shared_obj_from_this<peer_connection>());
}

void multi_source_distributor::read_request_media(peer_connection_sptr conn, 
	const media_request_msg& msg)
{
	if(!peer_socket_||!peer_socket_->is_connected())
	{
		base_distributor_type::read_request_media(conn, msg);
		return;
	}
	if(!conn||!conn->is_connected())
	{
		base_distributor_type::read_request_media(conn, msg);
		return;
	}

	//这里增加peerID以区分是谁请求的
	timestamp_t now=timestamp_now();
	conn_set::peer_id_index_type::iterator itr
		=conn_set_.peer_id_index().find(msg.peer_id());
	if(conn_set_.peer_id_index().end()==itr)
	{
		BOOST_ASSERT(conn&&conn->is_connected());
		conn_elm elm;
		elm.conn=conn;
		elm.peerID=msg.peer_id();
		for (int i=0;i<msg.seqno_size();++i)
		{
			elm.seqnos_[msg.seqno(i)]=now;
		}

		conn_set_.peer_id_index().insert(elm);
	}
	else
	{
		conn_elm& elm=const_cast<conn_elm&>(*itr);
		for (int i=0;i<msg.seqno_size();++i)
		{
			elm.seqnos_[msg.seqno(i)]=now;
		}
	}
	BOOST_ASSERT(conn_set_.peer_id_index().end()!=conn_set_.peer_id_index().find(msg.peer_id()));

	//直接把读取服务转到cache服务器，cache服务器返回的没有seqno再从磁盘
	peer_socket_->async_send_reliable(serialize(msg), global_msg::media_request);
}

void multi_source_distributor::on_recvd_missing_pieces_report(
	peer_connection* conn, const safe_buffer& buf)
{
	media_request_msg msg;
	if(!parser(buf, msg))
	{
		BOOST_ASSERT(0&&"这些seqno没人处理了");
		return;
	}

	/*这里给client peer 的conn发送*/
	BOOST_AUTO(itr, conn_set_.peer_id_index().find(msg.peer_id()));
	//BOOST_ASSERT(itr!=conn_set_.peer_id_index().end());
	if(itr!=conn_set_.peer_id_index().end())
	{
		conn_elm& elm=const_cast<conn_elm&>(*itr);
		for (int i=0;i<msg.seqno_size();++i)
		{	
			elm.seqnos_.erase(msg.seqno(i));
		}

		if(!elm.conn||!elm.conn->is_connected())
		{
			conn_set_.peer_id_index().erase(itr);
			return;
		}
		DEBUG_SCOPE(
			if(in_probability(0.01))
				std::cout<<"cache server has no piece, seqmin="
				<<msg.seqno(0)<<" seqmax="
				<<msg.seqno(msg.seqno_size()-1)<<std::endl;
		);

		BOOST_ASSERT(elm.conn&&elm.conn->is_connected());
		base_distributor_type::read_request_media(elm.conn, msg);
	}
}

void multi_source_distributor::send_media_packet(peer_connection_sptr conn, const std::string& channelID, 
	seqno_t seqno, bool isCompletePkt, const safe_buffer& buf, const error_code& ec)
{
	simple_distributor::send_media_packet(conn, channelID, seqno, isCompletePkt, buf, ec);

	//send media to cache server
	__send_cache_media(peer_socket_, channelID, seqno, buf);
}

void multi_source_distributor::on_recvd_media_packet(
	peer_connection* conn/*conn to mem_svr*/, safe_buffer buf)
{
	
	typedef boost::function<void(const std::string&, seqno_t, const safe_buffer&)> send_cache_func_type;
	send_cache_func_type send_func = boost::bind(
		&this_type::send_cache_media_packet_to_peer, SHARED_OBJ_FROM_THIS, _1, _2, _3);

	simple_distributor::__on_recvd_cached_media<send_cache_func_type>(buf, send_func);
	/*	区别？
	safe_buffer_io io( &buf );
	int32_t size = 0;
	std::string ID;
	seqno_t seqno;
	io>>size;
	io.read(ID, size);
	io>>seqno;
	send_cache_media_packet_to_peer(ID, seqno, buf);
	*/
}

void multi_source_distributor::send_cache_media_packet_to_peer(
	const std::string& peerID, seqno_t seqno, const safe_buffer& buf)
{
	/*这里给client peer 的conn发送*/
	BOOST_AUTO(itr, conn_set_.peer_id_index().find(peerID));
	if(itr!=conn_set_.peer_id_index().end())
	{
		error_code ec;
		conn_elm& elm=const_cast<conn_elm&>(*itr);
		elm.seqnos_.erase(seqno);

		BOOST_ASSERT(elm.conn&&elm.conn->is_connected());
		base_distributor_type::send_media_packet(
			elm.conn, elm.conn->channel_id(), seqno, false, buf, ec);

		DEBUG_SCOPE(
			if(in_probability(0.01)){
				std::cout<<"^^^^^^^^^^^^^^^^^^^^^recv cached seq="<<seqno<<" size="<<buf.size()<<"----------------------\n";
			}
			);
	}
}

void multi_source_distributor::on_request_time_out()
{
	//TODO("服务没起或mem_cache服务器断了");
	timestamp_t now=timestamp_now();
	for (BOOST_AUTO(itr, conn_set_.peer_id_index().begin());
		itr!=conn_set_.peer_id_index().end();++itr)
	{
		const conn_elm& elm=*itr;
		if(!elm.conn||!elm.conn->is_connected())
			continue;

		media_request_msg msg;
		msg.set_direct_request(true);
		msg.set_peer_id(elm.peerID);
		msg.set_channel_id(elm.conn->channel_id());

		conn_elm::seqno_set_type& seqnoSet=const_cast<conn_elm::seqno_set_type&>(elm.seqnos_);
		for (BOOST_AUTO(seqItr, seqnoSet.begin());
			seqItr!=seqnoSet.end();++seqItr)
		{
			if(is_time_passed(TIMEOUT_INTERVAL, seqItr->second, now))
			{
				msg.add_seqno(seqItr->first);
			}
		}
		for (int i=0;i<msg.seqno_size();++i)
		{
			seqnoSet.erase(msg.seqno(i));
		}

		if(msg.seqno_size())
		{
			DEBUG_SCOPE(
				std::cout<<"xxxxxxxxxxxxxxxxxxxxxx-time out-, read from dsk, size="<<msg.seqno_size()
				<<"seqmin="<<msg.seqno(0)
				<<"seqmax="<<msg.seqno(msg.seqno_size()-1)
				<<"~~~~~~~~~~~~~~~~~~~~~~~~~\n";
			);
			BOOST_ASSERT(elm.conn&&elm.conn->is_connected());
			base_distributor_type::read_request_media(elm.conn, msg);
		}
	}
}