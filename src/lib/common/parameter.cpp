#include "common/parameter.h"
#include "common/const_define.h"
#include "client/local_param.h"

NAMESPACE_BEGIN(p2common);

client_param_sptr create_client_param_sptr(const client_param_base& param)
{
	client_param_sptr paramSptr(new client_param);
	*(client_param_base*)(paramSptr.get())=param;

	paramSptr->local_info=p2client::static_local_peer_info();

	switch(paramSptr->type)
	{
	case INTERACTIVE_LIVE_TYPE:
		paramSptr->delay_guarantee=g_delay_guarantee?*g_delay_guarantee:DEFAULT_DELAY_GUARANTEE__INTERACTIVE;
		paramSptr->back_fetch_duration=g_back_fetch_duration?*g_back_fetch_duration:DEFAULT_BACK_FETCH_DURATION__INTERACTIVE;
		paramSptr->max_push_delay=MAX_PUSH_DELAY__INTERACTIVE;
		paramSptr->tracker_peer_ping_interval=TRACKER_PEER_PING_INTERVAL__INTERACTIVE;
		paramSptr->server_seed_ping_interval=SERVER_SEED_PING_INTERVAL__INTERACTIVE;
		paramSptr->member_table_exchange_interval=MEMBER_TABLE_EXCHANGE_INTERVAL__INTERACTIVE;
		paramSptr->stream_neighbor_peer_cnt=STREAM_NEIGHTBOR_PEER_CNT__INTERACTIVE;
		paramSptr->hub_neighbor_peer_cnt=HUB_NEIGHTBOR_PEER_CNT__INTERACTIVE;

		paramSptr->b_fast_push_to_player=g_b_fast_push_to_player?*g_b_fast_push_to_player:true;
		paramSptr->b_streaming_using_rtcp=g_b_streaming_using_rtcp?*g_b_streaming_using_rtcp:true;
		paramSptr->b_streaming_using_rudp=g_b_streaming_using_rudp?*g_b_streaming_using_rudp:true;
		paramSptr->b_tracker_using_rtcp=g_b_tracker_using_rtcp?*g_b_tracker_using_rtcp:true;
		paramSptr->b_tracker_using_rudp=g_b_tracker_using_rudp?*g_b_tracker_using_rudp:true;
		break;

	case LIVE_TYPE:
		paramSptr->delay_guarantee=g_delay_guarantee?*g_delay_guarantee:DEFAULT_DELAY_GUARANTEE;
		paramSptr->back_fetch_duration=g_back_fetch_duration?*g_back_fetch_duration:DEFAULT_BACK_FETCH_DURATION;
		paramSptr->max_push_delay=MAX_PUSH_DELAY;
		paramSptr->tracker_peer_ping_interval=TRACKER_PEER_PING_INTERVAL;
		paramSptr->server_seed_ping_interval=SERVER_SEED_PING_INTERVAL;
		paramSptr->member_table_exchange_interval=MEMBER_TABLE_EXCHANGE_INTERVAL;
		paramSptr->stream_neighbor_peer_cnt=STREAM_NEIGHTBOR_PEER_CNT;
		paramSptr->hub_neighbor_peer_cnt=HUB_NEIGHTBOR_PEER_CNT;

		paramSptr->b_fast_push_to_player=g_b_fast_push_to_player?*g_b_fast_push_to_player:true;
		paramSptr->b_streaming_using_rtcp=g_b_streaming_using_rtcp?*g_b_streaming_using_rtcp:true;
		paramSptr->b_streaming_using_rudp=g_b_streaming_using_rudp?*g_b_streaming_using_rudp:true;
		paramSptr->b_tracker_using_rtcp=g_b_tracker_using_rtcp?*g_b_tracker_using_rtcp:true;
		paramSptr->b_tracker_using_rudp=g_b_tracker_using_rudp?*g_b_tracker_using_rudp:true;
		break;

	case VOD_TYPE:
	case GLOBAL_CACHE_TYPE:
		paramSptr->delay_guarantee=g_delay_guarantee?*g_delay_guarantee:DEFAULT_DELAY_GUARANTEE__VOD;
		paramSptr->back_fetch_duration=0;//vod不作后向下载
		paramSptr->tracker_peer_ping_interval=TRACKER_PEER_PING_INTERVAL;
		paramSptr->server_seed_ping_interval=SERVER_SEED_PING_INTERVAL;
		paramSptr->member_table_exchange_interval=MEMBER_TABLE_EXCHANGE_INTERVAL;
		paramSptr->stream_neighbor_peer_cnt=STREAM_NEIGHTBOR_PEER_CNT;
		paramSptr->hub_neighbor_peer_cnt=HUB_NEIGHTBOR_PEER_CNT;

		paramSptr->b_fast_push_to_player=g_b_fast_push_to_player?*g_b_fast_push_to_player:true;
		paramSptr->b_streaming_using_rtcp=g_b_streaming_using_rtcp?*g_b_streaming_using_rtcp:true;
		paramSptr->b_streaming_using_rudp=g_b_streaming_using_rudp?*g_b_streaming_using_rudp:true;
		paramSptr->b_tracker_using_rtcp=g_b_tracker_using_rtcp?*g_b_tracker_using_rtcp:true;
		paramSptr->b_tracker_using_rudp=g_b_tracker_using_rudp?*g_b_tracker_using_rudp:true;
		break;

	case BT_TYPE:
		paramSptr->delay_guarantee=g_delay_guarantee?*g_delay_guarantee:DEFAULT_DELAY_GUARANTEE;
		paramSptr->back_fetch_duration=0;//bt不作后向下载
		paramSptr->tracker_peer_ping_interval=TRACKER_PEER_PING_INTERVAL;
		paramSptr->server_seed_ping_interval=SERVER_SEED_PING_INTERVAL;
		paramSptr->member_table_exchange_interval=MEMBER_TABLE_EXCHANGE_INTERVAL;
		paramSptr->stream_neighbor_peer_cnt=STREAM_NEIGHTBOR_PEER_CNT;
		paramSptr->hub_neighbor_peer_cnt=HUB_NEIGHTBOR_PEER_CNT;

		paramSptr->b_fast_push_to_player=g_b_fast_push_to_player?*g_b_fast_push_to_player:true;
		paramSptr->b_streaming_using_rtcp=g_b_streaming_using_rtcp?*g_b_streaming_using_rtcp:true;
		paramSptr->b_streaming_using_rudp=g_b_streaming_using_rudp?*g_b_streaming_using_rudp:true;
		paramSptr->b_tracker_using_rtcp=g_b_tracker_using_rtcp?*g_b_tracker_using_rtcp:true;
		paramSptr->b_tracker_using_rudp=g_b_tracker_using_rudp?*g_b_tracker_using_rudp:true;
		break;

	default:
		BOOST_ASSERT(0);
	}
	return paramSptr;
}

server_param_sptr create_server_param_sptr(const server_param_base& param)
{
	server_param_sptr paramSptr(new server_param);
	*(server_param_base*)(paramSptr.get())=param;

	switch(paramSptr->type)
	{
	case INTERACTIVE_LIVE_TYPE:
		paramSptr->seed_peer_cnt=SEED_PEER_CNT__INTERACTIVE;
		paramSptr->min_super_seed_peer_cnt=MIN_SUPER_SEED_PEER_CNT__INTERACTIVE;
		paramSptr->server_seed_ping_interval=SERVER_SEED_PING_INTERVAL__INTERACTIVE;
		break;

	case LIVE_TYPE:
	case VOD_TYPE:
	case BT_TYPE:
	case GLOBAL_CACHE_TYPE:
		paramSptr->seed_peer_cnt=SEED_PEER_CNT;
		paramSptr->min_super_seed_peer_cnt=MIN_SUPER_SEED_PEER_CNT;
		paramSptr->server_seed_ping_interval=SERVER_SEED_PING_INTERVAL;
		break;

	default:
		BOOST_ASSERT(0);
	}

	return paramSptr;
}

tracker_param_sptr create_tracker_param_sptr(const tracker_param_base& param)
{
	tracker_param_sptr paramSptr(new tracker_param);
	*(tracker_param_base*)(paramSptr.get())=param;

	switch(paramSptr->type)
	{
	case INTERACTIVE_LIVE_TYPE:
		paramSptr->tracker_peer_ping_interval=TRACKER_PEER_PING_INTERVAL__INTERACTIVE;
		paramSptr->b_tracker_using_rtcp=g_b_tracker_using_rtcp?*g_b_tracker_using_rtcp:true;
		paramSptr->b_tracker_using_rudp=g_b_tracker_using_rudp?*g_b_tracker_using_rudp:true;
		break;

	case LIVE_TYPE:
	case VOD_TYPE:
	case BT_TYPE:
	case GLOBAL_CACHE_TYPE:
		paramSptr->tracker_peer_ping_interval=TRACKER_PEER_PING_INTERVAL;
		paramSptr->b_tracker_using_rtcp=g_b_tracker_using_rtcp?*g_b_tracker_using_rtcp:true;
		paramSptr->b_tracker_using_rudp=g_b_tracker_using_rudp?*g_b_tracker_using_rudp:true;
		break;

	default:
		BOOST_ASSERT(0);
	}

	return paramSptr;
}

NAMESPACE_END(p2common);
