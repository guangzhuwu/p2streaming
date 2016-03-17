##############################################################################
if(NOT MSVC)
	add_definitions(-DBOOST_ALL_NO_LIB=1)
endif(NOT MSVC)

#Following define cannot be set with MSVC as it yields to a
if (NOT WIN32)
	add_definitions(-DBOOST_THREAD_POSIX)
endif (NOT WIN32 )

set(BOOST_SRC_PATH_DESCRIPTION "The Directory containing the boost include files was not found. Please Use  cmake -BOOST_SOURCE_DIR=\"your_boost_dir\" ...")
find_path(BOOST_SRC_DIR bootstrap.sh
      # Look in BOOST_SOURCE_DIR.
      ${BOOST_SOURCE_DIR}
      # Help the user find it if we cannot.
      DOC "${BOOST_SRC_PATH_DESCRIPTION}"
	NO_DEFAULT_PATH
  )

message(STATUS "BOOST_SRC_DIR: ${BOOST_SRC_DIR} ")
  
if(BOOST_SRC_DIR)
#include_directories(${BOOST_SRC_DIR})
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
	endif(MSVC)
	
 	set(JAM_CMD)
	if(WIN32) 
		 set(BUILD_JAM_CMD "bootstrap.bat")
		 set(JAM_CMD "bjam.exe") 
	else() 
		 set(BUILD_JAM_CMD "bootstrap.sh")
		 set(JAM_CMD "bjam")
	endif(WIN32)
	find_path(BOOST_BJAM_PATH ${JAM_CMD}
		${BOOST_SRC_DIR}
	  	)

	if(BOOST_BJAM_PATH)
		message(STATUS "BOOST_BJAM_PATH: ${BOOST_BJAM_PATH} found")
	else(BOOST_BJAM_PATH)
		message(STATUS "building ${JAM_CMD}, waiting....")
		execute_process(COMMAND ${BOOST_SRC_DIR}/${BUILD_JAM_CMD} WORKING_DIRECTORY ${BOOST_SRC_DIR})
	endif(BOOST_BJAM_PATH)
	
	#if(MSVC)
	#	set(BOOST_LIB_DIR "${BOOST_SRC_DIR}/stage/lib")
	#else()
	#	set(BOOST_LIB_DIR "${BOOST_SRC_DIR}/stage/lib/${COMPILER_NAME}")
	#endif(MSVC)
	
	if(MSVC)
	else()
		#execute_process(COMMAND echo "using gcc: ${COMPILER_NAME} : ${CMAKE_C_COMPILER} ;" >> ${BOOST_SRC_DIR}/tools/build/v2/user-config.jam)
		file(WRITE ${BOOST_SRC_DIR}/project-config.jam "using gcc : : ${CMAKE_C_COMPILER} ;")
	endif(MSVC)
	
	if(MSVC)
		set(BOOST_SYSTEM_LIB libboost_filesystem.lib)
	else()
		set(BOOST_SYSTEM_LIB libboost_filesystem.a)
	endif(MSVC)

	find_path(BOOST_LIB_DIR ${BOOST_SYSTEM_LIB}
	      ${PREFIX}/lib
		NO_DEFAULT_PATH
	  )
	
	if(NOT BOOST_LIB_DIR)
		message(STATUS "building boost libs, waiting....")
		execute_process(COMMAND ./${JAM_CMD} install
			--prefix=${PREFIX}
			--with-system --with-thread --with-date_time --with-regex --with-program_options --with-filesystem
			variant=release threading=multi link=static 
			WORKING_DIRECTORY ${BOOST_SRC_DIR}
			OUTPUT_VARIABLE OUTVAR RESULT_VARIABLE RESVAR
		)
		 if(NOT ${RESVAR} EQUAL 0)
			MESSAGE("${OUTVAR}")
		 endif() 
	endif(NOT BOOST_LIB_DIR)
else(BOOST_SRC_DIR)
	message(SEND_ERROR ${BOOST_SRC_PATH_DESCRIPTION})
endif(BOOST_SRC_DIR)

