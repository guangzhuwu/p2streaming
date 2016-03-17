#Link to libraries, variable ${EXE_NAME} ${3RD_PARTY_LIBS_TO_LINK} must be provided in advance

if(WIN32)
	set(THIRD_PARTY_LIB_SUFFIX .lib)
else()
	set(THIRD_PARTY_LIB_SUFFIX .a)
endif()
function(link_boost EXE_NAME)
	if(NOT MSVC)
		if(WIN32)
			set(BOOST_LIB_DEBUG_SUFFIX -mt-sgd)
			set(BOOST_LIB_RELEASE_SUFFIX -mt-s)
		else()
			set(BOOST_LIB_DEBUG_SUFFIX )
			set(BOOST_LIB_DEBUG_SUFFIX )
			set(BOOST_LIB_RELEASE_SUFFIX )
		endif()	
		foreach(lib ${ARGV})
			if(NOT(${lib} STREQUAL ${EXE_NAME}))
				target_link_libraries(${EXE_NAME} debug ${BOOST_LIB_DIR}/lib${lib}${BOOST_LIB_DEBUG_SUFFIX}${THIRD_PARTY_LIB_SUFFIX})
				target_link_libraries(${EXE_NAME} optimized ${BOOST_LIB_DIR}/lib${lib}${BOOST_LIB_RELEASE_SUFFIX}${THIRD_PARTY_LIB_SUFFIX})
			endif()
		endforeach()
	endif(NOT MSVC)
endfunction()

function(link_openssl EXE_NAME)
		target_link_libraries(${EXE_NAME} optimized ${OPENSSL_LIB_DIR}/libssl${THIRD_PARTY_LIB_SUFFIX})
		target_link_libraries(${EXE_NAME} optimized ${OPENSSL_LIB_DIR}/libcrypto${THIRD_PARTY_LIB_SUFFIX})
		#set_target_properties(${EXE_NAME} PROPERTIES LINK_FLAGS "-ldl")
endfunction()

function(link_protobuf EXE_NAME)
	target_link_libraries(${EXE_NAME} debug ${PROTOBUF_LIB_DIR}/libprotobuf-lite${THIRD_PARTY_LIB_SUFFIX})
	target_link_libraries(${EXE_NAME} optimized ${PROTOBUF_LIB_DIR}/libprotobuf-lite${THIRD_PARTY_LIB_SUFFIX})
endfunction()

function(link_third_party_libs EXE_NAME)
	foreach(lib_to_link ${ARGV})
		if(NOT(${lib_to_link} STREQUAL ${EXE_NAME}))
			target_link_libraries(${EXE_NAME} ${lib_to_link})
		endif()
	endforeach()
endfunction()

macro(source_group_by_dir source_files)
    if(MSVC)
        set(sgbd_cur_dir ${CMAKE_CURRENT_SOURCE_DIR})
        foreach(sgbd_file ${${source_files}})
            string(REGEX REPLACE ${sgbd_cur_dir}/\(.*\) \\1 sgbd_fpath ${sgbd_file})
            string(REGEX REPLACE "\(.*\)/.*" \\1 sgbd_group_name ${sgbd_fpath})
            string(COMPARE EQUAL ${sgbd_fpath} ${sgbd_group_name} sgbd_nogroup)
            string(REPLACE "/" "\\" sgbd_group_name ${sgbd_group_name})
            if(sgbd_nogroup)
                set(sgbd_group_name "\\")
            endif(sgbd_nogroup)
        		source_group(${sgbd_group_name} FILES ${sgbd_file})
        endforeach(sgbd_file)
    endif(MSVC)
endmacro(source_group_by_dir)


macro(make_library prjPath prjName)
	#add_subdirectory(${prjPath})
	
	project(${prjName})
	set(LIB_NAME ${prjName})
	
	set(HEADER_DIR ${prjPath})
	set(SRC_DIR ${prjPath})
	
	file(GLOB_RECURSE HEADER_LIST ${HEADER_DIR}/*.h ${HEADER_DIR}/*.hpp)
	file(GLOB_RECURSE SRC_LIST ${SRC_DIR}/*.cpp ${SRC_DIR}/*.c ${SRC_DIR}/*.C ${SRC_DIR}/*.cc  ${SRC_DIR}/*.cxx)
	
	set(ALL_LIST ${HEADER_LIST} ${SRC_LIST})
	source_group_by_dir(ALL_LIST)

	add_library(
		${LIB_NAME}	
		STATIC
		${ALL_LIST}
		)
	#set_target_properties(${LIB_NAME} PROPERTIES COMPILE_FLAGS "-static -s -fcommon -MMD -MP -MF  -Wno-deprecated")
endmacro(make_library)

macro(make_library prjName)	
	project(${prjName})
	set(LIB_NAME ${prjName})
	
	set(HEADER_DIR .)
	set(SRC_DIR .)
	
	file(GLOB_RECURSE HEADER_LIST ${HEADER_DIR}/*.h ${HEADER_DIR}/*.hpp)
	file(GLOB_RECURSE SRC_LIST ${SRC_DIR}/*.cpp ${SRC_DIR}/*.c ${SRC_DIR}/*.C  ${SRC_DIR}/*.cc  ${SRC_DIR}/*.cxx)
	
	set(ALL_LIST ${HEADER_LIST} ${SRC_LIST})
	source_group_by_dir(ALL_LIST)

	add_library(
		${LIB_NAME}	
		STATIC
		${ALL_LIST}
		)
	#set_target_properties(${LIB_NAME} PROPERTIES COMPILE_FLAGS "-static -s -fcommon -MMD -MP -MF  -Wno-deprecated")
endmacro(make_library)

macro(make_shared prjName)	
	project(${prjName})
	set(LIB_NAME ${prjName})
	
	set(HEADER_DIR .)
	set(SRC_DIR .)
	
	file(GLOB_RECURSE HEADER_LIST ${HEADER_DIR}/*.h ${HEADER_DIR}/*.hpp)
	file(GLOB_RECURSE SRC_LIST ${SRC_DIR}/*.cpp ${SRC_DIR}/*.c ${SRC_DIR}/*.C  ${SRC_DIR}/*.cc  ${SRC_DIR}/*.cxx)
	
	set(ALL_LIST ${HEADER_LIST} ${SRC_LIST})
	source_group_by_dir(ALL_LIST)

	add_library(
		${LIB_NAME}	
		SHARED
		${ALL_LIST}
		)
	#set_target_properties(${LIB_NAME} PROPERTIES COMPILE_FLAGS "-shared -s -fcommon -MMD -MP -MF  -Wno-deprecated")
endmacro(make_shared)
	
macro(make_executable prjPath prjName)
	#add_subdirectory(${prjPath})
	
	project(${prjName})
	set(EXE_NAME ${prjName})
	
	set(HEADER_DIR ${prjPath})
	set(SRC_DIR ${prjPath})
	
	file(GLOB_RECURSE HEADER_LIST ${HEADER_DIR}/*.h ${HEADER_DIR}/*.hpp)
	file(GLOB_RECURSE SRC_LIST ${SRC_DIR}/*.cpp ${SRC_DIR}/*.c ${SRC_DIR}/*.C  ${SRC_DIR}/*.cc  ${SRC_DIR}/*.cxx)
	
	set(ALL_LIST ${HEADER_LIST} ${SRC_LIST})
	source_group_by_dir(ALL_LIST)

	add_executable(
		${EXE_NAME}
		${ALL_LIST}
		)
		
	#set_target_properties(${EXE_NAME} PROPERTIES COMPILE_FLAGS "-static -s -fcommon -MMD -MP -MF  -Wno-deprecated")
endmacro(make_executable)

macro(make_executable prjName)	
	project(${prjName})
	set(EXE_NAME ${prjName})
	
	set(HEADER_DIR .)
	set(SRC_DIR .)
	
	file(GLOB_RECURSE HEADER_LIST ${HEADER_DIR}/*.h ${HEADER_DIR}/*.hpp)
	file(GLOB_RECURSE SRC_LIST ${SRC_DIR}/*.cpp ${SRC_DIR}/*.c ${SRC_DIR}/*.C ${SRC_DIR}/*.cc  ${SRC_DIR}/*.cxx)
	
	set(ALL_LIST ${HEADER_LIST} ${SRC_LIST})
	source_group_by_dir(ALL_LIST)

	add_executable(
		${EXE_NAME}	
		${ALL_LIST}
		)
	#set_target_properties(${EXE_NAME} PROPERTIES COMPILE_FLAGS "-static -s -fcommon -MMD -MP -MF  -Wno-deprecated")
endmacro(make_executable)
