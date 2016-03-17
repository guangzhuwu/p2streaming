#include "p2s_mds/media_server.h"
#include "shunt/creator.h"
#include <p2engine/push_warning_option.hpp>
#include <sstream>
#include <strstream>
#include <boost/lexical_cast.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <p2engine/pop_warning_option.hpp>

p2s_mds::p2s_mds(io_service& ios,
	const server_param_base& param, 
	const std::string& fluidistorUrl
	)
	:server_service_logic_base(ios)
	, fluidistor_url_(fluidistorUrl)
	, param_(param)
{
}

p2s_mds::~p2s_mds()
{
	error_code ec;
	stop(ec);

	LogInfo("p2s mds deconstructor!");
}

void p2s_mds::start(error_code& ec)
{
	variant_endpoint edp=endpoint_from_string<udp::endpoint>(param_.internal_ipport);
	//�����IP�鲥���Ͳ�������P2P��
	if (is_multicast(edp.address()))
	{
		if (!multicast_socket_)
		{
			multicast_socket_.reset(new udp::socket(get_io_service()));
			multicast_socket_->open(boost::asio::ip::udp::v4(), ec);
			multicast_endpoint_=edp;
			if (ec)
			{
				error_code err;
				multicast_socket_->close(err);
				multicast_socket_.reset();
			}
		}
	}
	else 
	{
		server_service_logic_base::start(param_, ec);
		if (ec)
			return;
	}

	if(is_live_category(param_.type)&&!receiver_)
	{
		//media_relay_=media_relay::create(get_io_service(), fluidistorPort);
		if (uri(fluidistor_url_,ec).protocol()=="http")
			receiver_ = receiver_creator::create(fluidistor_url_, get_io_service(),true);
		else
			receiver_ = receiver_creator::create(fluidistor_url_, get_io_service(),false);

		receiver_->updata(fluidistor_url_, ec);
		if (ec)
		{
			receiver_.reset();
			//media_relay_->reset();
			return;
		}
		else
		{
			receiver_->media_handler=boost::bind(&this_type::handle_media, this, _1);
		}
	}
}

void p2s_mds::reset(error_code& ec)
{
	stop(ec);
	start(ec);
}

void p2s_mds::stop(error_code& ec)
{
	server_service_logic_base::stop();
	if(receiver_)
	{
		receiver_->stop();
		receiver_.reset();
	}

	if(multicast_socket_)
	{
		multicast_socket_->close(ec);
		multicast_socket_.reset();
	}

}

bool p2s_mds::open_distributor(const server_param_base& param)
{
	//���Ե�ַ�Ƿ����
	boost::system::error_code ec, bindEc;
	variant_endpoint edp=endpoint_from_string<tcp::endpoint>(param.internal_ipport);
	boost::asio::ip::tcp::socket socket(get_io_service());
	socket.open(boost::asio::ip::tcp::v4(), ec);
	socket.bind(edp, bindEc);
	socket.close(ec);
	return 	!bindEc;
}

void p2s_mds::handle_media(const safe_buffer& buf)
{
	const char* data=buffer_cast<const char*>(buf);
	int recvLen=(int)buffer_size(buf);

	//��IP�鲥����ʽ
	if (multicast_socket_&&multicast_endpoint_.port()!=0)
	{
		multicast_socket_->send_to(asio::buffer(data, recvLen), multicast_endpoint_, 0);
		return;
	}
	else//��P2PӦ�ò��鲥����ʽ
	{
		if (recvLen>0&&recvLen<1450)
		{
			smooth_distribute(buf, "", 0, USER_LEVEL);
		}
		else
		{
			std::cout<<"recv from vlc error, recvLen="<<recvLen<<"\n";
		}
	}
}

