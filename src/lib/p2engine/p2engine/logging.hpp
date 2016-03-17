//
// loggine.hpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2009-2010, GuangZhu Wu 
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

#ifndef P2ENGINE_LOGGING_HPP
#define P2ENGINE_LOGGING_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "p2engine/push_warning_option.hpp"
#include "p2engine/config.hpp"
#include <boost/thread/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <queue>
#include <string>
#include "p2engine/pop_warning_option.hpp"

#include "p2engine/singleton.hpp"
namespace p2engine
{ 
	enum logging_level{
		LOG_FATAL = 1,  //ÖÂÃü´íÎó
		LOG_CRITICAL,   //ÑÏÖØ´íÎó
		LOG_ERROR,      //Ò»°ã´íÎó
		LOG_WARNING,    //¾¯¸æ
		LOG_NOTICE,     //×¢Òâ
		LOG_INFORMATION, //ÏûÏ¢
		LOG_DEBUG,      //µ÷ÊÔ
		LOG_TRACE       //¸ú×Ù
	};

	class logging
	{
		SINGLETON_ACCESS_DECLARE;
	protected:
		logging();
		void que_log(const char* msg);
		void do_log();
		void run();

		void open_file();

	public:
		~logging();
		void set_level(logging_level level){level_=level;}
		logging_level get_level() const{return level_;}
		void log(logging_level level, const char* fpath, long line, 
			const char* format, ...);
		void path_to_name(const char* path, char* name);

	protected:
		static const  char** s_loghead();

	protected:
		boost::shared_ptr<boost::thread> logging_thread_;

		std::queue<std::string> bufer_;
		FILE *fp_;
		boost::mutex file_mutex_;
		boost::mutex buf_mutex_;

		logging_level level_;
		
		size_t loged_size_;
	};
}

#ifdef USE_LOG
#	if !defined(_MSC_VER) || _MSC_VER>1310//msvc7.1
#	define LOG(x) 
#		define LogFatal(...) \
		if(LOG_FATAL<=p2engine::singleton<p2engine::logging>::instance().get_level())\
		p2engine::singleton<p2engine::logging>::instance().log(LOG_FATAL, __FILE__, __LINE__, ##__VA_ARGS__);

#		define LogCritical(...) \
		if(LOG_CRITICAL<=p2engine::singleton<p2engine::logging>::instance().get_level())\
		p2engine::singleton<p2engine::logging>::instance().log(LOG_CRITICAL, __FILE__, __LINE__, ##__VA_ARGS__);

#		define LogError(...) \
		if(LOG_ERROR<=p2engine::singleton<p2engine::logging>::instance().get_level())\
		p2engine::singleton<p2engine::logging>::instance().log(LOG_ERROR, __FILE__, __LINE__, ##__VA_ARGS__);

#		define LogWarning(...) \
		if(LOG_WARNING<p2engine::singleton<p2engine::logging>::instance().get_level())\
		p2engine::singleton<p2engine::logging>::instance().log(LOG_WARNING, __FILE__, __LINE__, ##__VA_ARGS__);

#		define LogInfo(...) \
		if(LOG_INFORMATION<=p2engine::singleton<p2engine::logging>::instance().get_level())\
		p2engine::singleton<p2engine::logging>::instance().log(LOG_INFORMATION, __FILE__, __LINE__, ##__VA_ARGS__);

#		define LogDebug(...) \
		if(LOG_DEBUG<=p2engine::singleton<p2engine::logging>::instance().get_level())\
		p2engine::singleton<p2engine::logging>::instance().log(LOG_DEBUG, __FILE__, __LINE__, ##__VA_ARGS__);

#		define LogTrace(...) \
		if(LOG_TRACE<=p2engine::singleton<p2engine::logging>::instance().get_level())\
		p2engine::singleton<p2engine::logging>::instance().log(LOG_TRACE, __FILE__, __LINE__, ##__VA_ARGS__);
#	endif
#else //USE_LOG
#	define LOG(x) 
#	if defined(_MSC_VER)
#		define LogFatal __noop
#		define LogCritical __noop
#		define LogError __noop
#		define LogWarning __noop
#		define LogInfo __noop
#		define LogDebug __noop
#		define LogTrace __noop
#	else
#		define LogFatal(...)
#		define LogCritical(...)
#		define LogError(...)
#		define LogWarning(...)
#		define LogInfo(...)
#		define LogDebug(...)
#		define LogTrace(...)
#	endif
#endif//USE_LOG

#endif//_LOGBASE_H_
