#Authors: GuangZhuWu (guangzhuwu@gmail.com)
# Copyright: see the accompanying file COPYING or copy at
     
# Commonly used options
# --------------------- 
#
# The following lists some commonly used options to configure the build system
# (an option name following by (*) indicates the default option):
#
#   CMAKE_INSTALL_PREFIX = /usr/local(*) (any path can be used)
#   CMAKE_BUILD_TYPE     = None | Debug | Release(*) 
#   BUILD_SHARED_LIBS    = ON   | OFF(*)

#   BOOST_SRC_DIR
#	PROTOBUF_SRC_DIR

cmake_minimum_required (VERSION 2.6)

INCLUDE	(CheckCXXSourceCompiles)
INCLUDE	(CheckIncludeFileCXX)
INCLUDE	(CheckLibraryExists)
INCLUDE (CheckFunctionExists)
INCLUDE (CheckIncludeFile)
INCLUDE (CheckIncludeFiles)
INCLUDE (CheckSymbolExists)
INCLUDE (CheckTypeSize)
INCLUDE (CheckCCompilerFlag)
INCLUDE (CheckLibraryExists)

##---------------------------------------------------
## You must set the platform 
##--------------------------------------------------- 
set(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} ${CMAKE_CXX_FLAGS_DEBUG} -O0 -Wall -g -ggdb")
set(CMAKE_CXX_FLAGS_RELEASE "$ENV{CXXFLAGS} ${CMAKE_CXX_FLAGS_RELEASE} -O3 -Wall")

if(NOT PLATFORM)
	message(SEND_ERROR "To make cross-compile work, please use cmake -DPLATFORM=\"your_platform_of_anyname\" ...")
endif(NOT PLATFORM)

if(ADD_DEFINE)
  add_definitions( ${ADD_DEFINE} )
endif(ADD_DEFINE)

if(NOT MCPU)
else(NOT MCPU)
	add_definitions(-mcpu=${MCPU})
endif(NOT MCPU)

if(NOT NO_WSTRING)
else(NOT NO_WSTRING)
	#add_definitions(-D__UCLIBCXX_HAS_WCHAR__)
	add_definitions(-DBOOST_NO_WSTRING=1)
	add_definitions(-DBOOST_NO_INTRINSIC_WCHAR_T=1)
	add_definitions(-DWCHAR_MIN=__WCHAR_MIN)
	add_definitions(-DWCHAR_MAX=__WCHAR_MAX)
	message(STATUS "NO_WSTRING")

	add_definitions(-DBOOST_ASIO_DISABLE_DEV_POLL=1)
	add_definitions(-DBOOST_ASIO_DISABLE_EPOLL=1)
	add_definitions(-DBOOST_ASIO_DISABLE_KQUEUE=1)
	
	set(BOOST_BJAM_DEFINE "debug/define=BOOST_NO_INTRINSIC_WCHAR_T=1 release/define=BOOST_NO_INTRINSIC_WCHAR_T=1")
endif(NOT NO_WSTRING)

add_definitions(-DBOOST_ASIO_HEADER_ONLY=1)

#check compiler
check_include_file_cxx(ifaddrs.h P2ENGINE_USE_IFADDRS)
check_cxx_source_compiles("
    #include <fcntl.h>
    int main(){
		posix_fadvise(0, 0, 0, 0);
        return 0;
    }
    " HAVE_POSIX_FADVISE
)
check_include_file_cxx(iconv.h P2ENGINE_USE_ICONV)
if(P2ENGINE_USE_IFADDRS)
	add_definitions(-DP2ENGINE_USE_IFADDRS=1)
else(P2ENGINE_USE_IFADDRS)
	add_definitions(-DP2ENGINE_USE_IFADDRS=0)
endif(P2ENGINE_USE_IFADDRS)
if(HAVE_POSIX_FADVISE)
	add_definitions(-DHAVE_POSIX_FADVISE=1)
else(HAVE_POSIX_FADVISE)
	add_definitions(-DHAVE_POSIX_FADVISE=0)
endif(HAVE_POSIX_FADVISE)
if(P2ENGINE_USE_ICONV)
	add_definitions(-DP2ENGINE_USE_ICONV=1)
else(P2ENGINE_USE_ICONV)
	add_definitions(-DP2ENGINE_USE_ICONV=0)
endif(P2ENGINE_USE_ICONV)
	add_definitions(-DONLY_P2CLIENT=1)

project(p2s_live)

##---------------------------------------------------
## Compiler information 
##--------------------------------------------------- 
if(MSVC)
		if(MSVC10)
			set(COMPLIER_NAME "msvc-10.0")
		elseif(MSVC90)
			set(COMPLIER_NAME "msvc-9.0")
		elseif(MSVC80)
			set(COMPLIER_NAME "msvc-8.0")
		elseif(MSVC71)
			set(COMPLIER_NAME "msvc-7.1")
		elseif(MSVC70)
			set(COMPLIER_NAME "msvc-7.0")
		elseif(MSVC60)
			set(COMPLIER_NAME "msvc-6.0")
		else()
			message(SEND_ERROR,"Unknown MSVC version!")
		endif()
else(MSVC)
		string(REGEX MATCH ".*/.*$" COMPILER_NAME   ${CMAKE_C_COMPILER} )
		if(COMPILER_NAME)
			string(REGEX MATCH ".*/([^\\./]+).*$" COMPILER_NAME   ${CMAKE_C_COMPILER} )
		else()
			string(REGEX MATCH "([^\\.]+).*$" COMPILER_NAME   ${CMAKE_C_COMPILER} )
		endif()
		set(COMPILER_NAME ${CMAKE_MATCH_1})
endif(MSVC)
message(STATUS "CXX COMPILER VERSION: ${CXX_COMPILER_VERSION}")


##---------------------------------------------------
## Determine build type
##--------------------------------------------------- 
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE} " )

##---------------------------------------------------
## Set global compiler flags (if available)
##--------------------------------------------------- 
if( CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX )
  check_c_compiler_flag(-Wall ACCEPTS_WALL )
  if( ACCEPTS_WALL )
    add_definitions( -Wall ) 
  endif( ACCEPTS_WALL )
  
  if( NOT MINGW )
    check_c_compiler_flag(-pthread HAVE_PTHREAD)
    if( HAVE_PTHREAD )
	    add_definitions(-pthread)
	    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -pthread")
	endif( HAVE_PTHREAD )
    check_library_exists(rt clock_gettime "" HAVE_LIBRT) 
    if( HAVE_LIBRT )
		set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lrt")
    endif( HAVE_LIBRT )
  endif( NOT MINGW )

  check_c_compiler_flag(-fno-strict-overflow ACCEPTS_FNO_STRICT_OVERFLOW )
  if( ACCEPTS_FNO_STRICT_OVERFLOW)
    add_definitions( -fno-strict-overflow )
  endif( ACCEPTS_FNO_STRICT_OVERFLOW)

  check_c_compiler_flag(-pipe ACCEPTS_PIPE )
  if( ACCEPTS_PIPE )
    add_definitions( -pipe ) 
  endif( ACCEPTS_PIPE )

  # Following flag will cause warning on MacOSX, if enabled:
  #686-apple-darwin9-g++-4.0.1: -as-needed: linker input file unused because linking not done
  if( NOT APPLE)
    check_c_compiler_flag(-Wl,-as-needed ACCEPTS_AS_NEEDED )
    if( ACCEPTS_AS_NEEDED )
      add_definitions( -Wl,-as-needed ) 
    endif( ACCEPTS_AS_NEEDED )
  endif( NOT APPLE )

elseif(MSVC)
  add_definitions( "/EHa" )   

endif( CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX )
message(STATUS "CMAKE_SYSTEM_NAME: ${CMAKE_SYSTEM_NAME}")

## --------------------------------------------------
## Detect architecture
## --------------------------------------------------
if (UNIX AND NOT WIN32)
  if (CMAKE_SIZEOF_VOID_P MATCHES "8")
    message (STATUS "Architecture: 64bit Unix" )
    set(ATERM_FLAGS "-m64 -DAT_64BIT")
    add_definitions("-DAT_64BIT")
    # set(ARCHITECTURE "x86_64")
  endif (CMAKE_SIZEOF_VOID_P MATCHES "8")
  if (CMAKE_SIZEOF_VOID_P MATCHES "4")
    message (STATUS "Architecture: 32bit Unix" )
    set(ATERM_FLAGS "-m32")
    # set(ARCHITECTURE "i386")
  endif (CMAKE_SIZEOF_VOID_P MATCHES "4")	
endif (UNIX AND NOT WIN32)

if(WIN32)
	string(REGEX MATCH "[0-9]+.[0-9]+" VERSION_STR  "${CMAKE_SYSTEM}")
	string(SUBSTRING ${VERSION_STR} 0 1 V1)
	string(SUBSTRING ${VERSION_STR} 2 1 V2)
	set(WIN_VER_STR "0x0${V1}0${V2}")
	#For Boost.Asio under Windows
	add_definitions(-D_WIN32_WINNT=${WIN_VER_STR})
	if(CYGWIN)
		add_definitions(-D__USE_W32_SOCKETS)
	endif(CYGWIN)
endif(WIN32)


# Multi-threading support
if(CMAKE_SYSTEM_NAME STREQUAL "SunOS")
  set(MULTI_THREADED_COMPILE_FLAGS "-pthreads")
  set(MULTI_THREADED_LINK_LIBS rt)
  set(PLATFORM_OS "sunos")
elseif(CMAKE_SYSTEM_NAME STREQUAL "BeOS")
  # No threading options necessary for BeOS
  set(PLATFORM_OS "beos")
elseif(CMAKE_SYSTEM_NAME MATCHES ".*BSD")
  set(MULTI_THREADED_COMPILE_FLAGS "-pthread")
  set(MULTI_THREADED_LINK_LIBS pthread)
  set(PLATFORM_OS "bsd")
elseif(CMAKE_SYSTEM_NAME STREQUAL "DragonFly")
  # DragonFly is a FreeBSD bariant
  set(MULTI_THREADED_COMPILE_FLAGS "-pthread")
  set(PLATFORM_OS "dragonfly")
elseif(CMAKE_SYSTEM_NAME STREQUAL "IRIX")
  # TODO: GCC on Irix doesn't support multi-threading?
  set(PLATFORM_OS "irix")
elseif(CMAKE_SYSTEM_NAME STREQUAL "HP-UX")
  # TODO: gcc on HP-UX does not support multi-threading?
  set(PLATFORM_OS "hpux")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  # No threading options necessary for Mac OS X
  set(PLATFORM_OS "macos")
elseif(UNIX)
  # Assume -pthread and -lrt on all other variants
  set(MULTI_THREADED_COMPILE_FLAGS "-pthread -D_REENTRANT")
  set(MULTI_THREADED_LINK_FLAGS "")
  check_c_compiler_flag(-pthread CXX_ACCEPTS_PTHREAD )
  if( CXX_ACCEPTS_PTHREAD )
    set (MULTI_THREADED_LINK_LIBS ${MULTI_THREADED_LINK_LIBS} pthread)
  endif( CXX_ACCEPTS_PTHREAD )
  check_library_exists("rt" "" "" HAVE_LIBRT_RT) 
  if( HAVE_LIBRT_RT )
		set(CMAKE_EXE_LINKER_FLAGS "${MULTI_THREADED_LINK_LIBS} -lrt")
  endif( HAVE_LIBRT_RT )
  #check_c_compiler_flag(-lc CXX_ACCEPTS_C )
  #check_library_exists("c" "" "" HAVE_LIBRT_C) 
  #if( HAVE_LIBRT_C )
	#	set(CMAKE_EXE_LINKER_FLAGS "${MULTI_THREADED_LINK_LIBS} c")
  #endif( HAVE_LIBRT_C )
  if (MINGW)
    set(PLATFORM_OS "mingw")
  elseif(CYGWIN)
    set(PLATFORM_OS "cygwin")
  elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(PLATFORM_OS "linux")
  else()
    set(PLATFORM_OS "unix")
  endif()
elseif(WIN32)
  set(PLATFORM_OS "windows")
else()
  set(PLATFORM_OS "unknown")
endif()
message(STATUS "PLATFORM_OS:${PLATFORM_OS}")

## --------------------------------------------------
## Shared or Static
## --------------------------------------------------	
if(BUILD_SHARED_LIBS)
  if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX) 
    if(NOT MINGW)
      check_c_compiler_flag(-fPIC CXX_ACCEPTS_FPIC )
      if( CXX_ACCEPTS_FPIC )
        add_definitions( -fPIC )
        set(BUILD_SHARED_LIBS_OPTION "-fPIC")  
      endif( CXX_ACCEPTS_FPIC )
    endif(NOT MINGW)
  endif( CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX)

  if(CMAKE_CXX_COMPILER MATCHES "icpc")
    check_c_compiler_flag(-fPIC CXX_ACCEPTS_FPIC )
    if( CXX_ACCEPTS_FPIC )
      add_definitions( -fPIC )
      set(BUILD_SHARED_LIBS_OPTION "-fPIC")  
    endif( CXX_ACCEPTS_FPIC )
  endif(CMAKE_CXX_COMPILER MATCHES "icpc")

  if(WIN32)
    if(MINGW OR MSYS)
      get_filename_component( MINGW_LIB_DIR ${CMAKE_CXX_COMPILER} PATH )

      # Install required MINGW DLLs
      install(
		FILES
        ${MINGW_LIB_DIR}/libgcc_s_dw2-1.dll
		${MINGW_LIB_DIR}/mingwm10.dll
        DESTINATION ${P2SP2P_BIN_DIR}
      )
	  
    endif(MINGW OR MSYS)
  endif(WIN32)
  
  ##cross comple for arm using arm-none-linux-gnueabi-gcc
  if(${CMAKE_C_COMPILER} MATCHES arm-none-linux-gnueabi-gcc*)
    message(SEND_ERROR "Must using static ${CMAKE_C_COMPILER}")
  endif()
  
else(BUILD_SHARED_LIBS)
 ##cross comple for arm using arm-none-linux-gnueabi-gcc
  if(${CMAKE_C_COMPILER} MATCHES arm-none-linux-gnueabi-gcc*)
    message(STATUS "CMAKE_C_COMPILER: ${CMAKE_C_COMPILER}")
	#add_definitions(-static,-s,-fcommon,-MMD,-MP,-MF)
  endif()
  set(BUILD_SHARED_LIBS false)
endif(BUILD_SHARED_LIBS)
message(STATUS "BUILD_SHARED_LIBS: ${BUILD_SHARED_LIBS}")

##---------------------------------------------------
## config
##--------------------------------------------------- 
if(MSVC)
	#We statically link to reduce dependancies
	foreach(flag_var CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
	  if(${flag_var} MATCHES "/MDd")
	      string(REGEX REPLACE "/MDd" "/MTd" ${flag_var} "${${flag_var}}")
	  endif()
	  if(${flag_var} MATCHES "/MD")
	      string(REGEX REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
	  endif()
	endforeach()
endif()

##---------------------------------------------------
## prefix
##--------------------------------------------------- 
if(NOT PREFIX)
	set(PREFIX ${CMAKE_SOURCE_DIR}/bin/${PLATFORM}/${CMAKE_BUILD_TYPE})
endif(NOT PREFIX)
set(EXECUTABLE_OUTPUT_PATH ${PREFIX}/bin)
set(LIBRARY_OUTPUT_PATH ${PREFIX}/lib)

#svn checkout
#execute_process(COMMAND svn co http://192.168.0.199/svn/p2pvod/trunk #"${CMAKE_SOURCE_DIR}/../trunk")

message(STATUS "cxx defines: ${CMAKE_CXX_DEFINES}" )

include_directories(${PREFIX}/include)

include(build/cmake/Function.cmake)
include(build/cmake/MakeBoost.cmake)
include(build/cmake/MakeProtobuf.cmake)
include(build/cmake/MakeOpenssl.cmake)

add_subdirectory(contrib)
add_subdirectory(src)
