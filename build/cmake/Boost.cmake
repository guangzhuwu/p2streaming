cmake_minimum_required (VERSION 2.6)

##############################################################################
if(NOT MSVC)
add_definitions(-DBOOST_ALL_NO_LIB=1)
endif(NOT MSVC)

set(WITH_LIBS "--with-system --with-filesystem --with-thread --with-date_time --with-regex")

#Following define cannot be set with MSVC as it yields to a
if (NOT WIN32)
	add_definitions(-DBOOST_THREAD_POSIX)
endif (NOT WIN32 )

set(BOOST_INC_PATH_DESCRIPTION "The Directory containing the boost include files was not found. Please Use  cmake -DBOOST_SOURCE_DIR=\"your_boost_dir\" ...")
find_path(BOOST_INC_DIR boost/config.hpp
      # Look in BOOST_SOURCE_DIR.
      ${BOOST_SOURCE_DIR}
      # inbuild dir.
      ${CMAKE_SOURCE_DIR}/contrib/boost
	  ${CMAKE_SOURCE_DIR}/../../contrib/boost
      # Help the user find it if we cannot.
      DOC "${BOOST_INC_PATH_DESCRIPTION}"
  )
  
if(BOOST_INC_DIR)
#include_directories(${BOOST_INC_DIR})
	set(BOOST_TOOLSET)
	if(MSVC)
		if(MSVC10)
			set(BOOST_TOOLSET "--toolset=msvc-10.0")
		elseif(MSVC90)
			set(BOOST_TOOLSET "--toolset=msvc-9.0")
		elseif(MSVC80)
			set(BOOST_TOOLSET "--toolset=msvc-8.0")
		elseif(MSVC71)
			set(BOOST_TOOLSET "--toolset=msvc-7.1")
		elseif(MSVC70)
			set(BOOST_TOOLSET "--toolset=msvc-7.0")
		elseif(MSVC60)
			set(BOOST_TOOLSET "--toolset=msvc-6.0")
		else()
			message(SEND_ERROR "Unknown MSVC version!")
		endif()
	else()
		string(REGEX MATCH ".*/.*$" COMPILER_NAME   ${CMAKE_C_COMPILER} )
		if(COMPILER_NAME)
			string(REGEX MATCH ".*/([^\\./]+).*$" COMPILER_NAME   ${CMAKE_C_COMPILER} )
		else()
			string(REGEX MATCH "([^\\.]+).*$" COMPILER_NAME   ${CMAKE_C_COMPILER} )
		endif()
		set(COMPILER_NAME ${CMAKE_MATCH_1})
		set(BOOST_TOOLSET "--toolset=${COMPILER_NAME}")
		message(STATUS "COMPILER_NAME: ${COMPILER_NAME}")
		ADD_CUSTOM_COMMAND(
        COMMAND echo "using gcc: $(COMPILER_NAME): ${CMAKE_C_COMPILER} ;" >> ${BOOST_INC_DIR}/tools/build/v2/user-config.jam
		COMMAND echo "using gcc: $(COMPILER_NAME): ${CMAKE_C_COMPILER} ;" >> ${BOOST_INC_DIR}/project-config.jam
        ) 
	endif(MSVC)
	
	if(WIN32) 
		 set(BUILD_JAM_CMD "bootstrap.bat")
		 set(JAM_CMD "bjam.exe") 
	else() 
		 set(BUILD_JAM_CMD "bootstrap.sh")
		 set(JAM_CMD "bjam")
	endif(WIN32)
	find_path(BOOST_BJAM_PATH ${JAM_CMD}
      ${BOOST_INC_DIR}
	  )
	if(BOOST_BJAM_PATH)
		message(STATUS "BOOST_BJAM_PATH: ${BOOST_BJAM_PATH} found")
	else(BOOST_BJAM_PATH)
		execute_process(COMMAND ${BOOST_INC_DIR}/${BUILD_JAM_CMD} WORKING_DIRECTORY ${BOOST_INC_DIR})
	endif(BOOST_BJAM_PATH)
	
	if(MSVC)
		set(BOOST_LIB_DIR "${BOOST_INC_DIR}/stage/lib")
	else()
		set(BOOST_LIB_DIR "${BOOST_INC_DIR}/stage/lib/${COMPILER_NAME}")
	endif(MSVC)
	
	message(STATUS "${BOOST_INC_DIR}/${JAM_CMD} ${BOOST_TOOLSET} --stagedir=\${BOOST_LIB_DIR}\ stage variant=release threading=multi link=static ${WITH_LIBS}")
	execute_process(COMMAND ${BOOST_INC_DIR}/${JAM_CMD}
		${BOOST_TOOLSET} --stagedir=\"${BOOST_LIB_DIR}\" stage
		variant=release threading=multi link=static ${WITH_LIBS}
	) 		
else(BOOST_INC_DIR)
	message(SEND_ERROR ${BOOST_INC_DIR_DESCRIPTION})
endif(BOOST_INC_DIR)

