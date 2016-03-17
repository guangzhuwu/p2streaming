//
// acceptor.hpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2009, GuangZhu Wu  <guangzhuwu@gmail.com>
//
//This program is free software; you can redistribute it and/or modify it 
//under the terms of the GNU General Public License or any later version.
//
//This program is distributed in the hope that it will be useful, but 
//WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY 
//or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License 
//for more details.
//
//You should have received a copy of the GNU General Public License along 
//with this program; if not, contact <guangzhuwu@gmail.com>.
//

#ifndef p2engine_acceptor_hpp__
#define p2engine_acceptor_hpp__

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "p2engine/basic_dispatcher.hpp"
#include "p2engine/shared_access.hpp"
#include "p2engine/connection.hpp"

namespace p2engine {

	namespace urdp{
		template<typename BaseConnectionType>
		class basic_urdp_connection;
		template<typename ConnectionType, typename ConnectionBaseType>
		class basic_urdp_acceptor;
	}
	namespace trdp{
		template<typename BaseConnectionType>
		class basic_trdp_connection;
		template<typename ConnectionType, typename ConnectionBaseType>
		class basic_trdp_acceptor;
	}

	template<typename ConnectionBaseType>
	class basic_acceptor
		: public basic_engine_object
		, public basic_acceptor_dispatcher<ConnectionBaseType>
	{
		typedef basic_acceptor<ConnectionBaseType> this_type;
		SHARED_ACCESS_DECLARE;

	public:
		typedef variant_endpoint endpoint;
		typedef ConnectionBaseType connection_base_type;
		typedef urdp::basic_urdp_connection<connection_base_type> urdp_connection_type;
		typedef trdp::basic_trdp_connection<connection_base_type> trdp_connection_type;
		typedef urdp::basic_urdp_acceptor<urdp_connection_type, connection_base_type> urdp_acceptor_type;
		typedef trdp::basic_trdp_acceptor<trdp_connection_type, connection_base_type> trdp_acceptor_type;

	protected:
		basic_acceptor(io_service&ios, bool realTimeUsage)
			:basic_engine_object(ios)
			, domain_(INVALID_DOMAIN)
			, b_real_time_usage_(realTimeUsage)
		{}
		virtual ~basic_acceptor(){};

	public:
		enum acceptor_t{
			TCP, //tcp message connection acceptor
			UDP, //udp message connection acceptor(urdp)
			MIX//mix tcp and udp
		};

	public:
		//listen on a local_edp to accept connections of domainName
		//listen(localEdp, "bittorrent/p2p", ec)
		virtual error_code listen(const endpoint& local_edp, 
			const std::string& domainName, 
			error_code& ec) = 0;

		virtual error_code listen(const endpoint& local_edp, 
			error_code& ec) = 0;

		virtual void keep_async_accepting() = 0;
		virtual void block_async_accepting() = 0;

		error_code close(){ error_code ec; return close(ec); }
		virtual error_code close(error_code& ec) = 0;

		virtual endpoint local_endpoint(error_code& ec) const = 0;

	public:
		bool is_real_time_usage()const
		{
			return b_real_time_usage_;
		}
		const std::string& domain()const{ return domain_; };

	protected:
		std::string domain_;
		bool b_real_time_usage_;
	};

	template<typename DerivedType, typename BasicAcceptor>
	class basic_mix_acceptor
	{
		template<typename Base, typename Derived>
		struct get_derived_this
		{
			BOOST_STATIC_ASSERT((boost::is_base_and_derived<Base, Derived>::value));
			Derived* operator()(Base* ptr)const
			{
				Derived*p = (Derived*)(ptr);//!!DO NOT USING reinterpret_cast!!
				BOOST_ASSERT((uintptr_t)dynamic_cast<Derived*>(ptr) == (uintptr_t)(p));
				return p;
			}
		};
		typedef get_derived_this<basic_mix_acceptor, DerivedType> get_derived_type;
		typedef typename BasicAcceptor::urdp_acceptor_type urdp_acceptor_type;
		typedef typename BasicAcceptor::trdp_acceptor_type trdp_acceptor_type;

	protected:
		virtual ~basic_mix_acceptor(){}

	protected:
		void start_acceptor(const variant_endpoint& edp, const std::string& domain, 
			error_code& ec, bool tryZeroPort = false)
		{
			start_acceptor(edp, edp, domain, ec, tryZeroPort);
		}
		void start_acceptor(const variant_endpoint& trdpEdp, const variant_endpoint& urdpEdp, 
			const std::string& domain, error_code& ec, bool tryZeroPort = false)
		{
			trdp_acceptor_ = trdp_acceptor_type::create(
				get_derived_type()(this)->get_io_service(), 
				true);
			trdp_acceptor_->register_accepted_handler(
				boost::bind(&DerivedType::on_accepted, get_derived_type()(this), _1, _2)
				);
			trdp_acceptor_->listen(trdpEdp, domain, ec);
			if (ec)
			{
				if (tryZeroPort)
				{
					ec.clear();
					variant_endpoint edp0 = trdpEdp;
					edp0.port(0);
					trdp_acceptor_->listen(edp0, domain, ec);
				}
			}
			if (ec)
			{
			}
			else
			{
				trdp_acceptor_->keep_async_accepting();
			}

			urdp_acceptor_ = urdp_acceptor_type::create(
				get_derived_type()(this)->get_io_service(), 
				true);
			urdp_acceptor_->register_accepted_handler(
				boost::bind(&DerivedType::on_accepted, get_derived_type()(this), _1, _2)
				);
			urdp_acceptor_->listen(urdpEdp, domain, ec);
			if (ec)
			{
				if (tryZeroPort)
				{
					ec.clear();
					variant_endpoint edp0 = urdpEdp;
					edp0.port(0);
					urdp_acceptor_->listen(edp0, domain, ec);
				}
			}
			if (ec)
			{
			}
			else
			{
				urdp_acceptor_->keep_async_accepting();
			}
		}
		void close_acceptor()
		{
			if (trdp_acceptor_)
			{
				error_code ec;
				trdp_acceptor_->close(ec);
				trdp_acceptor_.reset();
			}
			if (urdp_acceptor_)
			{
				error_code ec;
				urdp_acceptor_->close(ec);
				urdp_acceptor_.reset();
			}
		}
	protected:
		typename boost::shared_ptr<trdp_acceptor_type> trdp_acceptor_;
		typename boost::shared_ptr<urdp_acceptor_type> urdp_acceptor_;
	};
} // namespace p2engine

#endif//basic_urdp_acceptor_h__


