#include "simple_server/distributor_scheduling.h"
#include "simple_server/distributor_Impl.hpp"
#include "simple_server/multi_source_distributor.h"
#include "common/utility.h"
#include "simple_server/utility.h"
#include <algorithm>

#define EABLE_MEMORY_CACHED 1;

NAMESPACE_BEGIN(p2simple);

using namespace p2common;

void dummy_func(channel_session* channel_info, seqno_t max_of_last_cyc){}

#define ____TRY_LOCK(TimeMsec, ecPtr)\
	boost::timed_mutex::scoped_timed_lock lock(mutex_, boost::defer_lock_t());\
	____RE_LOCK(TimeMsec, ecPtr);

#define ____RE_LOCK(TimeMsec, ecPtr)\
	BOOST_ASSERT(!lock.owns_lock());\
	lock.timed_lock(boost::posix_time::millisec(TimeMsec));\
	if (!lock.owns_lock())\
{\
	if(ecPtr)\
	*reinterpret_cast<error_code*>(ecPtr)=boost::asio::error::timed_out;\
	return;\
}

boost::shared_ptr<simple_distributor_interface> distributor_scheduling::start(
	peer_info& srv_info, const path_type& dsk_file)
{
	if(!async_filecache_)
		async_filecache_ = async_filecache::create(get_io_service());

	//启动simple distributor
#ifndef EABLE_MEMORY_CACHED
	simple_distributor_ = asfile_distributor::create(
		get_io_service(), async_filecache_, dsk_file, this
		);
#else
	simple_distributor_=multi_source_distributor::create(
		get_io_service(), async_filecache_, dsk_file, this);
#endif
	simple_distributor_->start(srv_info);
	simple_distributor_->cal_channel_file_info(dsk_file);

	srv_info_ = srv_info;
	start_assist_distributor(dsk_file);

	return simple_distributor_;
}

void distributor_scheduling::stop()
{
	if(shift_timer_)
	{
		shift_timer_->cancel();
		shift_timer_.reset();
	}

	if(simple_distributor_)
	{
		simple_distributor_->stop();
		simple_distributor_.reset();
	}

	for (BOOST_AUTO(itr, channel_file_set_.id_index().begin());
		itr!=channel_file_set_.id_index().end();++itr)
	{
		simple_distributor_interface_sptr& assistDistributor = 
			const_cast<simple_distributor_interface_sptr&>((*itr).distributor_);

		if(!assistDistributor)
			continue;

		assistDistributor->stop();
		assistDistributor.reset();
	}
}

int distributor_scheduling::out_kbps()const
{
	return (int)(media_packet_rate_.bytes_per_second())*8/1024;//kb
}

void distributor_scheduling::start_shift_timer()
{
	if(shift_timer_)
		return;

	shift_timer_ = timer::create(get_io_service());
	shift_timer_->set_obj_desc("p2simple::distributor_scheduling::shift_timer_");
	shift_timer_->register_time_handler(boost::bind(&this_type::on_shift, this));
	shift_timer_->async_keep_waiting(millisec(0), millisec(channel_session::SHIFT_INTERVAL));
}

void distributor_scheduling::start_assist_distributor(const path_type& dsk_file)
{
#ifdef WINDOWS_OS
	paths_type channel_files;
	std::string fileName;

	if(!find_channel_files(dsk_file, fileName, channel_files))
		return;

	//设置为admin后就不读数据了，让assist处理
	simple_distributor_->upgrade(simple_distributor_interface::SERVER_ADMIN); 

	int64_t film_duration=0;

	static_channel_session(channel_files, fileName, film_duration);

	update_channel_info_to_admin(film_duration);

	create_assist_distributor_and_start();

	start_shift_timer();
#endif
}

void distributor_scheduling::on_shift()
{
	static boost::optional<seqno_t> s_interval_;
	if(!s_interval_)
	{
		s_interval_ = (seq_num_*1000) / simple_distributor_->get_duration();
	}

	____TRY_LOCK(100, NULL);

	DEBUG_SCOPE(
		if(in_probability(0.01))
			std::cout<<"-------------------------------------------------------\n";
	);
	int32_t candi_min=0;//这里不能依赖顺序，因为最大seqno可能出现在任意一个sesssion
	for (BOOST_AUTO(itr, channel_file_set_.id_index().rbegin()); 
		itr!=channel_file_set_.id_index().rend(); ++itr)
	{
		channel_session& current_file = const_cast<channel_session&>(*itr);

		current_file.shift(*s_interval_, 
			boost::bind(&dummy_func, &current_file, candi_min)
			);

		candi_min = std::max(candi_min, current_file.max_seqno_);
	}

	for (BOOST_AUTO(itr, channel_file_set_.id_index().rbegin()); 
		itr!=channel_file_set_.id_index().rend(); ++itr)
	{
		shift_file(&const_cast<channel_session&>(*itr), candi_min);
	}

	DEBUG_SCOPE(
		if(in_probability(0.01))
			std::cout<<"-------------------------------------------------------\n";
	);
}

//在所有的session执行shift完成后调用
void distributor_scheduling::shift_file(channel_session* channel_info, seqno_t max_of_last_cyc)
{
	//max_seq < 0, 整个文件已经更新
	seqno_t min_candi=max_of_last_cyc+1;

	if(channel_info->max_seqno_<0)
		channel_info->reschedule(min_candi, min_candi+seq_num_per_file_-1);

	DEBUG_SCOPE(
		std::cout<<"id: "<<channel_info->id_<<"[min, max]: ["
		<<channel_info->min_seqno_
		<<", "
		<<channel_info->max_seqno_
		<<"]"
		<<std::endl;
	);
	BOOST_ASSERT((channel_info->max_seqno_-channel_info->min_seqno_+1)
		==seq_num_per_file_);

	if(channel_info->min_seqno_<0&&
		channel_info->max_seqno_>0)
	{
		channel_info->assign_candidate(min_candi, min_candi+(seq_num_per_file_-(channel_info->max_seqno_+1))-1);

		BOOST_ASSERT(
			((channel_info->max_seqno_candidate_ - channel_info->min_seqno_candidate_+1)
			+channel_info->max_seqno_+1)
			==seq_num_per_file_
			);
		DEBUG_SCOPE(
			std::cout<<"id: "<<channel_info->id_<<"[candi_min, candi_max]: ["
			<<channel_info->min_seqno_candidate_
			<<", "
			<<channel_info->max_seqno_candidate_
			<<"]"
			<<std::endl;
		);
	}
}

bool distributor_scheduling::find_channel_files(
	const path_type& dskFile, std::string& file_name, paths_type& files)
{
	if(!find_assist_file(dskFile, files))
		return false;

	files.push_back(dskFile);

	return true;
}

void distributor_scheduling::static_channel_session(const paths_type& channels, 
													const std::string& fileName, 
													int64_t& DurationOfAllFile)
{
	if(channels.empty())
		return;

	DurationOfAllFile=0;


	struct channel_static
	{
		channel_static(channel_set& sets, int64_t& DurationOfAllFile)
			:sets_(sets), DurationOfAllFile_(DurationOfAllFile){}

		void operator()(const path_type& argv)const
		{
			std::string file_title = filename(argv); 
			tuple_type title_info = title_match(file_title);

			uint64_t Duration = get_duration(argv);
			DurationOfAllFile_ += Duration;

			sets_.id_index().insert(channel_session(
				argv, 
				boost::lexical_cast<int>(boost::tuples::get<0>(title_info)), 
				peer_id_t(md5(argv.string())), 
				get_length(argv), 
				Duration, 
				boost::lexical_cast<int>(boost::tuples::get<0>(title_info))
				));
		}
		channel_set& sets_;
		int64_t& DurationOfAllFile_;
	};
	//GCC下for_each有问题
	channel_static static_channel(channel_file_set_, DurationOfAllFile);
	BOOST_FOREACH(const path_type& pth, channels)
	{
		static_channel(pth);
	}
	//这些信息齐全后可以计算每个文件的seqno区间了
	seqno_t seqno=0;
	for (BOOST_AUTO(itr, channel_file_set_.id_index().begin()); 
		itr!=channel_file_set_.id_index().end(); ++itr)
	{
		channel_session& current_file = const_cast<channel_session&>(*itr);

		current_file.min_seqno_ = seqno;
		current_file.max_seqno_ = (int32_t)(seqno + current_file.length_/PIECE_SIZE-1);

		seqno = current_file.max_seqno_+1;
		seq_num_ = std::max(seq_num_, seqno);
	}

}

void distributor_scheduling::update_channel_info_to_admin(int64_t filmDuration)
{
	seq_num_per_file_ = seq_num_/channel_file_set_.id_index().size();
	simple_distributor_->set_len_duration(seq_num_*PIECE_SIZE, filmDuration);
}

void distributor_scheduling::create_assist_distributor_and_start()
{
	boost::shared_ptr<simple_distributor_interface> last_distributor = simple_distributor_;

	for (BOOST_AUTO(itr, channel_file_set_.id_index().begin()); 
		itr!=channel_file_set_.id_index().end(); ++itr)
	{
		const path_type& file_path_name = (*itr).name_;

		//start assistant distributor
		boost::shared_ptr<async_filecache> file_cache = async_filecache::create(get_io_service());

		boost::shared_ptr<simple_distributor_interface> assist_distributor = 
			p2simple::asfile_distributor::create(
			get_io_service(), file_cache, file_path_name, this
			);

		const_cast<channel_session&>(*itr).distributor_ = assist_distributor;
		last_distributor->set_delegate_handler(assist_distributor);

		assist_distributor->start_assistant(*srv_info_, const_cast<channel_session*>(&(*itr)));
		assist_distributor->cal_channel_file_info(file_path_name);

		last_distributor = assist_distributor;
	}

}

NAMESPACE_END(p2simple);
