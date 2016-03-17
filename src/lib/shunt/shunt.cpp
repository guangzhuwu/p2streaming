/*
<?xml version="1.0" encoding="UTF-8" ?>
<!-- ���������� -->
<shunt>
<id>9f5fe01adbe2b61d123bfddbd06b84e5</id> 
<!-- ����������������ֻ��ʹ����������һ����ֻ�ܽ���һ·���� -->
<receive>
<url></url> 
</receive>

<!-- ������ -->
<send>
<!-- �������ͣ���һ���������󵽴�ʱ������������ӷ�����, (�����ж��) -->
<acceptor>
<url>udp://localhost:1234</url>
<url>udp://localhost:8000</url>
<url>http://localhost:8080/abcdefg</url>
</acceptor>
</send>
</shunt>
*/

#include "shunt/shunt.h"

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/format.hpp>
#include <boost/foreach.hpp>

#include "shunt/typedef.h"
#include "shunt/creator.h"

NAMESPACE_BEGIN(p2shunt);

p2sshunt::p2sshunt(io_service& ios)
	:basic_engine_object(ios)
{
	g_back_fetch_duration=100;
	g_delay_guarantee=30*1000;
	g_b_fast_push_to_player=false;
	g_b_tracker_using_rudp=false;
	g_b_streaming_using_rtcp=false;
}

p2sshunt::~p2sshunt()
{
}

void p2sshunt::run(const std::string& xml)
{
	system_time::sleep(millisec(random(0, 5000)));//��ֹ�ܶ�shuntͬʱ����
	load_config(xml);

	debug_print_timer_=rough_timer::create(get_io_service());
	//debug_print_timer_->set_obj_desc("p2shunt::p2sshunt::debug_print_timer_");
	debug_print_timer_->register_time_handler(boost::bind(&this_type::debug_print, this));
	debug_print_timer_->async_keep_waiting(seconds(1), seconds(1));
}

void p2sshunt::run(const shunt_xml_param& param)
{
	system_time::sleep(millisec(random(0, 5000)));//��ֹ�ܶ�shuntͬʱ����
	load_config(param);

	debug_print_timer_=rough_timer::create(get_io_service());
	//debug_print_timer_->set_obj_desc("p2shunt::p2sshunt::debug_print_timer_");
	debug_print_timer_->register_time_handler(boost::bind(&this_type::debug_print, this));
	debug_print_timer_->async_keep_waiting(seconds(1), seconds(1));
}

void p2sshunt::stop()
{
}

void p2sshunt::debug_print()
{
	std::cout<<"is_connected="<<(is_connected())
		<<", average_kbps="<<(int)average_media_speed()
		<<", instantaneous_kbps="<<(int)instantaneous_media_speed()
		<<", average_packets="<<(int)average_packet_speed()
		<<", instantaneous_packets="<<(int)instantaneous_packet_speed()
		<<"\n";
}

void p2sshunt::load_config(const std::string& filepath)
{
	using boost::property_tree::ptree;
	try
	{
		ptree pt;
		boost::property_tree::read_xml(filepath, pt);
		load_config(pt);
	}
	catch (std::exception& e)
	{
		std::cout<<e.what()<<std::endl;
		BOOST_ASSERT(0&&"xml parser errro!!");
	}
}

void p2sshunt::load_config(const boost::property_tree::ptree& pt)
{
	shunt_xml_param param;
	if(load_config(pt, param))
	{
		start_one_kind_recv(param.receive_url);
		start_sender(param.send_urls);
	}
}
bool p2sshunt::load_config(const boost::property_tree::ptree& pt,
	shunt_xml_param& param)
{
	return load_config_v1(pt, param)
		||load_config_v2(pt, param);
}

bool p2sshunt::load_config_v1(const boost::property_tree::ptree& pt,
	shunt_xml_param& param)
{
	return false;//����v1
	using boost::property_tree::ptree;
	try
	{
		boost::optional<int> Version=pt.get_optional<int>("shunt.version");
		if(Version&&1!=*Version)
			return false;

		bool ok=false;
		//id
		boost::optional<std::string> idStr =
			pt.get_optional<std::string>("shunt.id");
		if (idStr)
			param.id=*idStr;

		//recv
		boost::optional<std::string> recv_unicast_endpoint = 
			pt.get_optional<std::string>("shunt.receive.unicast.endpoint");
		boost::optional<std::string> recv_multicast_endpoint = 
			pt.get_optional<std::string>("shunt.receive.multicast.endpoint");		
		boost::optional<std::string> recv_udp_endpoint = 
			pt.get_optional<std::string>("shunt.receive.multicast.udp");
		boost::optional<std::string> recv_p2s_endpoint =
			pt.get_optional<std::string>("shunt.receive.p2s.endpoint");
		boost::optional<std::string> recv_p2s_key =
			pt.get_optional<std::string>("shunt.receive.p2s.key");
		boost::optional<std::string> recv_http_url = 
			pt.get_optional<std::string>("shunt.receive.http.url");
		boost::optional<std::string> recv_url = 
			pt.get_optional<std::string>("shunt.receive.url");
		int b_recv_unicast_endpoint=(recv_unicast_endpoint&&recv_unicast_endpoint->length()>0)?1:0;
		int b_recv_multicast_endpoint=(recv_multicast_endpoint&&recv_multicast_endpoint->length()>0)?1:0;
		int b_recv_udp_endpoint=(recv_udp_endpoint&&recv_udp_endpoint->length()>0)?1:0;
		int b_recv_p2s_endpoint=(recv_p2s_endpoint&&recv_p2s_endpoint->length()>0)?1:0;
		int b_recv_http_endpoint=(recv_http_url)?1:0;
		int b_recv_url=(recv_url)?1:0;

		if ((b_recv_unicast_endpoint+b_recv_multicast_endpoint+b_recv_udp_endpoint
			+b_recv_p2s_endpoint+b_recv_http_endpoint+b_recv_url
			)>1
			)
		{
			std::cout<<"error: shunt.receive must use ONLY ONE kind of unicast/multicast/p2s !"
				<<std::endl;
			return false;
		}
		else if (b_recv_unicast_endpoint)
		{
			std::string url=std::string("udp://")+(*recv_unicast_endpoint);
			param.receive_url=url;
		}
		else if (b_recv_multicast_endpoint)
		{
			std::string recv_url=std::string("udp://")+(*recv_multicast_endpoint);
			param.receive_url=recv_url;
			ok=true;
		}
		else if (b_recv_udp_endpoint)
		{
			std::string url=std::string("udp://")+(*recv_udp_endpoint);
			param.receive_url=url;
			ok=true;
		}
		else if (b_recv_p2s_endpoint)
		{
			std::string url=std::string("shunt://") + *recv_p2s_endpoint+std::string("/?");
			if (recv_p2s_key)
				url+=std::string("key=")+*recv_p2s_key;
			else
				url+=std::string("key=")+default_channel_key;
			
			param.receive_url=url;
			ok=true;
		}
		else if (b_recv_http_endpoint)
		{
			g_back_fetch_duration=100;
			g_delay_guarantee=80*1000;
			std::string url=std::string("http://") + *recv_http_url;
			param.receive_url=url;
			ok=true;
		}
		else if (b_recv_url)
		{
			if (recv_url->find("http://")!=std::string::npos)
			{
				g_back_fetch_duration=100;
				g_delay_guarantee=80*1000;
			}
			param.receive_url=*recv_url;
			ok=true;
		}
		//delay����
		boost::optional<int> delay = pt.get_optional<int>("shunt.receive.http.delay");
		if (!delay)
			delay = pt.get_optional<int>("shunt.receive.http.time");
		if (!delay)
			delay = pt.get_optional<int>("shunt.receive.p2s.delay");
		if (!delay)
			delay = pt.get_optional<int>("shunt.receive.p2s.time");
		if (!delay)
			delay = pt.get_optional<int>("shunt.receive.delay");
		if (!delay)
			delay = pt.get_optional<int>("shunt.receive.time");
		if (delay)
		{
			int origDelay=bound(2*MIN_DELAY_GUARANTEE, (*delay)*1000, MAX_DELAY_GUARANTEE);
			g_back_fetch_duration=std::min(origDelay/100, 100);
			g_delay_guarantee=origDelay;
		}


		//send.acceptor
		boost::optional<std::string> send_acceptor_http_endpoint = 
			pt.get_optional<std::string>("shunt.send.acceptor.http.endpoint");
		if (send_acceptor_http_endpoint&&send_acceptor_http_endpoint->length()>0)
		{
			std::string recv_url=std::string("http://") + *send_acceptor_http_endpoint;
			param.send_urls.insert(recv_url);
			ok=true;
		}
		boost::optional<std::string> send_acceptor_p2s_endpoint =
			pt.get_optional<std::string>("shunt.send.acceptor.p2s.endpoint");
		boost::optional<std::string> send_acceptor_p2s_external_endpoint =
			pt.get_optional<std::string>("shunt.send.acceptor.p2s.external_endpoint");
		boost::optional<std::string> key = 
			pt.get_optional<std::string>("shunt.send.acceptor.p2s.key");
		boost::optional<std::string> send_url = 
			pt.get_optional<std::string>("shunt.send.url");
		if (send_acceptor_p2s_endpoint&&send_acceptor_p2s_endpoint->length()>0)
		{
			if (!key)
				key=default_channel_key;

			std::string recv_url=std::string("shunt://")+*send_acceptor_p2s_endpoint +"/?";
			if (send_acceptor_p2s_external_endpoint)
				recv_url +="external_address="+*send_acceptor_p2s_external_endpoint;
			recv_url+="&key="+*key;

			param.send_urls.insert(recv_url);
			ok=true;
		}
		//����\�鲥
		{
			std::set<std::string>send_unicast_endpoints;
			const char* const xcastStr[]={"unicast", "multicast", "udp"};
			for (int i=0;i<sizeof(xcastStr)/sizeof(xcastStr[0]);++i)
			{
				const boost::optional<const ptree&> v=pt.get_child_optional(
					std::string("shunt.send.")+xcastStr[i]);
				if (v)
				{
					ptree::const_iterator itr=v->begin();
					for(;itr!=v->end();++itr)
					{
						if (itr->first=="endpoint")
						{
							std::string recv_url=std::string("udp://")+itr->second.data();
							param.send_urls.insert(recv_url);
						}
						ok=true;
					}
				}
			}
		}
		//url
		if (send_url&&send_url->length()>0)
		{
			param.send_urls.insert(*send_url);
			ok=true;
		}
		return ok;

	}
	catch (std::exception& e)
	{
		std::cout<<e.what()<<std::endl;
		BOOST_ASSERT(0&&"xml parser errro!!");
		return false;
	}
	catch (...)
	{
		return false;
	}
}

bool p2sshunt::load_config_v2(const boost::property_tree::ptree& pt,
						shunt_xml_param& param)
{
	using boost::property_tree::ptree;
	try
	{
		bool ok=false;
		//id
		boost::optional<std::string> idStr =
			pt.get_optional<std::string>("shunt.id");
		if (idStr)
			param.id=*idStr;

		//recv
		boost::optional<std::string> recv_url = 
			pt.get_optional<std::string>("shunt.receive.url");
		BOOST_ASSERT(recv_url);
		if (recv_url)
		{
			append_key_for_compatible(*recv_url);
			param.receive_url=*recv_url;
			try_delay(*recv_url);
			ok=true;
		}

		//send.acceptor
		std::set<std::string>send_unicast_endpoints;
		boost::optional<const ptree&> v=pt.get_child_optional(
			std::string("shunt.send."));
		if (v)
		{
			ptree::const_iterator itr=v->begin();
			for(;itr!=v->end();++itr)
			{
				if (itr->first=="url")
				{
					param.send_urls.insert(itr->second.data());
					ok=true;
				}
			}
		}
		return ok;
	}
	catch (std::exception& e)
	{
		std::cout<<e.what()<<std::endl;
		BOOST_ASSERT(0&&"xml parser errro!!");
		return false;
	}
	catch (...)
	{
		return false;
	}
}

void p2sshunt::load_config(const shunt_xml_param& param)
{
	id_ = param.id;

	//receive
	if(!param.receive_url.empty())
	{
		std::string realUrl=param.receive_url;
		append_key_for_compatible(realUrl);
		try_delay(realUrl);
		start_one_kind_recv(realUrl);
	}

	start_sender(param.send_urls);
}

void p2sshunt::start_one_kind_recv(const std::string& url)
{
	if(url.empty())
		return;

	error_code ec;
	if (receiver_)
		receiver_.reset();
	if (uri(url, ec).protocol() == "http")
		receiver_ = receiver_creator::create(url, get_io_service(), true);
	else
		receiver_ = receiver_creator::create(url, get_io_service(), false);

	receiver_->updata(url, ec);
	receiver_->media_handler = boost::bind(&this_type::shunt, this, _1);
}

void p2sshunt::start_sender(const std::set<std::string>& urls)
{
	BOOST_FOREACH(const std::string& url, urls)
	{
		error_code ec;
		uri u(url, ec);
		if (ec)
			continue;
		if("udp"==u.protocol())
		{
			if(!unicast_sender_)
			{
				unicast_sender_=sender_creator::create(url, get_io_service());
				senders_.insert(unicast_sender_);
			}
			if(unicast_sender_)
				unicast_sender_->updata(url, ec);
		}
		else
		{
			boost::shared_ptr<sender> sender=sender_creator::create(url, get_io_service());
			if(sender)
			{
				sender->updata(url, ec);
				senders_.insert(sender);
			}
		}
	}
}

void p2sshunt::shunt(const safe_buffer& buf)
{
	BOOST_FOREACH(boost::shared_ptr<sender> Sender, senders_)
	{
		Sender->shunt(buf);
	}
}

void p2sshunt::try_delay(const std::string& url)
{
	error_code ec;
	uri u(url, ec);
	if (ec)
		return;
	if("shunt"!=u.protocol())
		return;

	std::string delay=u.query_map()["delay"];
	BOOST_ASSERT(!delay.empty());
	if (!delay.empty())
	{
		int idelay=boost::lexical_cast<int>(delay);
		int origDelay=bound(2*MIN_DELAY_GUARANTEE, idelay*1000, MAX_DELAY_GUARANTEE);
		g_back_fetch_duration=std::min(origDelay/100, 100);
		g_delay_guarantee=origDelay;
	}
}

void p2sshunt::append_key_for_compatible(std::string& url)
{
	//�ɰ汾��shuntû������keyʱ��ʹ��default_key
	error_code ec;
	uri u(url, ec);
	if (ec)
		return;
	if("shunt"!=u.protocol())
		return;

	const std::string& key=(u.query_map())["key"];
	if(key.empty())
	{
		url.append("&key=");
		url.append(default_channel_key.c_str(), default_channel_key.length());
	}
}

NAMESPACE_END(p2shunt);
