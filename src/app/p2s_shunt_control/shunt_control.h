#ifndef shunt_control_service_h__
#define shunt_control_service_h__

#include <list>

#include <boost/noncopyable.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/member.hpp>

#include <p2engine/p2engine.hpp>

#include "common/utility.h"
#include "common/message.pb.h"
#include "app_common/app_common.h"

namespace p2control{

	namespace multi_index = boost::multi_index;
	typedef alive::shunt_Alive shunt_Alive;
	class shunt_db;

	enum enum_cmd{
		CMD_UNDEF = 1,
		CMD_ADD,
		CMD_START,
		CMD_RESTART,
		CMD_STOP,
		CMD_DEL
	};

	class shunt_control
		: public control_base
	{
		typedef shunt_control this_type;
		SHARED_ACCESS_DECLARE;

		enum{ REPORT_INTERVAL = 5 };
		enum{ SHUNT_LIFE_THRESH = 4 };
		enum{ DELAY_INTERVAL = 200 };

	public:
		struct process_element
		{
			shunt_xml_param param;
			std::string id_;
			timestamp_t last_report_time_;
			pid_t		pid_;
			process_element(const shunt_xml_param& _param)
				:param(_param), id_(_param.id)
			{}
		};

		struct process_set
			: public multi_index::multi_index_container < process_element,
			multi_index::indexed_by <
			multi_index::ordered_unique < multi_index::member<process_element, std::string, &process_element::id_> >
			> >
		{
			typedef process_element    value_type;
			typedef nth_index<0>::type process_id_type;
			process_id_type& id_index(){ return multi_index::get<0>(*this); }
			const process_id_type& id_index() const { return multi_index::get<0>(*this); }
		};

	public:
		static this_type::shared_ptr create(io_service& ios)
		{
			return this_type::shared_ptr(new shunt_control(ios),
				shared_access_destroy<this_type>());
		}

	public:
		virtual void on_client_login(message_socket_sptr, const safe_buffer&);
		virtual void on_recvd_alarm_message(const safe_buffer& buf);

	protected:
		void delay_start();
		virtual void recover_from_db();

		virtual void __start();
		virtual void __set_operation_http_port(uint32_t port, std::string& errorMsg);
		virtual void __set_alive_alarm_port(uint32_t port, std::string& errorMsg);

		virtual void on_wild_sub_process_timer();
		virtual bool on_request_handler(const req_session& reqSess, std::string& errorMsg);
		virtual void on_sub_process_check_timer();

	private:
		bool __add_shunt(process_element& elm, std::string& errorMsg);
		bool __add_shunt(const std::string&Name, const std::string& xml, std::string& channel_link, std::string& errorMsg);
		bool __del_shunt(const std::string& link, std::string& errorMsg);
		bool __restart_shunt(const std::string& link, std::string& errorMsg);
		bool __start_shunt(const std::string& link, std::string& errorMsg);
		bool __stop_shunt(const std::string& link, std::string& errorMsg);

		bool __del_all_shunt(std::string& errorMsg);
		bool __restart_all_shunt(std::string& errorMsg);
		bool __start_all_shunt(std::string& errorMsg);
		bool __stop_all_shunt(std::string& errorMsg);

		bool __shunt_stopped(const process_element& elm, std::string& errorMsg);
		void __update_shunt_pid(const process_element& elm);
		void __recover_from_db(int delayTime);
		void __delay_add_shunt(const std::string&id);

		void check_or_recover_shunt(const std::string& id, std::string& errorMsg);
		void on_know_new_shunt(const std::string& id, int32_t pid);
	private:
		bool __db__set_channel(const shunt_xml_param& param
			, const shunt_Alive&msg);
		bool __db__del_channel(const std::string& link);

	protected:
		shunt_control(io_service& ios);
		~shunt_control();

	private:
		process_set suspended_process_;
		process_set running_process_;
		process_set recover_process_;

		std::auto_ptr<shunt_db> db_;
		boost::shared_ptr<rough_timer> recover_timer_;
		boost::shared_ptr<rough_timer> delay_timer_;
	};

};//namespace p2control

#endif // shunt_control_service_h__
