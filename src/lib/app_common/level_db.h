#ifndef p2s_p2p_app_common_level_db_h__
#define p2s_p2p_app_common_level_db_h__

#include <string>
#include <memory>

#include <boost/noncopyable.hpp>
namespace leveldb{
	class DB;
}

namespace app_common{

	class level_db
		:public boost::noncopyable
	{
	public:
		explicit level_db(const std::string& dbName);
		~level_db();
		//////////////////////////////////////////////////////////////////////////
		bool put(const std::string& key, const std::string& value, std::string& errorMsg);
		bool get(const std::string& key, std::string& value, std::string& errorMsg);
		bool del(const std::string& key, std::string& errorMsg);
		bool update(const std::string& key, const std::string& value, std::string& errorMsg);

	private:
		std::auto_ptr<leveldb::DB> db_;
	};

};

#endif // p2s_p2p_app_common_level_db_h__
