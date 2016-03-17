#ifndef monitor_h__
#define monitor_h__

#include <p2engine/p2engine.hpp>

#include <p2engine/push_warning_option.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/member.hpp>
#include <p2engine/pop_warning_option.hpp>

#include "common/typedef.h"
#include "p2s_mds_control/utility.h"
#include "app_common/app_common.h"

namespace p2control
{	
	using namespace p2engine;
	using namespace utility;
	namespace multi_index = boost::multi_index;
	
	class mds_control
		: public control_base
	{
		typedef mds_control			this_type;
		typedef std::string			cms_cmd_type;
		enum{MAX_LEN_PER_FILE=1024*1024*100, MAX_DURATION=60*60};
		SHARED_ACCESS_DECLARE;

		struct mds_element
		{
			typedef int			id_t;
			typedef std::string link_t;
			typedef std::string url_t;
			typedef boost::unordered_map<link_t, url_t>	link_url_map_t;
		
			//��Ϊkey�����ɸı�
			const id_t			id;
			const pid_t			pid;
			int					type;
			timestamp_t			last_report_time;
			link_url_map_t		link_url_map;

			mds_element(id_t serverID, pid_t pid)
				:id(serverID), pid(pid)
			{
			}

			void copy_non_index_data_from(const mds_element&rhs)
			{
				type=rhs.type;
				last_report_time=rhs.last_report_time;
				link_url_map=rhs.link_url_map;
			}

			void insert(const link_t& link, const url_t& u)
			{
				link_url_map.insert(std::make_pair(link, u));
			}

			void erase(const link_t& link)
			{
				link_url_map.erase(link);
			}
		};
		struct mds_set
			:public multi_index::multi_index_container<mds_element, 
			multi_index::indexed_by<
			multi_index::ordered_unique<multi_index::member<mds_element, const mds_element::id_t, &mds_element::id> >, 
			multi_index::ordered_unique<multi_index::member<mds_element, const pid_t, &mds_element::pid> >
			>
			> 
		{
			typedef nth_index<0>::type server_index_type; 
			typedef nth_index<1>::type process_index_type;
			server_index_type& mds_id_index(){return multi_index::get<0>(*this);}
			const server_index_type& mds_id_index()const {return multi_index::get<0>(*this);}
			process_index_type& process_id_index(){return multi_index::get<1>(*this);}
			const process_index_type& process_id_index()const{return multi_index::get<1>(*this);}
		};

		struct channel_element{
			channel_element(int mds_id_, const server_param_base& param_)
				:link(param_.channel_link), mds_id(mds_id_), param(param_)
				, last_alive_time_(timestamp_now())
			{}
			timestamp_t			last_alive_time_;
			std::string			link;
			int					mds_id;
			server_param_base	param;
		};
		struct channel_set
			:public multi_index::multi_index_container<channel_element, 
			multi_index::indexed_by<
			multi_index::ordered_unique<multi_index::member<channel_element, std::string, &channel_element::link>  >
			>
		>
		{
			typedef nth_index<0>::type link_index_type; 
			link_index_type& link_index(){return multi_index::get<0>(*this);}
			const link_index_type& link_index()const {return multi_index::get<0>(*this);}
		};

		typedef struct  
		{
			std::string       session_id;
			channel_vec_type  chnls;
		} channel_vec_t;

		typedef struct 
		{
			std::map<int, channel_vec_t> live_chnls_;
			std::map<int, channel_vec_t> vod_chnls_;

			std::map<int, channel_vec_t>& get(int type)
			{
				return is_vod_category(type)?vod_chnls_:live_chnls_;
			}
		}waiting_st;

	public:
		static boost::shared_ptr<this_type> create(io_service& ios)
		{
			return boost::shared_ptr<this_type>(
				new this_type(ios), shared_access_destroy<this_type>()
				);
		}

	public:
		virtual void on_client_login(message_socket_sptr conn, const safe_buffer& buf);
		virtual void on_recvd_alarm_message(const safe_buffer& buf);
		virtual void on_client_dropped(const __id& ID);
		virtual void on_recvd_error_message(const std::string& msg);

	protected:
		virtual	void recover_from_db();
		virtual void __start();
		virtual void __set_operation_http_port(uint32_t port, std::string& errorMsg);
		virtual void __set_alive_alarm_port(uint32_t port, std::string& errorMsg); 

	protected:
		virtual void on_wild_sub_process_timer();
		virtual bool on_request_handler(const req_session& reqSess, std::string& errorMsg);
		
	protected:
		void add_or_update_mds_set(int serverID, mds_set& mds, int type, int pid=0);
		void add_channels_to_mds(int serverID, mds_set& mds, const channel_vec_type&);
		void create_mds_process(const std::string&, distribution_type, int, 
								const channel_vec_t&, int deadline=10);
		void close_mds_process(distribution_type type);
		void create_mds_or_set_channel (const node_map_t&, 
			distribution_type, std::string& errorMsg);

		void erase_channel(distribution_type type, const std::string& link, int mds_id);
		bool get_channels(distribution_type type, std::vector<server_param_base>&, std::string&);
		void insert_channel(const server_param_base&, mds_element& mds);
		
		void mds_alive_check(distribution_type);
		void modify_channel_owner(const server_param_base& channel, int mds_id, bool add);
		void post_cmd_to_mds(const std::string& sessionID, int serverID, 
			const channel_vec_type&, const cms_cmd_type&, bool isReply=false);
		void post_config_cmd_to_mds(const std::string& sessionID, int type, int serverID, 
			const std::string& configValue, const cms_cmd_type&);
		void process_node_map(const node_map_t&, distribution_type type, 
			const char* cmd_str);
		void parse_request_param(const config_map_type&, int&, std::string&);

		bool regist_code_config(const config_map_type& req, std::string& errorMsg);
		void remove_mds_idles(distribution_type);
		bool recover_servers(distribution_type);

		void start_check_timer();
		void stop_check_timer();
		void start_recover_timer(distribution_type type);
		inline void process_mapped_channel( const node_map_t& obj_channel_map, 
											distribution_type type, 
											const char* cmd_str )
		{
			process_node_map(obj_channel_map, type, cmd_str);
		}
	protected:
		void on_check_timer();
		bool on_add_request(const req_session& req, std::string& errorMsg);
		bool on_del_request(const req_session& req, std::string& errorMsg);
		bool on_start_request(const req_session&, std::string& errorMsg);
		bool on_stop_request(const req_session& req, std::string& errorMsg);
		bool on_reset_request(const req_session& req, std::string& errorMsg);
		bool on_tracker_request(const req_session& req, std::string& errorMsg);
		bool on_reg_code_updated(const req_session& req, std::string& errorMsg);
		bool on_status_request(const req_session& req, std::string& errorMsg);
	
	protected:
		bool do_stop_channel(const req_session& reqSess, 
			distribution_type, std::string& errorMsg);
		bool do_start_channel(const req_session& reqSess, 
			distribution_type, std::string& errorMsg);
		bool do_add_or_reset_channel(const req_session& reqSess, 
			distribution_type, std::string& errorMsg);

		bool __do_stop_channel(const req_session& reqSess, 
			distribution_type, const std::string&, std::string& errorMsg);
		bool __do_start_channel(const req_session& reqSess, 
			distribution_type, const std::string&, std::string& errorMsg);
		bool __do_add_or_reset_channel(const req_session&, 
			distribution_type, std::string& errorMsg);
		
		bool __do_stop_mds(const req_session& reqSess, 
			const mds_element&, std::string& errorMsg);
		bool __do_start_mds(const req_session& reqSess, 
			const mds_element&, std::string& errorMsg);
		bool __do_reset_mds(const req_session&, 
			const mds_element&, std::string& errorMsg);

	private:
		void __on_client_login(message_socket_sptr conn, const safe_buffer& buf);

    private:
		int  channel_belong_to(distribution_type type, const std::string&);
		mds_set& current_server_set(distribution_type);
		size_t current_channel_cnt(distribution_type);
	
		size_t max_process_cnt(distribution_type type, size_t channel_size);
		bool mds_exist(distribution_type type, int id);
		bool service_type_valide(int serviceType);

		void delay_add_tracker(int serverID, const server_param_base&, int deadline=10);
	
private:
		template<typename HandlerType>
		bool process_all(const std::string& sessionID, distribution_type type, 
			std::string& errorMsg, HandlerType& handler)
		{
			try{
				mds_set& runningMds=is_vod_category(type)?vod_mds_set_:live_mds_set_;
				BOOST_AUTO(itr, sessions_.find(sessionID));
				if(itr!=sessions_.end())
				{
					session& sess=const_cast<session&>(itr->second);
					sess.expected_reply_cnt=runningMds.size();
				}

				BOOST_FOREACH(const mds_element& elm, runningMds)
				{
					if(!handler(elm, errorMsg))
						return false;
				}
				return true;
			}
			catch(const std::exception& e)
			{
				errorMsg=e.what();
				return false;
			}
			catch(...){return false;}
		}

	protected:
		mds_control(io_service& ios);
		~mds_control();

	private:
		std::auto_ptr<mds_db>		db_;
		std::string					old_regist_code_;
		boost::shared_ptr<timer>	alive_timer_;
		boost::shared_ptr<timer>    recover_timer_;
		boost::shared_ptr<timer>    delay_timer_;
		
		mds_set						vod_mds_set_;
		mds_set						live_mds_set_;
		mds_set						recover_vod_set_;
		mds_set						recover_live_set_;
		channel_set					channel_set_;

		waiting_st							waiting_chnls_;
		boost::unordered_set<std::string>	suspended_channels_;
		boost::shared_ptr<http_acceptor>	http_acceptor_;
		timed_keeper_set<connection_sptr>	cmd_sockets_;
		std::list<alive::mds_Alive>			alarm_msg_set_;
		timestamp_t							last_write_time_;
	};
	typedef boost::shared_ptr<mds_control>	mds_control_sptr;

	//////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////
};

#endif // monitor_h__
