#include "common/common.h"
#include "p2s_ppc/p2p_processor.hpp"
#include "client/client_service.h"
#include "client/pa_handler.h"

#include <p2engine/push_warning_option.hpp>
#include <boost/regex.hpp>
#include <boost/lexical_cast.hpp>
#include <string>
#include <p2engine/pop_warning_option.hpp>

NAMESPACE_BEGIN(ppc);

p2p_processor::p2p_processor(boost::shared_ptr<p2sppc_server> svr)
	:client_service_logic_base(svr->get_io_service())
	, state_(DISSCONNECTED)
	, mulicast_socket_(get_io_service())
	, p2sppc_server_(svr)
{
}

p2p_processor::~p2p_processor()
{
}

void p2p_processor::stop_channel(bool flush)
{
	if(DISSCONNECTED == state_)
	{
		BOOST_ASSERT(!client_service_);
		return;
	}

	state_=DISSCONNECTED;
	stop_service(flush);

	current_channel_link_.clear();
	for(BOOST_AUTO(itr, head_request_sockets_.begin());
		itr!=head_request_sockets_.end();++itr)
	{
		(*itr)->close(false);
	}
	head_request_sockets_.clear();

	for(BOOST_AUTO(itr, switch_request_sockets_.begin());
		itr!=switch_request_sockets_.end();++itr)
	{
		(*itr)->close(false);
	}
	switch_request_sockets_.clear();
	
	for(BOOST_AUTO(itr, stream_sockets_.begin());
		itr!=stream_sockets_.end();++itr)
	{
		(*itr)->close(!flush);
	}
	stream_sockets_.clear();

	for(BOOST_AUTO(itr, waiting_stream_socekets_.begin());
		itr!=waiting_stream_socekets_.end();++itr)
	{
		(*itr).first->close(false);
	}
	waiting_stream_socekets_.clear();

	idle_time_.reset();
	packets_.clear();
	if (mulicast_socket_.is_open())
	{
		error_code ec;
		mulicast_socket_.close(ec);
	}
}

void p2p_processor::start_channel(const client_param_base& param)
{
	BOOST_ASSERT(state_==DISSCONNECTED);

	state_=CONNECTING;
	current_channel_link_=param.channel_link;
	mulicast_endpoint_=endpoint_from_string<udp::endpoint>(param.tracker_host);
	if (is_multicast(mulicast_endpoint_.address()))
	{
		error_code ec;
		if (mulicast_socket_.is_open())
			mulicast_socket_.close(ec);
		mulicast_socket_.open(mulicast_endpoint_.protocol(), ec);
		mulicast_socket_.set_option(udp::socket::reuse_address(true), ec);
		mulicast_socket_.bind(udp::endpoint(address(), mulicast_endpoint_.port()), ec);
		mulicast_socket_.set_option(boost::asio::ip::multicast::join_group(mulicast_endpoint_.address()), ec);

		mulicast_buf_.resize(65536);
		mulicast_socket_.async_receive(
			boost::asio::buffer(&mulicast_buf_[0], mulicast_buf_.size()), 
			make_alloc_handler(
			boost::bind(&this_type::__on_multicast_recvd, 
			SHARED_OBJ_FROM_THIS, 
			boost::asio::placeholders::error, 
			boost::asio::placeholders::bytes_transferred)
			)
			);

		state_=CONNECTED;
		get_io_service().post(make_alloc_handler(
			boost::bind(&this_type::on_login_success, SHARED_OBJ_FROM_THIS)
			));
	}
	else
	{
		client_service_logic_base::start_service(param);
	}
}

bool p2p_processor::process(const uri& u, const http::request& req, 
	const connection_sptr& sock)
{
	struct get_distribution_type{
		int operator()(const std::string& typestr)const
		{
			if (boost::iequals(typestr, "interactive_live"))
				return INTERACTIVE_LIVE_TYPE;
			else if (boost::iequals(typestr, "live"))
				return LIVE_TYPE;
			else if (boost::iequals(typestr, "vod"))
				return VOD_TYPE;
			else if (boost::iequals(typestr, "bt"))
				return BT_TYPE;
			else
				return -1;
		}
	};
	struct get_sample_type{
		std::string operator()(const std::string& typestr)const
		{
			if(boost::iequals(typestr, "live"))
				return std::string("p2s");
			else if(boost::iequals(typestr, "vod"))
				return std::string("p2v");
			else
				return std::string("type_unknow");
		}
	};

	std::string decodedUrl=u.path();
	//"/p2p-(live)/tracker/key/(offset=offset)/channelid(.xx)";
	static const boost::regex p2pLiveReg(
		"/?p2p-(.+)/(\\d+\\.\\d+\\.\\d+\\.\\d+:\\d+)/([^/]+)/(offset=(\\d+)/)?([^\\.]+)(\\..*)?"
		);
	boost::smatch what;
	if( boost::regex_match(decodedUrl, what, p2pLiveReg) )
	{
		const std::string& typestr=what[1];
		const std::string& tracker=what[2];
		const std::string& key=what[3];
		const std::string& offset=what[5];
		const std::string& id=what[6];

		int distribtype=get_distribution_type()(typestr);
		if (distribtype<0)
		{
			p2sppc_server::close_connection(sock, http::response::HTTP_BAD_REQUEST);
			return true;
		}

		if(p2sppc_server_.expired())
			return true;
		const std::pair<std::string, std::string>& keyPair=p2sppc_server_.lock()->key_pair();

		client_param_base param;
		param.channel_key=hex_to_string(key);
		param.channel_link=id;
		param.channel_uuid=id;
		param.tracker_host=tracker;
		param.public_key=keyPair.first;
		param.private_key=keyPair.second;
		param.type=static_cast<distribution_type>(distribtype);
		param.offset =offset.empty()?0:boost::lexical_cast<int64_t>(offset);
		std::string range=req.get(http::HTTP_ATOM_Range);
		if(!range.empty())
		{
			boost::algorithm::to_lower(range);
			const boost::regex rangeReg(".*bytes\\s*=\\s*(\\d+)\\s*-\\s*(\\d*).*");
			boost::smatch rangeMatch;
			if( boost::regex_match(range, rangeMatch, rangeReg) )
			{
				const std::string& range1=rangeMatch[1];
				if (!range1.empty())
					param.offset=boost::lexical_cast<int64_t>(range1);
			}
		}
		if (current_channel_link_!=id)
		{
			stop_channel();
		}
		else
		{

		}
		if(state_==DISSCONNECTED)
		{
			start_channel(param);

			// add zyliu
			if( !p2sppc_server_.expired())
			{
				p2sppc_server_.lock()->report_viewing_info(p2client::OPT_CHANNEL_START,
					get_sample_type()(typestr), param.channel_link);
			}
			// add zyliu	
		}
		else if(is_vod_category(distribtype))
		{
			set_play_offset(param.offset);
		}
		current_play_offset_ = param.offset;
		register_stream_cmd_socket(sock, req, req.method()==http::HTTP_METHORD_HEAD);
		return true;
	}
	else
	{//�Ƿ���cmd
		std::map<std::string, std::string> qmap=u.query_map();
		const std::string& cmd=qmap["cmd"];
		if(cmd=="stop_chan")
		{
			const std::string& id=qmap["id"];
			if (id==current_channel_link_)
			{
				stop_channel();
				p2sppc_server::close_connection(sock, http::response::HTTP_OK);
			}
			return true;
		}
		if(cmd=="switch_chan")
		{
			const std::string& url=hex_to_string(qmap["url"]);
			BOOST_FOREACH(connection_sptr switchSock, switch_request_sockets_)
			{
				p2sppc_server::close_connection(switchSock, http::response::HTTP_REQUEST_TIMEOUT);
			}
			switch_request_sockets_.clear();
			switch_request_sockets_.insert(sock);
			error_code ec;
			process(uri(url, ec), req, connection_sptr());//
			return true;
		}
		else if (cmd=="range")
		{
			const std::string& id=qmap["id"];
			if (id==current_channel_link_)
				register_stream_cmd_socket(sock, req, true);
			else
				p2sppc_server::close_connection(sock, http::response::HTTP_NOT_FOUND);
			return true;
		}
		
	}

	return false;
}

void p2p_processor::register_stream_cmd_socket(connection_sptr sock, 
	const http::request& req, bool head_only)
{
	if (!sock)
	{
		return;
	}
	
	switch(state_)
	{
	case DISSCONNECTED:
		BOOST_ASSERT(0);
		p2sppc_server::close_connection(sock, http::header::HTTP_BAD_REQUEST);
		break;

	case CONNECTING:
		if(head_only)
			head_request_sockets_.insert(sock);
		else
			waiting_stream_socekets_.insert(std::make_pair(sock, req.has(http::HTTP_ATOM_Range)));
		sock->register_disconnected_handler(boost::bind(&this_type::on_disconnected, this, _1, sock.get()));
		break;

	case CONNECTED:
		if(head_only)
			response_header_socket(sock);
		else
			response_stream_socket(sock, req.has(http::HTTP_ATOM_Range));
		sock->register_disconnected_handler(boost::bind(&this_type::on_disconnected, this, _1, sock.get()));
		break;

	default:
		BOOST_ASSERT(0);
	}
}

void p2p_processor::response_stream_socket(connection_sptr conn, bool RangRequest)
{
	if (!conn||!conn->is_open())
		return;

	http::response res;
	if (RangRequest||get_client_param_sptr()->offset>0)
		res.status(http::header::HTTP_PARTIAL_CONTENT);
	else
		res.status(http::header::HTTP_OK);
	if(is_vod_category(get_client_param_sptr()->type))
	{
		BOOST_AUTO(vodInfo, client_service_ ->get_vod_channel_info());
		BOOST_ASSERT(vodInfo);
		int64_t fileLength=vodInfo->film_length();
		client_param_sptr clientParam=get_client_param_sptr();
		std::ostringstream ostr;
		ostr<<"bytes "<<clientParam->offset<<"-"<<(fileLength-1)<<"/"<<fileLength;
		res.content_length(fileLength-get_client_param_sptr()->offset);
		res.set(http::HTTP_ATOM_Content_Range, ostr.str());
		res.set("Content-Duration", boost::lexical_cast<std::string>(vodInfo->film_duration()));
		res.set(http::HTTP_ATOM_Accept_Ranges, "bytes");
	}
	else
	{
		//ֱ��������Range����
		res.status(http::header::HTTP_OK);
		res.set(http::HTTP_ATOM_Accept_Ranges, "none");
	}
	//res.content_type("text/plain");
	//res.content_type("application/octet-stream");
	res.set(http::HTTP_ATOM_Cache_Control, "no-cache");

	safe_buffer buf;
	safe_buffer_io bio(&buf);
	bio<<res;

	conn->async_send(buf);

	std::cout<<"---------ppc response-----------\n"
		<<res<<"\n";

	for(BOOST_AUTO(itr, packets_.begin());itr!=packets_.end();++itr)
		conn->async_send(itr->first);

	stream_sockets_.insert(conn);
}

void p2p_processor::response_header_socket(connection_sptr conn)
{
	if (!conn||!conn->is_open())
		return;

	http::response res;
	res.status(http::header::HTTP_NO_CONTENT);
	if(is_vod_category(get_client_param_sptr()->type))
	{
		BOOST_AUTO(vodInfo, client_service_ ->get_vod_channel_info());
		BOOST_ASSERT(vodInfo);
		int64_t fileLength=vodInfo->film_length();
		std::ostringstream ostr;
		ostr<<"bytes "<<get_client_param_sptr()->offset<<"-"<<(fileLength-1)<<"/"<<fileLength;

		res.content_length(fileLength-get_client_param_sptr()->offset);
		res.set(http::HTTP_ATOM_Content_Range, ostr.str());
	}
	else
	{
		//ֱ��������Range����
		res.status(http::header::HTTP_OK);
		res.set(http::HTTP_ATOM_Accept_Ranges, "none");
	}

	safe_buffer buf;
	safe_buffer_io bio(&buf);
	bio<<res;
	conn->async_send(buf);
}

void p2p_processor::on_disconnected(error_code ec, connection_type*conn)
{
	erase(conn->shared_obj_from_this<connection_type>());
}

void  p2p_processor::erase(connection_sptr sock)
{
	waiting_stream_socekets_.erase(sock);
	stream_sockets_.erase(sock);
	head_request_sockets_.erase(sock);
}


//////////////////////////////////////////////////////////////////////////
//��¼ʧ��
void p2p_processor::on_login_failed(error_code_enum code, const std::string& errorMsg)
{
	http::header::status_type statusCode=(code==e_unauthorized)?
		http::header::HTTP_UNAUTHORIZED : http::header::HTTP_SERVICE_UNAVAILABLE;

	for(BOOST_AUTO(itr, waiting_stream_socekets_.begin());
		itr!=waiting_stream_socekets_.end();++itr)
	{
		p2sppc_server::close_connection(itr->first, statusCode);
	}
	waiting_stream_socekets_.clear();

	for(BOOST_AUTO(itr, stream_sockets_.begin());itr!=stream_sockets_.end();++itr)
	{
		p2sppc_server::close_connection(*itr, statusCode);
	}
	stream_sockets_.clear();

	idle_time_.reset();

	stop_channel();
	return;
}

//��¼�ɹ�
void p2p_processor::on_login_success()
{
	if (state_==CONNECTED)
	{
		BOOST_ASSERT(waiting_stream_socekets_.empty());
		return;
	}
	state_=CONNECTED;
	
	for(BOOST_AUTO(itr, waiting_stream_socekets_.begin());
		itr!=waiting_stream_socekets_.end();++itr)
	{
		response_stream_socket(itr->first, itr->second);
	}
	waiting_stream_socekets_.clear();

	
	for (BOOST_AUTO(it, head_request_sockets_.begin());
		it!= head_request_sockets_.end(); ++it)
	{
		response_header_socket(*it);
	}
	head_request_sockets_.clear();

}

//����
void p2p_processor::on_droped()
{
}

//һ���½ڵ����ϵͳ
void p2p_processor::on_join_new_peer(const peer_id_t& newPeerID, const std::string& userInfo)
{
}
//������һ�����ڱ��ڵ����ߵĽڵ�
void p2p_processor::on_known_online_peer(const peer_id_t& newPeerID, const std::string& userInfo)
{
}
//�ڵ��뿪
void p2p_processor::on_known_offline_peer(const peer_id_t& newPeerID)
{
}

void p2p_processor::__on_multicast_recvd(error_code ec, size_t len)
{
	mulicast_socket_.async_receive(
		boost::asio::buffer(&mulicast_buf_[0], mulicast_buf_.size()), 
		make_alloc_handler(
		boost::bind(&this_type::__on_multicast_recvd, 
		SHARED_OBJ_FROM_THIS, 
		boost::asio::placeholders::error, 
		boost::asio::placeholders::bytes_transferred)
		)
		);

	if (ec)
		return;
	safe_buffer buf;
	safe_buffer_io io(&buf);
	io.write(&mulicast_buf_[0], len);

	on_recvd_media(buf, peer_id_t(), 0);
}

int p2p_processor::overstocked_to_player_media_size()
{
	std::size_t len=0;
	std::size_t n=0;
	typedef std::set<connection_sptr>::iterator iterator;
	iterator itr=stream_sockets_.begin();
	for(;itr!=stream_sockets_.end();)
	{
		if ((*itr)->is_open())
		{
			len+=(*itr)->overstocked_send_size();
			++n;
			++itr;
		}
		else
		{
			stream_sockets_.erase(itr++);
		}
	}
	if (n)
		return static_cast<int>(len/n);
	return 0;
}

void  p2p_processor::on_recvd_media(const p2engine::safe_buffer& buf, 
	const peer_id_t& srcPeerID, media_channel_id_t mediaChannelID
	)
{
	if (DISSCONNECTED==state_)
	{
		BOOST_ASSERT(!client_service_);
		return;
	}
	if (CONNECTED!=state_)
	{
		stop_channel();
		return;
	}

	ptime now=ptime_now();

	if (!is_vod_category(get_client_param_sptr()->type))
	{
		packets_.push_back(std::make_pair(buf, now));

		time_duration duration=seconds(6);
		while(is_time_passed(duration, packets_.front().second, now))
			packets_.pop_front();
	}
	
	if (!switch_request_sockets_.empty())
	{
		std::string reponse;
		reponse+="<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n Channel switch OK! ";
		reponse+="channel_link=";
		reponse+=current_channel_link_;
		for (BOOST_AUTO(it, switch_request_sockets_.begin());
			it!= switch_request_sockets_.end(); ++it)
		{
			p2sppc_server::close_connection(*it, http::header::HTTP_OK, reponse);
		}
		switch_request_sockets_.clear();
	}

	if (stream_sockets_.empty())
	{
		if (!idle_time_)
		{
			idle_time_=now;
		}
		else if(is_time_passed(seconds(6), *idle_time_, now))
		{
			stop_channel();

			// add zyliu
			if( !p2sppc_server_.expired())
			{
				p2sppc_server_.lock()->report_viewing_info(p2client::OPT_CHANNEL_STOP);
			}
			// add zyiu
			return;
		}
	}
	else
	{
		idle_time_.reset();

		typedef std::set<connection_sptr>::iterator iterator;
		iterator itr=stream_sockets_.begin();
		for(;itr!=stream_sockets_.end();)
		{
			if ((*itr)->is_open())
			{
				(*itr)->async_send(buf);
				++itr;
			}
			else
			{
				stream_sockets_.erase(itr++);
			}
		}
	}
}

void p2p_processor::on_media_end(const peer_id_t& srcPeerID, media_channel_id_t mediaChannelID)
{
	BOOST_AUTO(itr, stream_sockets_.begin());
	for (;itr!=stream_sockets_.end();)
	{
		if ((*itr)->is_open())
			(*itr)->close();
		stream_sockets_.erase(itr++);
	}
}

void p2p_processor::send_flv_header(connection_sptr conn)
{
	http::response res;
	res.status(http::header::HTTP_OK);
	res.content_type("video/x-flv");
	res.content_length((std::numeric_limits<boost::int64_t>::max)());
	std::string s;
	res.serialize(s);
	s+="FLV\x1\x1\0\0\0\x9\0\0\0\x9";
	safe_buffer buf;
	safe_buffer_io bio(&buf);
	bio.write(s.c_str(), s.length());
	conn->async_send(buf);
}


NAMESPACE_END(ppc);
