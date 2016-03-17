#include "client/cache/cache_service.h"
#include "client/tracker_manager.h"

#include <p2engine/push_warning_option.hpp>
#include <boost/filesystem.hpp>
#include <p2engine/pop_warning_option.hpp>

#if !defined(_DEBUG_SCOPE) && defined(NDEBUG)
#	define  CACHE_SERVICE_DBG(x) 
#else 
#	define  CACHE_SERVICE_DBG(x) x
#endif

NAMESPACE_BEGIN(p2simple);

void simple_client_distributor::on_recvd_buffermap_request(peer_connection*conn, 
	safe_buffer buf)
{
	p2p_buffermap_request_msg msg;
	if (!parser(buf, msg))
		return;

	int minSeq=msg.min_seqno();
	int maxSeq=msg.max_seqno();
	std::string pieceMap;

	BOOST_AUTO(cache, p2client::get_cache_manager(get_io_service()));
	cache->get_piece_map(conn->channel_id(), minSeq, maxSeq, pieceMap);
	if (pieceMap.empty())
		return;

	buffermap_exchange_msg exmsg;
	buffermap_info* mutableBufferMap=exmsg.mutable_buffermap();
	if(cache->has_piece(conn->channel_id(), minSeq))
	{
		if(-1==smallest_seq_cached_)
			smallest_seq_cached_ = minSeq;

		smallest_seq_cached_ = std::min(smallest_seq_cached_, minSeq);
		mutableBufferMap->set_smallest_seqno_i_have(smallest_seq_cached_);
	}

	mutableBufferMap->set_bigest_seqno_i_know(maxSeq);
	mutableBufferMap->set_first_seqno_in_bitset(minSeq);
	mutableBufferMap->set_bitset(pieceMap);

	seqno_t erased_begin=-1;
	seqno_t erased_end=-1;
	cache->pop_piece_erased(conn->channel_id(), erased_begin, erased_end, true);
	if((erased_end-erased_begin))
	{
		mutableBufferMap->set_erased_seq_begin(erased_begin);
		mutableBufferMap->set_erased_seq_end(erased_end);
		DEBUG_SCOPE(
			std::cout<<"XXXXXXXXXXXXXX--distributor buffermap request reply tell NOT same channel neighbor: ["<<erased_begin<<", "<<erased_end<<"]"<<std::endl;
		);
	}

	conn->async_send_unreliable(serialize(exmsg), peer_peer_msg::buffermap_exchange);
}
NAMESPACE_END(p2simple);


//////////////////////////////////////////////////////////////////////////
NAMESPACE_BEGIN(p2client);

//#define  DEBUG_CRASH std::cout<<__FILE__<<": "<<__LINE__<<std::endl;

cache_service::cache_service(io_service&ios, client_param_sptr localParam)
	: basic_engine_object(ios)
	, basic_client_object(localParam)
	, running_(false)
{
	tracker_handler_=tracker_manager::create(ios, localParam, tracker_manager::cache_type);
	async_dskcache_=async_dskcache::create(ios);
	distributor_ = p2simple::simple_client_distributor::create(ios, async_dskcache_);

	//report disk catch to tracker::cache_service

	//distributor_ creat
	//distributor_ start
}
cache_service::~cache_service()
{
	if(update_cache_to_tracker_timer_)
	{
		update_cache_to_tracker_timer_->cancel();
		update_cache_to_tracker_timer_.reset();
	}
}

void cache_service::start(int64_t cache_file_size)
{
	if(running_)
		return;
	running_=true;

	BOOST_ASSERT(get_client_param_sptr()==tracker_handler_->get_client_param_sptr());

	tracker_handler_->ON_LOGIN_FINISHED=boost::bind(
		&this_type::on_tracker_login_finished, this, (int)cache_file_size
		);
	tracker_handler_->start(cache_tracker_demain);
}

void cache_service::on_tracker_login_finished(int cache_file_size)
{
	distributor_->start(get_client_param_sptr()->local_info);

	BOOST_ASSERT(PIECE_SIZE*PIECE_COUNT_PER_CHUNK==PIECE_SIZE);
	int64_t pieceSize=PIECE_SIZE+100;
	int64_t chunkSize=PIECE_COUNT_PER_CHUNK*pieceSize;
	BOOST_ASSERT(pieceSize*PIECE_COUNT_PER_CHUNK==chunkSize);
	int64_t fileSize=cache_file_size;
	int64_t memCacheSize=PIECE_SIZE;

	chunkSize=(chunkSize/pieceSize)*pieceSize;
	fileSize=(fileSize/chunkSize)*chunkSize;

	std::string catch_file =uri::normalize(
		get_client_param_sptr()->cache_directory+"p2s_iptv.cache");

	typedef boost::function<void(const error_code&)> open_handler_type;
	async_dskcache_->open<open_handler_type>(catch_file, fileSize, chunkSize, pieceSize, 
		memCacheSize, boost::bind(&this_type::on_dskcache_opened, SHARED_OBJ_FROM_THIS, _1) 
		);

	running_ = true;
}

void cache_service::start_update_cache_timer(const std::string& channelID)
{
	//�в��ŵ�Ƶ�������ˣ�����ſ�ʼ����cache�ڵ�
	//get_cache_tracker_handler(get_io_service())->register_playing_channel_to_cache(channelID);
	if (!update_cache_to_tracker_timer_)
	{
		update_cache_to_tracker_timer_ = timer::create(get_io_service());
		update_cache_to_tracker_timer_->set_obj_desc("p2client::cache_service::update_cache_to_tracker_timer_");

		update_cache_to_tracker_timer_->register_time_handler(boost::bind(
			&this_type::update_changed_cache, this, channelID
			));
		update_cache_to_tracker_timer_->cancel();
		update_cache_to_tracker_timer_->async_keep_waiting(seconds(0), seconds(60));
	}
}

void cache_service::stop_update_cache_timer()
{
	if (update_cache_to_tracker_timer_)
	{
		update_cache_to_tracker_timer_->unregister_time_handler();
		update_cache_to_tracker_timer_->cancel();
		update_cache_to_tracker_timer_.reset();
	}
}

void cache_service::update_changed_cache(const std::string& channelID)
{

	//BOOST_ASSERT(async_dskcache_->is_open());
	//��sdcard��openһ��1G���ҵ�cache�ļ���ʱ�ܺ�ʱ�����ܴ�ʱstate����openning
	if(!async_dskcache_->is_open())
		return;

	std::vector<async_dskcache::channel_info_type> channels_info, del_channels_info;
	async_dskcache_->get_all_channels_info(channels_info, del_channels_info);

	std::vector<std::pair<std::string, int> > cache_infoVec;
	cache_infoVec.reserve(1);

	for (BOOST_AUTO(itr, channels_info.begin()); itr != channels_info.end(); ++itr)
	{
		if((*itr).channel_id==channelID)
		{
			cache_infoVec.push_back(
				std::make_pair((*itr).channel_id, (*itr).health)
				);
			break;
		}
		else if(channelID.empty())//channelIDΪ�ձ�ʾ��������Ƶ��״̬
		{
			cache_infoVec.push_back(
				std::make_pair((*itr).channel_id, (*itr).health)
				);
			CACHE_SERVICE_DBG(
				std::cout<<"report channel:"<<string_to_hex((*itr).channel_id)<<"\n";
				);
		}
	}
	for (BOOST_AUTO(itr, del_channels_info.begin()); 
		itr != del_channels_info.end(); ++itr)
	{
		cache_infoVec.push_back(
			std::make_pair((*itr).channel_id, (*itr).health)
			);
	}
	tracker_handler_->cache_changed(cache_infoVec);
}

void cache_service::on_dskcache_opened(const error_code&ec)
{
	if (ec)
	{
		BOOST_ASSERT(!async_dskcache_->is_open());
		return;
	}
	update_cached_channel();
}

void cache_service::update_cached_channel()
{
	update_changed_cache("");
}

boost::shared_ptr<cache_service>& get_cache_service(io_service& ios)
{
	static boost::shared_ptr<cache_service> s_instence;

	BOOST_ASSERT(!s_instence||&ios==&s_instence->get_io_service());
	(void)(ios);

	return s_instence;
}

//////////////////////////////////////////////////////////////////////////
boost::shared_ptr<p2client::tracker_manager> get_cache_tracker_handler(io_service& ios)
{
	if (get_cache_service(ios))
		return get_cache_service(ios)->get_tracker_handler();
	return boost::shared_ptr<p2client::tracker_manager>();
}

boost::shared_ptr<asfio::async_dskcache> get_cache_manager(io_service& ios)
{
	if (get_cache_service(ios))
		return get_cache_service(ios)->get_catch_manager();
	return boost::shared_ptr<asfio::async_dskcache>();
}

boost::shared_ptr<cache_service> init_cache_service(io_service& ios, 
	client_param_sptr localParam, size_t cache_file_size)
{
	boost::shared_ptr<cache_service>& sInstance=get_cache_service(ios);
	if(!sInstance)
	{
		sInstance.reset(new cache_service(ios, localParam));
		sInstance->start(cache_file_size);
	}
	else
		sInstance->update_cached_channel();

	return sInstance;
}

NAMESPACE_END(p2client)

