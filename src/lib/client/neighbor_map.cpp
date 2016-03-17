#include "client/neighbor_map.h"

NAMESPACE_BEGIN(p2client);

std::pair<neighbor_map::iterator, bool> neighbor_map::insert(const value_type& x)
{
	if ((int)container_.size()>=maxPeerCnt_)
		return std::make_pair(end(), false);

	std::pair<iterator, bool> rst=container_.insert(x);
	if (rst.second)
		alloc_id(rst.first);
	return rst;
}
std::pair<neighbor_map::iterator, bool> neighbor_map::insert(
	const const_iterator& hint, const value_type& x)
{
	if ((int)container_.size()>=maxPeerCnt_)
		return std::make_pair(end(), false);

	size_t orgSize=container_.size();
	iterator rst=container_.insert(hint, x);
	bool ok=container_.size()>orgSize;
	if (ok)alloc_id(rst);
	return std::make_pair(rst, ok);
}

void neighbor_map::erase(const peer_id_t& id)
{
	iterator x=container_.find(id);
	if (x!=container_.end())
	{
		int localId=x->second->local_id();
		BOOST_ASSERT(localId!=0xFFFFFFFF&&localId>=0&&localId<maxPeerCnt_);
		id_allocator_.release_id(localId);

		container_.erase(x);
	}
}
void neighbor_map::alloc_id(iterator& itr)
{
	static int uuid=0;
	int id=id_allocator_.alloc_id();
	BOOST_ASSERT(itr->second);
	BOOST_ASSERT(id<maxPeerCnt_);
	itr->second->local_id(id);
	itr->second->local_uuid(uuid++);
	if (uuid==INVALID_ID)//应该不会用完
		++uuid;
}

NAMESPACE_END(p2client);
