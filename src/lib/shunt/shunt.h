#ifndef _SHUNT_SHUNT_H__
#define _SHUNT_SHUNT_H__

#include <boost/property_tree/ptree.hpp>

#include "shunt/fluid_receiver.h"
#include "shunt/fluid_sender.h"
#include "shunt/media_receiver.h"
#include "shunt/media_sender.h"
#include "shunt/fluid_media_convert.h"
#include "shunt/typedef.h"

/*
<?xml version="1.0" encoding="UTF-8" ?>
<!-- ���������� -->
<shunt id="1">
<!-- ����������������ֻ��ʹ����������һ����ֻ�ܽ���һ·���� -->
<receive>
<!-- ����UDP������ -->
<unicast>
<endpoint>:3000</endpoint>
</unicast>

<!-- ����UDP�鲥�� -->
<multicast>
<endpoint>239.0.0.0:4000</endpoint>
</multicast>

<!-- ����p2s�������� -->
<p2s>
<endpoint>210.77.16.29:7000</endpoint>
</p2s>
</receive>

<!-- ������ -->
<send>
<!-- �������ͣ���һ���������󵽴�ʱ������������ӷ����� -->
<acceptor>
<!-- HTTP���� -->
<http>
<endpoint>:6000</endpoint>
</http>

<!-- P2S��������� -->
<p2s>
<endpoint>:7000</endpoint>
</p2s>
</acceptor>

<!-- �������͵�����Ŀ�ĵ�ַ -->
<unicast>
<endpoint>192.168.0.1:5000</endpoint>
<endpoint>192.168.0.2:5000</endpoint>
<endpoint>192.168.0.3:5000</endpoint>
</unicast>
</send>
</shunt>
*/
namespace p2shunt{

	class p2sshunt
		:public basic_engine_object
	{
		typedef p2sshunt this_type;
		SHARED_ACCESS_DECLARE;
	public:
		explicit p2sshunt(io_service& ios);
		virtual ~p2sshunt();

		void run(const std::string& xmlPath="shunt.xml");
		void run(const shunt_xml_param& param);
		
		void stop();

		void load_config(const std::string& xmlPath);
		void load_config(const boost::property_tree::ptree& pt);
		void load_config(const shunt_xml_param& param);
		
		void start_one_kind_recv(const std::string& url);
		void start_sender(const std::set<std::string>& urls);

		void shunt(const safe_buffer& buf);

		double  average_media_speed()
		{
			if(receiver_) return receiver_->average_media_speed();
			return 0;
		}
		double  average_packet_speed()//packets per second
		{
			if(receiver_) return receiver_->average_packet_speed();
			return 0;
		}
		double instantaneous_media_speed()//kbps
		{
			if(receiver_) return receiver_->instantaneous_media_speed();
			return 0;
		}
		double  instantaneous_packet_speed()//packets per second
		{
			if(receiver_) return receiver_->instantaneous_packet_speed();
			return 0;
		}

		bool is_connected()
		{
			if(receiver_) return receiver_->is_connected();
			return false;
		}
		const std::string& id()const
		{
			return id_;
		}


		static bool load_config(const boost::property_tree::ptree& pt,
			shunt_xml_param& param);
		static bool load_config_v1(const boost::property_tree::ptree& pt, 
			shunt_xml_param& param);
		static bool load_config_v2(const boost::property_tree::ptree& pt, 
			shunt_xml_param& param);

	private:
		void debug_print();

	private:
		static void try_delay(const std::string& url); 
		static void append_key_for_compatible(std::string& url);

	private:
		std::string								id_;
		boost::shared_ptr<receiver>				receiver_;
		std::set<boost::shared_ptr<sender> >	senders_;
		boost::shared_ptr<sender>				unicast_sender_;

		fluid_to_media fluid_to_media_;
		media_to_fluid media_to_fluid_;

		boost::shared_ptr<rough_timer> debug_print_timer_;
	};

}

#endif // HTTP_SERVER4_SERVER_HPP
