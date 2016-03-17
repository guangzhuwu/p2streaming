#ifndef _WFS_COMMON_CACHE_HPP
#define _WFS_COMMON_CACHE_HPP

#include "p2s_mds_cache/cache_policy.h"
#include "p2engine/typedef.hpp"

using namespace p2engine;
namespace wfs{

	template <class Key, class Value, class Mutex, 
	class HashFunc = boost::hash<Key> , 
	class EqualFunc = std::equal_to<Key>, 
	class Policy = cache_policy_lru<Key, HashFunc, EqualFunc> 
	> 
	class cache {
	private:
		typedef typename Mutex::scoped_lock scoped_lock;
		typedef std::pair<Value, bool> entry_t;
		typedef typename boost::unordered_map<Key, entry_t, HashFunc, EqualFunc> hash_map_t;

	public:
		typedef boost::function<void (const Key &k, const Value &v)>  pop_callback_t;
		typedef typename hash_map_t::const_iterator const_iterator;

	public:
		cache(size_t msize =(1 << 20), 
			pop_callback_t evict_cb = boost::bind(&void_pop_callback, _1, _2)
			) : max_size_(msize), pop_callback_(evict_cb), cache_map_(997)
		{
			std::cout<<"------------cache size="<<msize<<"\n";
		}

		size_t max_size() const 
		{
			scoped_lock lock(hash_mutex_);
			return max_size_;
		}

		size_t size() const 
		{
			scoped_lock lock(hash_mutex_);

			return cache_map_.size();
		}

		void clear() 
		{
			scoped_lock lock(hash_mutex_);

			cache_map_.clear();
			policy_.clear();
		}

		const_iterator begin()const {return cache_map_.begin();}
		const_iterator end()const {return cache_map_.end();}

		bool resize(size_t msize)
		{
			scoped_lock lock(hash_mutex_);

			if (max_size_ < msize) {
				max_size_ = msize;
				return true;
			}

			Key k; Value v;
			while (cache_map_.size() > max_size_) {
				if (!policy_.pop(&k))
					return false;

				bool callBack=cache_map_[k].second;
				if (callBack)
					v = cache_map_[k].first;
				cache_map_.erase(k);
				if (callBack) {
					lock.unlock();
					pop_callback_(k, v);
					lock.lock();
				}
			}
			return true;
		}

		bool write(const Key &key, const Value &data, const bool callback = true)
		{
			Key ek; entry_t ev;
			{
				scoped_lock lock(hash_mutex_);
				hash_map_t::iterator itr=cache_map_.find(key);
				if (itr==cache_map_.end())
				{
					if (cache_map_.size() >= max_size_) {
						if (!policy_.pop(&ek))
							return false;
						hash_map_t::iterator itrPop=cache_map_.find(ek);
						BOOST_ASSERT(itrPop!=cache_map_.end());
						ev = itrPop->second;
						cache_map_.erase(itrPop);
					}
					cache_map_.insert(std::make_pair(key, entry_t(data, callback)));	    
				}
				else
				{
					itr->second=entry_t(data, callback);
				}
				policy_.hit(key);
			}
			if (ev.second)
				pop_callback_(ek, ev.first);
			return true;
		}

		bool read(const Key &key, Value *data)
		{
			scoped_lock lock(hash_mutex_);

			const_iterator i = cache_map_.find(key);
			if (i == cache_map_.end()) {
				policy_.miss(key);
				return false;
			} else {
				policy_.hit(key);
				*data = i->second.first;
				return true;
			}
		}

		bool find(const Key &key)
		{
			scoped_lock lock(hash_mutex_);

			const_iterator i = cache_map_.find(key);
			return i != cache_map_.end();
		}

		void free(const Key &key)
		{
			scoped_lock lock(hash_mutex_);

			policy_.erase(key);
			cache_map_.erase(key);
		}

		float load_factor()const //The average number of elements per bucket. 
		{
			return cache_map_.load_factor();
		}

	private:
		static void void_pop_callback(const Key &k, const Value &v) { }

	private:
		Policy policy_;
		Mutex hash_mutex_;
		hash_map_t cache_map_;
		size_t max_size_;
		pop_callback_t pop_callback_;
	};

}//namespace wfs

#endif//_WFS_COMMON_CACHE_HPP

