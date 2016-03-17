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
#ifndef P2ENGINE_CONFIG_FUNCTION_HPP
#define P2ENGINE_CONFIG_FUNCTION_HPP
#if defined(P2ENGINE_HAS_STD_FUNCTION)
# include <functional>
namespace p2engine{
	using std::function;
	using std::bind;
}
#else // defined(P2ENGINE_HAS_STD_FUNCTION)
# include <boost/function.hpp>
# include <boost/bind.hpp>
namespace p2engine{
	using boost::function;
	using boost::bind;
}
#endif // defined(P2ENGINE_HAS_STD_FUNCTION)

#endif//P2ENGINE_CONFIG_FUNCTION_HPP


