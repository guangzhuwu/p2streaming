#include "app_common/level_db.h"

#include <p2engine/push_warning_option.hpp>
#include "leveldb/db.h"
#include "leveldb/write_batch.h"

#include <assert.h>
#include <boost/assert.hpp>
#include <boost/shared_ptr.hpp>
#include "p2engine/macro.hpp"
#include <p2engine/pop_warning_option.hpp>

NAMESPACE_BEGIN(app_common);
using namespace p2engine;

level_db::level_db(const std::string& dbName)
{
	leveldb::Options options;
	options.create_if_missing = true;

	leveldb::DB* db;
	leveldb::Status status = leveldb::DB::Open(options, dbName, &db);
	BOOST_ASSERT(status.ok());
	db_.reset(db);

	if (!status.ok()) 
		std::cerr << status.ToString() << std::endl;
}

level_db::~level_db(){}
//////////////////////////////////////////////////////////////////////////
bool level_db::put(const std::string& key, const std::string& value, std::string& errorMsg)
{
	leveldb::Slice Key= key;
	leveldb::Slice Value=value;
	leveldb::Status s = db_->Put(leveldb::WriteOptions(), Key, Value);
	if(s.ok()) return true;

	errorMsg=s.ToString();
	return false;
}

bool level_db::get(const std::string& key, std::string& value, std::string& errorMsg)
{
	leveldb::Slice Key= key;
	leveldb::Status s=db_->Get(leveldb::ReadOptions(), Key, &value);
	if(s.ok()) return true;

	errorMsg=s.ToString();
	return false;
}

bool level_db::del(const std::string& key, std::string& errorMsg)
{
	leveldb::Slice Key= key;
	leveldb::Status s = db_->Delete(leveldb::WriteOptions(), Key);

	if(s.ok()) return true;

	errorMsg=s.ToString();
	return false;
}

bool level_db::update(const std::string& key, const std::string& value, std::string& errorMsg)
{
	leveldb::Slice Key= key;
	std::string V;
	leveldb::Status s=db_->Get(leveldb::ReadOptions(), Key, &V);
	if (s.ok()) {
		leveldb::WriteBatch batch;
		batch.Delete(Key);

		batch.Put(Key, value);
		s = db_->Write(leveldb::WriteOptions(), &batch);
	}
	if(s.ok()) return true;

	errorMsg=s.ToString();
	return false;
}

NAMESPACE_END(app_common);
