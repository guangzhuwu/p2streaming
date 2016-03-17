#ifndef simple_server_simple_distributor_Impl_h__
#define simple_server_simple_distributor_Impl_h__

#include "simple_server/simple_distributor.h"

namespace p2simple{
	namespace multi_index = boost::multi_index;

	struct session
	{
		int32_t     min_seqno_;
		int32_t     max_seqno_;
		int32_t     min_seqno_candidate_;//文件被覆盖后新的seqno区间
		int32_t     max_seqno_candidate_;
		peer_connection_sptr socket_;

		session(peer_connection_sptr sock, const channel_session& channel_info)
			: socket_(sock)
			, min_seqno_(channel_info.min_seqno_)
			, max_seqno_(channel_info.max_seqno_)
			, min_seqno_candidate_(channel_info.min_seqno_candidate_)
			, max_seqno_candidate_(channel_info.max_seqno_candidate_)
		{}

		bool seqno_intersect(seqno_t seqno)
		{
			return (seqno_minus(seqno, min_seqno_>=0?min_seqno_:0)>=0 
				&& seqno_minus(max_seqno_, seqno)>=0);
		}

		bool seqno_candi_intersect(seqno_t seqno)
		{
			return (seqno_minus(seqno, min_seqno_candidate_>=0?min_seqno_candidate_:0)>=0 
				&& seqno_minus(max_seqno_candidate_, seqno)>=0);
		}

		bool in_charge(seqno_t seqno, int32_t& seqno_offset)
		{
			if(seqno_intersect(seqno))
			{
				seqno_offset = min_seqno_;
				return true;
			}

			if(seqno_candi_intersect(seqno))
			{
				seqno_offset = min_seqno_candidate_;
				return true;
			}

			return false;
		}

	};

	struct session_set 
		: public multi_index::multi_index_container<session, 
		multi_index::indexed_by<
		multi_index::ordered_unique<multi_index::member<session, peer_connection_sptr, &session::socket_> >, 
		multi_index::ordered_unique<multi_index::member<session, int32_t, &session::min_seqno_>, std::less<int32_t> >
		> 
		>
	{
		typedef multi_index::nth_index<session_set, 0>::type socket_index_type;
		socket_index_type& socket_index(){return multi_index::get<0>(*this);}
	};


	template<typename _CacheType>
	class distributor_Impl 
		: public simple_distributor
	{
		typedef  distributor_Impl<_CacheType>     this_type;
		SHARED_ACCESS_DECLARE;

		struct lru{
			lru(this_type* distributor)
			{
				m_distributor=distributor;
				m_file=distributor->media_manager_;
				m_last_use_time=distributor->last_use_time_;
			}
			this_type* m_distributor;
			boost::shared_ptr<_CacheType> m_file;
			ptime m_last_use_time;
			bool operator<(const lru& rhs)const
			{
				return m_last_use_time<rhs.m_last_use_time||m_file<rhs.m_file;
			}
		};
		friend struct lru;
		enum{FILE_POOL_SIZE=120};
	public:
		typedef  boost::shared_ptr<_CacheType> filecache_sptr;
		static boost::shared_ptr<this_type> create(
			io_service& net_svc, const filecache_sptr& ins, 
			const  boost::filesystem::path& media_pathname = boost::filesystem::path(""), 
			distributor_scheduling* scheduling=NULL
			)
		{
			boost::shared_ptr<this_type>rst(
				new this_type(net_svc, ins, media_pathname, scheduling), 
				shared_access_destroy<this_type>()
				);
			return rst;
		}

	public:
		void read_request_media_after_open(peer_connection_sptr conn, const media_request_msg& msg, 
			error_code ec)
		{
			simple_distributor::read_request_media(conn, msg);

			if(this->is_admin()||!conn->is_connected()||!media_manager_)
			{
				return;
			}
			session* dis_session = NULL;
			if(!media_manager_->is_complete_packet()) //不是读取缓存
				dis_session = this->get_session(conn); 

			timestamp_t now=timestamp_now();
			double LocalToRemoteLostRate=conn->local_to_remote_lost_rate();
			const std::string & channelID = msg.channel_id();

			for (int i=0; i<msg.seqno_size(); ++i)
			{
				seqno_t seqno= msg.seqno(i);

				if(dis_session && !dis_session->in_charge(seqno, seqno_offset_))
					continue; //不在session负责的seqno区间

				//丢包严重，以一定概率发送数据
				if (LocalToRemoteLostRate>0.50
					&&in_probability(LocalToRemoteLostRate)
					)
				{
					conn->media_have_sent(seqno, now);
					continue;
				}				

				media_manager_->read_piece(channelID, seqno-seqno_offset_, 
					boost::bind(&this_type::send_media_packet, SHARED_OBJ_FROM_THIS, 
					conn, channelID, seqno, media_manager_->is_complete_packet(), 
					_1, _2)
					);
			}
		}

		virtual void read_request_media(peer_connection_sptr conn, const media_request_msg& msg)
		{
			if(this->is_admin()||!conn->is_connected())
				return;

			if (!media_manager_)
			{
				media_manager_=_CacheType::create(get_io_service());
			}
			if(!media_manager_->is_open())
			{	
				const boost::filesystem::path& cache_file = media_manager_->path();

				if(this->media_path_name().empty()
					&& boost::filesystem::exists(cache_file))
				{
					this->set_media_pathname(cache_file);
				}
				else if(this->media_path_name().empty())
				{
					return;
				}

				BOOST_ASSERT(!this->media_path_name().empty());
				media_manager_->open(
					this->media_path_name(), this->get_length(), PIECE_SIZE, PIECE_SIZE, 0, 
					boost::bind(&this_type::read_request_media_after_open, SHARED_OBJ_FROM_THIS, conn, msg, _1)
					);
				return;
			}
			read_request_media_after_open(conn, msg, error_code());
		}

	protected:
		void __on_disconnected(peer_connection* conn, const error_code& ec)
		{
			sessions_.socket_index().erase(conn->shared_obj_from_this<peer_connection>());
		}

	protected:
		session* get_session(peer_connection_sptr conn)
		{
			if(!this->is_assist())
				return NULL;

			channel_session* channel_info = NULL;

			BOOST_AUTO(itr, sessions_.socket_index().find(conn));
			if(sessions_.socket_index().end() == itr)
			{
				channel_info = this->get_channel_file();
				if(!channel_info)
					return NULL;

				BOOST_AUTO(session_itr, sessions_.socket_index().insert(
					session(conn, *channel_info)));

				return const_cast<session*>(&(*(session_itr.first)));
			}
			else
			{
				return const_cast<session*>(&(*itr));
			}
		}

	protected:
		distributor_Impl(
			io_service& ios, const filecache_sptr& ins, 
			const boost::filesystem::path& media_pathname, 
			distributor_scheduling* scheduling=NULL
			)
			: simple_distributor(ios, scheduling)
			, seqno_offset_(0)
			, last_use_time_(ptime_now())
		{
			if (!ins)
			{
				this->media_manager_=_CacheType::create(ios);
			}
			else
			{
				this->media_manager_ = ins;
			}
			if (!media_pathname.empty())
			{
				this->set_media_pathname(media_pathname);
			}
			else if (ins&&ins->is_open())
			{
				this->set_media_pathname(ins->path());
			}
		}

		virtual ~distributor_Impl(){}

	private:
		boost::shared_ptr<_CacheType>  media_manager_;
		ptime last_use_time_;
		session_set sessions_;
		int32_t     seqno_offset_;
	};

	typedef distributor_Impl<asfio::async_filecache> asfile_distributor;
	typedef distributor_Impl<asfio::async_dskcache>  dsk_cache_distributor;

};

#endif // simple_server_simple_distributor_Impl_h__
