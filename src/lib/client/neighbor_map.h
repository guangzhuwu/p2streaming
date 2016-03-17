//
// neighbor_map.h
// ~~~~~~~~~~~~~~~~
//
// Copyright (c) 2008 GuangZhu Wu (GuangZhuWu@gmail.com)
//
// All rights reserved. 
//
#ifndef peer_neighbor_map_h__
#define peer_neighbor_map_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include <boost/unordered_map.hpp>

#include "client/typedef.h"
#include "client/peer_connection.h"

namespace p2client{

	class neighbor_map
	{
		typedef boost::unordered_map<peer_id_t, peer_connection_sptr> container;
		typedef basic_local_id_allocator<int> id_allocator;
	public:
		typedef container::iterator iterator;
		typedef container::const_iterator const_iterator;
		typedef container::value_type value_type;

		neighbor_map(size_t maxPeerCnt)
			:container_(maxPeerCnt*2+1)
			, id_allocator_(true)
			, maxPeerCnt_(maxPeerCnt)
		{
		}
		size_t size()const{return container_.size();}
		bool empty()const{return container_.empty();}
		iterator begin(){return container_.begin();}
		const_iterator begin()const{return container_.begin();}
		iterator end(){return container_.end();}
		const_iterator end()const{return container_.end();}
		iterator find(const peer_id_t& id){return container_.find(id);}
		const_iterator find(const peer_id_t& id)const{return container_.find(id);}
		std::pair<iterator, bool> insert(const value_type& x);
		std::pair<iterator, bool> insert(const const_iterator& hint, const value_type& x);
		void erase(const iterator& x)
		{
			BOOST_ASSERT(x!=container_.end());
			int id=x->second->local_id();
			BOOST_ASSERT(id!=0xFFFFFFFF&&id>=0&&id<maxPeerCnt_);
			id_allocator_.release_id(id);
			container_.erase(x);
		}
		void erase(const peer_id_t& id);
		void clear()
		{
			container_.clear();
			id_allocator_.reset();
		}
	private:
		void alloc_id(iterator& itr);
	private:
		container container_;
		id_allocator id_allocator_;
		int maxPeerCnt_;
	};

}
#endif//peer_neighbor_map_h__


