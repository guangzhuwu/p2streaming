#include "p2engine/utilities.hpp"
#include "p2engine/safe_buffer_io.hpp"
#include "p2engine/logging.hpp"
#include "p2engine/broadcast_socket.hpp"
#include "p2engine/socket_utility.hpp"
#include "p2engine/rdp/const_define.hpp"
#include "p2engine/rdp/trdp_flow.hpp"
#include "p2engine/rdp/basic_shared_tcp_layer.hpp"

NAMESPACE_BEGIN(p2engine);
NAMESPACE_BEGIN(trdp);

basic_shared_tcp_layer::this_type_container basic_shared_tcp_layer::s_this_type_pool_;
spinlock basic_shared_tcp_layer::s_this_type_pool_mutex_;
rough_speed_meter basic_shared_tcp_layer::s_out_speed_meter_(millisec(3000));
rough_speed_meter basic_shared_tcp_layer::s_in_speed_meter_(millisec(3000));


bool basic_shared_tcp_layer::is_shared_endpoint_type(const endpoint& edp)
{
	spinlock::scoped_lock lock(s_this_type_pool_mutex_);
	return s_this_type_pool_.find(edp)!=s_this_type_pool_.end(); 
}

basic_shared_tcp_layer::shared_ptr 
	basic_shared_tcp_layer::create(io_service& ios, 
	const endpoint& local_edp, 
	error_code& ec, 
	bool realTimeUsage
	)
{
	bool anyport=(local_edp.port()==0);
	address addr=local_edp.address();
	bool anyaddr=is_any(addr);
	if ((anyport||anyaddr))
	{
		spinlock::scoped_lock lock(s_this_type_pool_mutex_);
		this_type_container::iterator itr=s_this_type_pool_.begin();
		for (;itr!=s_this_type_pool_.end();++itr)
		{
			BOOST_ASSERT(itr->second);
			shared_ptr net_obj=itr->second->shared_obj_from_this<this_type>();
			bool address_match=(addr==net_obj->local_endpoint_.address());
			bool port_match=(local_edp.port()==net_obj->local_endpoint_.port());
			bool match=(anyport&&address_match)||(anyaddr&&port_match);
			match=(match&&realTimeUsage==net_obj->is_real_time_usage());
			if (match)
			{
				return net_obj;
			}
		}
	}
	else
	{
		spinlock::scoped_lock lock(s_this_type_pool_mutex_);
		this_type_container::iterator iter = s_this_type_pool_.find(local_edp);
		if (iter != s_this_type_pool_.end())
		{
			BOOST_ASSERT(iter->second);
			if (iter->second->is_real_time_usage()==realTimeUsage)
				return iter->second->shared_obj_from_this<this_type>();
			else
			{
				ec=asio::error::address_in_use;
				return shared_ptr();
			}
		}
	}
	shared_ptr net_obj;
	endpoint edp(local_edp);
	for (int i=(anyport?16:1);i>0;--i)
	{
		ec.clear();
		if (anyport)
			edp.port(random<unsigned short>(1024, 10000));
		try
		{
			net_obj= shared_ptr(new this_type(ios, edp, ec, realTimeUsage));
		}
		catch (...)
		{
			LOG(LogError("catched exception when create basic_shared_tcp_layer"););
			continue;
		}
		if (!ec&&net_obj->is_open())
		{
			net_obj->start();
			break;
		}
		else
		{
			//std::cout<<ec.message()<<std::endl;
		}
	}
	BOOST_ASSERT(net_obj);
	return net_obj;
}

basic_shared_tcp_layer::basic_shared_tcp_layer(io_service& ios, const endpoint& local_edp, 
	error_code& ec, bool realTimeUsage)
	: basic_engine_object(ios) 
	, socket_(ios)
	, tcp_acceptor_(ios)
	, state_(INIT)
	, b_real_time_usage_(realTimeUsage)
{
	this->set_obj_desc("basic_shared_tcp_layer");

	tcp_acceptor_.open(boost::asio::ip::tcp::endpoint(local_edp).protocol(), ec);
	if (!ec)
	{
		error_code e;
		//set some option to fate rail time message trans
		if (realTimeUsage)
		{
			asio::socket_base::reuse_address reuse_address_option(false);
			asio::socket_base::receive_buffer_size receive_buffer_size_option(64*1024);
			asio::socket_base::send_buffer_size send_buffer_size_option(4*1024);//using a small buffer??
			asio::socket_base::send_low_watermark send_low_watermark_option(4);
			type_of_service type_of_service_option(0x20);

			tcp_acceptor_.set_option(reuse_address_option, e);
			tcp_acceptor_.set_option(receive_buffer_size_option, e);
			tcp_acceptor_.set_option(send_buffer_size_option, e);
			tcp_acceptor_.set_option(send_low_watermark_option, e);
			tcp_acceptor_.set_option(type_of_service_option, e);
		}
		else
		{
			asio::socket_base::reuse_address reuse_address_option(false);
			asio::socket_base::receive_buffer_size receive_buffer_size_option(1024*1024);
			asio::socket_base::send_buffer_size send_buffer_size_option(512*1024);
			//asio::socket_base::send_low_watermark send_low_watermark_option(4);
			type_of_service type_of_service_option(0x20);

			tcp_acceptor_.set_option(reuse_address_option, e);
			tcp_acceptor_.set_option(receive_buffer_size_option, e);
			tcp_acceptor_.set_option(send_buffer_size_option, e);
			//tcp_acceptor_.set_option(send_low_watermark_option, e);
			tcp_acceptor_.set_option(type_of_service_option, e);
		}
		tcp_acceptor_.set_option(asio::socket_base::linger(false, 0));
	}

	if (ec)
	{
		LOG(
			LogError("unable to open tcp acceptor, error:%d, %s", ec.value(), ec.message().c_str());
		);
		return;
	}
	tcp_acceptor_.bind(local_edp, ec);
	if (ec)
	{
		LOG(
			LogError("unable to bind tcp acceptor with endpoint %s, error %d %s", 
			endpoint_to_string(local_edp).c_str(), ec.value(), 
			ec.message().c_str());
		);
		error_code e;
		tcp_acceptor_.close(e);
		return;
	}
	else
	{
		local_endpoint_=tcp_acceptor_.local_endpoint(ec);
		if (ec)
		{
			LOG(
				LogError("unable get_local_endpoint on tcp acceptor, error %d %s", 
				ec.value(), ec.message().c_str()
				);
			);
			error_code e;
			tcp_acceptor_.close(e);
			local_endpoint_=endpoint();
			return;
		}
		else
		{
			spinlock::scoped_lock lock(s_this_type_pool_mutex_);
			s_this_type_pool_.insert(std::make_pair(local_endpoint_, this));
		}
	}
};

basic_shared_tcp_layer::~basic_shared_tcp_layer()
{
	BOOST_ASSERT(acceptors_.empty());
	{
		spinlock::scoped_lock lock(s_this_type_pool_mutex_);
		s_this_type_pool_.erase(local_endpoint_);
	}
	error_code ec;
	cancel_without_protector();
	tcp_acceptor_.close(ec);
}

basic_shared_tcp_layer::acceptor_type*
	basic_shared_tcp_layer::find_acceptor(const std::string& domainName)
{
	//check if there is any acceptor listening on the domain
	acceptor_container::iterator itr=acceptors_.find(domainName);
	if (itr==acceptors_.end())
		return NULL;
	else
		return itr->second;
}

void basic_shared_tcp_layer::start()
{
	if (state_!=INIT)
		return;
	state_=STARTED;
	error_code ec;
	tcp_acceptor_.listen(asio::socket_base::max_connections, ec);
	if (ec)
	{
		error_code e;
		LOG(
			LogError("unable to start basic_shared_tcp_layer acceptor with endpoint %s, error %d %s", 
			endpoint_to_string(local_endpoint(e)).c_str(), ec.value(), 
			ec.message().c_str());
		);
	}
	async_accept();
}

void basic_shared_tcp_layer::cancel_without_protector()
{
	error_code ec;
	tcp_acceptor_.cancel(ec);
	state_=STOPED;
}

void basic_shared_tcp_layer::handle_accept(const error_code& ec, flow_impl_sptr flow)
{
	if (state_!=STARTED)
		return;
	if (!ec)
	{
		//accept next
		async_accept();

		//process this socket
		flow_keeper_.try_keep(flow, seconds(5));
		flow->waiting_domain(SHARED_OBJ_FROM_THIS);
	}
	else
	{
		error_code e;
		LOG(
			LogError("unable async_accept for basic_shared_tcp_layer acceptor with endpoint %s, error %d %s", 
			endpoint_to_string(local_endpoint(e)).c_str(), ec.value(), 
			ec.message().c_str());
		);
		tcp_acceptor_.cancel(e);
		tcp_acceptor_.close(e);
	}
}

void basic_shared_tcp_layer::async_accept()
{
	if (state_!=STARTED)
		return;

	flow_impl_sptr flow=flow_impl_type::create_for_passive_connect(get_io_service(), 
		is_real_time_usage());
	tcp_acceptor_.async_accept(flow->lowest_layer(), 
		make_alloc_handler(
		boost::bind(&this_type::handle_accept, SHARED_OBJ_FROM_THIS, _1, flow)
		)
		);
}

void basic_shared_tcp_layer::register_flow(boost::shared_ptr<basic_flow_adaptor> flow, error_code& ec)
{
}
void basic_shared_tcp_layer::unregister_flow(uint32_t flow_id, const basic_flow_adaptor* flow)
{
}

void basic_shared_tcp_layer::register_acceptor(boost::shared_ptr<basic_acceptor_adaptor> acc, error_code& ec)
{
	ec.clear();
	std::pair<acceptor_container::iterator, bool> insertRst
		=acceptors_.insert(std::make_pair(acc->get_domain(), acc.get()));
	if (!insertRst.second)
		ec=asio::error::already_open; 
}

void basic_shared_tcp_layer::unregister_acceptor(const basic_acceptor_adaptor* acptor)
{
	acceptor_container::iterator itr=acceptors_.begin();
	for (;itr!=acceptors_.end();++itr)
	{
		if (static_cast<basic_acceptor_adaptor*>(itr->second) == acptor)
		{
			acceptors_.erase(itr);
			break;
		}
	}
}

NAMESPACE_END(trdp);
NAMESPACE_END(p2engine);

