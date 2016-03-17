#ifndef multi_source_distributor_h__
#define multi_source_distributor_h__

#include "p2engine/p2engine/basic_engine_object.hpp"
#include <p2engine/push_warning_option.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/member.hpp>
#include <p2engine/pop_warning_option.hpp>

#include "common/message.pb.h"
#include "simple_server/distributor_Impl.hpp"
#include "simple_server/peer_connection.h"

using namespace p2engine;
using namespace p2common;


namespace p2simple{
	namespace multi_index = boost::multi_index;
	typedef p2simple::distributor_Impl<asfio::async_filecache> base_distributor_type;

	class multi_source_distributor
		: public base_distributor_type
	{
		typedef multi_source_distributor		this_type;
		typedef p2simple::peer_connection		peer_connection;
		typedef p2simple::peer_connection_sptr	peer_connection_sptr;
		typedef p2simple::urdp_peer_connection	urdp_peer_connection;
		enum{TIMEOUT_INTERVAL=3000};
	public:
		static boost::shared_ptr<this_type> create(
			io_service& net_svc, const filecache_sptr& ins, 
			const  boost::filesystem::path& media_pathname = boost::filesystem::path(""), 
			distributor_scheduling* scheduling=NULL
			)
		{
			return boost::shared_ptr<this_type>(
				new this_type(net_svc, ins, media_pathname, scheduling), 
				shared_access_destroy<this_type>()
				);
		}

	public:
		multi_source_distributor(io_service& ios, const filecache_sptr& ins, 
			const boost::filesystem::path& media_pathname, distributor_scheduling* scheduling=NULL)
			:base_distributor_type(ios, ins, media_pathname, scheduling)
		{}
		~multi_source_distributor();
	public:
		virtual void start(peer_info& local_info);
		virtual void read_request_media(peer_connection_sptr conn, const media_request_msg& msg);
	
	protected:
		virtual void register_message_handler(peer_connection_sptr con);
		virtual void send_media_packet(peer_connection_sptr conn, const std::string& channelID, 
			seqno_t seqno, bool isCompletePkt, const safe_buffer& buf, const error_code& ec);
		
	protected:
		void on_recvd_media_packet(peer_connection* conn, safe_buffer buf);
		void on_recvd_missing_pieces_report(peer_connection* conn, const safe_buffer& buf);
		void on_connected(peer_connection* conn, const error_code& ec);
		void on_cache_svr_disconnected(peer_connection* conn, const error_code& ec);
		void on_request_time_out();

	protected:
		void connect();
		void send_cache_media_packet_to_peer(const std::string& peerID, seqno_t seqno, const safe_buffer& buf);
	protected:
		virtual void __on_disconnected(peer_connection* conn, const error_code& ec);

	private:
		typedef struct {
			typedef boost::unordered_map<seqno_t, timestamp_t> seqno_set_type;
			std::string				peerID;
			peer_connection_sptr	conn;
			seqno_set_type			seqnos_;
		}conn_elm;

		class conn_set
			: public multi_index::multi_index_container<conn_elm, 
			multi_index::indexed_by<
			multi_index::ordered_unique<multi_index::member<conn_elm, std::string, &conn_elm::peerID> >, 
			multi_index::ordered_unique<multi_index::member<conn_elm, peer_connection_sptr, &conn_elm::conn> >
			>
			>
		{
		public:
			typedef multi_index::nth_index<conn_set, 0>::type peer_id_index_type;
			typedef multi_index::nth_index<conn_set, 1>::type conn_index_type;
			peer_id_index_type& peer_id_index(){return multi_index::get<0>(*this);}
			conn_index_type& conn_index(){return multi_index::get<1>(*this);}
		};

		conn_set							conn_set_;
		boost::shared_ptr<timer>			connect_timer_;
		boost::shared_ptr<timer>			timeout_timer_;
		peer_connection_sptr				peer_socket_;
		std::list<std::string>				cache_server_ipport_;
	};

};

#endif // cache_service_h__
