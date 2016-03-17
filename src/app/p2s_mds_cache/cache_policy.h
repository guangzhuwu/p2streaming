#ifndef _WFS_COMMON_CACHE_POLICY_HPP
#define _WFS_COMMON_CACHE_POLICY_HPP

#include <list>
#include <boost/unordered_map.hpp>
#include <boost/function.hpp>

namespace wfs{

	template <class Key, 
	class HashFunc = boost::hash<Key> , 
	class EqualFunc = boost::equal_to<Key>
	> 
	class cache_policy_null {
	public:
		typedef Key key_type;
		typedef HashFunc hasher;
		typedef EqualFunc key_equal;

	public:
		void hit(const Key&) {}
		void miss(const Key&) {}
		bool pop(Key*) {return false;}
		void erase(const Key&) {}
		void clear() {}
	};

	// Least Recently Used Cache Policy
	template <class Key, 
	class HashFunc = boost::hash<Key> , 
	class EqualFunc = std::equal_to<Key>
	> 
	class cache_policy_lru {
	public:
		typedef Key key_type;
		typedef HashFunc hasher;
		typedef EqualFunc key_equal;

	private:
		typedef typename std::list<Key> list_t;
		typedef typename std::list<Key>::iterator list_iterator_t;
		typedef typename std::list<Key>::reverse_iterator list_reverse_iterator_t;
		typedef typename boost::unordered_map<Key, list_iterator_t, HashFunc> hash_map_t;
		typedef typename boost::unordered_map<Key, list_iterator_t, HashFunc>::iterator hash_map_iterator_t;

	public:  
		cache_policy_lru():access_count_(997){}

		void hit(const Key &key)
		{
			queue_.push_back(key);
			hash_map_iterator_t itr = access_count_.find(key);
			if (itr != access_count_.end()) {
				queue_.erase(itr->second);
				itr->second=--queue_.end();
			}
			else
			{
				access_count_.insert(std::make_pair(key, --queue_.end()));
			}
		}

		void miss(const Key&) {}

		bool pop(Key *key)
		{
			if (queue_.empty())
				return false;
			*key = queue_.front();
			queue_.pop_front();
			access_count_.erase(*key);
			return true;
		}

		void erase(const Key &key)
		{
			hash_map_iterator_t i = access_count_.find(key);
			if (i != access_count_.end()) {
				queue_.erase(i->second);
				access_count_.erase(i);
			}	
		}

		/// Clears all the entries
		void clear() 
		{
			access_count_.clear();
			queue_.clear();
		}


	private:
		hash_map_t access_count_;
		list_t queue_;
	};

}//namespace wfs


#endif//_WFS_COMMON_CACHE_POLICY_HPP
