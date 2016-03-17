//
// heuristic_scheduling_strategy.h
// ~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2008 GuangZhu Wu (GuangZhuWu@gmail.com)
//
// All rights reserved. 
//
#ifndef heuristic_scheduling_strategy_h__
#define heuristic_scheduling_strategy_h__

#include "client/stream/scheduling_typedef.h"

namespace p2client{

	class heuristic_scheduling_strategy
		: public scheduling_strategy 
	{
		typedef heuristic_scheduling_strategy this_type;
		SHARED_ACCESS_DECLARE;

		struct piece
		{
			seqno_t seqno;                 //片段序号
			peer_connection* peer_in_charge; //将向这个节点请求 
			float select_priority;          //片段选择时候的优先级
			float shceduling_priority;      //片段调度时候的优先级
			bool  b_urgent:1;
			bool  b_not_necessary_to_use_server:1;
			bool  b_disk_cached:1;

			piece():peer_in_charge(NULL), b_urgent(false), b_disk_cached(false){}
		};

		struct piece_prior
		{
			//按照优先级排序用，优先级高的排前面，优先调度
			bool operator()(const piece&p1, const piece&p2)const
			{
				if (p1.peer_in_charge&&!p2.peer_in_charge)
					return true;
				else if(p2.peer_in_charge&&!p1.peer_in_charge)
					return false;
				if (p1.b_urgent&&!p2.b_urgent)
					return true;
				else if(p2.b_urgent&&!p1.b_urgent)
					return false;
				else if (p1.shceduling_priority!=p2.shceduling_priority)
					return(p1.shceduling_priority>p2.shceduling_priority);
				return seqno_less(p1.seqno, p2.seqno);
			}
		};

		struct task_peer_param
		{
			double remoteToLocalLostRate;
			double localToRemoteLostRate;
			double remoteToLocalLostRatePow2;
			double maxAppendTaskCnt;
			double aliveProbability;
			int residualTastCount;
			int taskCnt;
			bool inited;

			task_peer_param():inited(false), taskCnt(0){}
			void reset_taskcount(){taskCnt=0;}
		};

		struct substream_state_cache
		{
			peer_connection* conn;
			double dupRate;
			double averageDelay;
			double varDelay;
			double dupeThresh;
			double delayThresh;
			double pushRate;
			double elapseThresh;
			double lostRate;
			double aliveProbability;
			int64_t lastSetLoop;//上一次设置时候的loop号

			substream_state_cache():lastSetLoop(-100){};
		};

		struct piece_state_cache
		{
			seqno_t		seqno;
			timestamp_t timestamp;
			int			elapse;
			double		necessityScore;
			double		neighborScore;
			double		urgentDegree;
			enum {NOT_SURE=0, SURE_SELECT, SURE_NOT_SELECT} selectFlag;

			piece_state_cache():selectFlag(NOT_SURE){}
		};

		struct stable_select_param
		{
			double randSelectServerProb;
			double serverToLocalLostRate;
			double magnifyingPower;
			int	urgentTime;
		};
		struct mutable_select_param
		{
			seqno_t seqno;
			int subStreamID;
			piece_state_cache* pieceState;
			absent_packet_info* pktInfo;
			substream_state_cache* subStreamState;
		};

		typedef std::vector<piece_state_cache> piece_state_array;
		typedef std::vector<substream_state_cache> substream_state_array;
		//typedef boost::unordered_map<peer*, task_peer_param> task_peerparam_map;

	private:
		struct set_urgent_degree {
			set_urgent_degree():calced_(false){}
			void operator()(mutable_select_param&mprm, 
				boost::optional<seqno_t>&smallestNotUrgent, 
				const stream_monitor& streamMonitor, timestamp_t m_now, seqno_t seqno, 
				int urgentTime);
			bool calced_;
		};

	public:
		static shared_ptr create(stream_scheduling& scheduling)
		{
			return shared_ptr(new this_type(scheduling), 
				shared_access_destroy<this_type>());
		}

	protected:
		heuristic_scheduling_strategy(stream_scheduling& scheduling);
		~heuristic_scheduling_strategy(void);

	public:
		virtual void stop();
		virtual void start();
		virtual const scheduling_task_map& get_task_map();

		const bool is_pull_start() const
		{
			return pull_real_started_;
		}

	protected:
		int get_min_pull_size(bool onlyUrgent);		
		std::pair<seqno_t, seqno_t> bigest_select_seqno();
		int select_candidate_piece(bool onlyUrgent);
		void select_best_scheduling();

		void set_stable_select_param(stable_select_param&);
		void set_mutable_select_param(mutable_select_param&, seqno_t seqno);
		void set_substream_state(const stable_select_param&sprm, 
			mutable_select_param&mprm);

		double select_score(const stable_select_param&sprm, 
			mutable_select_param&mprm, int sellectLoopCnt);
		double scheduling_score(const peer& p, const task_peer_param& param, 
			double alf, bool isServer, piece& pic);

		void check_requested(const stable_select_param&s, seqno_t maxCheckSeqno, 
			int maxCheckCnt);

	protected:
		stream_scheduling*                      scheduling_;

		int                                     average_absent_media_packets_cnt_;    
		std::vector<piece>                      candidates_buf_;                         //启发式调度时，暂时调度方案，最优值将赋给best_scheduling_
		std::vector<piece>                      best_scheduling_;                        //最优调度方案
		scheduling_task_map						task_map_;                               //最终调度
		piece_state_array						select_piece_state_cache_;               //缓存select过程中的一些变量
		substream_state_array					select_substream_state_cache_;           //缓存select过程中的一些变量
		std::vector<int64_t>					select_substream_state_cache_init_flag_; //缓存select过程中的一些变量
		bool                                    pull_real_started_;
		int                                     min_request_cnt_;
		std::vector<task_peer_param>			temp_task_map_;

		rough_speed_meter						select_because_of_elapse_meter_;
		rough_speed_meter						select_because_of_urgent_meter_;
		rough_speed_meter						schedule_packet_speed_;

		timestamp_t								last_check_requested_time_;

		seqno_t									last_max_select_seqno_;

		int64_t									loop_;
	};

}

#endif //heuristic_scheduling_strategy_h__
