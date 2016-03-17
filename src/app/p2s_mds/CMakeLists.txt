
include_directories(include)
make_executable(p2s_mds)

##detail config for p2s_mds
add_dependencies(p2s_mds miniupnpc)
add_dependencies(p2s_mds p2engine)
add_dependencies(p2s_mds natpunch)
add_dependencies(p2s_mds common)
add_dependencies(p2s_mds client)
add_dependencies(p2s_mds tracker)
add_dependencies(p2s_mds server)
add_dependencies(p2s_mds interface)
add_dependencies(p2s_mds service_logic)


target_link_libraries(p2s_mds interface)
target_link_libraries(p2s_mds service_logic)
target_link_libraries(p2s_mds client)
target_link_libraries(p2s_mds server)
target_link_libraries(p2s_mds tracker)
target_link_libraries(p2s_mds natpunch)
target_link_libraries(p2s_mds common)
target_link_libraries(p2s_mds p2engine)
target_link_libraries(p2s_mds miniupnpc)
link_boost(p2s_mds
	boost_system 
	boost_regex
	boost_thread
	boost_date_time
	boost_filesystem
	)
link_protobuf(p2s_mds)

set_target_properties(p2s_mds PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PREFIX}/bin)
#set_target_properties(p2s_mds PROPERTIES LINK_FLAGS "-static -s -pthread -lrt -fcommon -MMD -MP -MF  -Wno-deprecated")

#if(${PLATFORM} STREQUAL "windows")
#	LINK_THIRD_PARTY_LIBS(p2s_mds Rpcrt4)
#elseif(${PLATFORM} STREQUAL "linux")
#	LINK_THIRD_PARTY_LIBS(p2s_mds pthread  rt)
#endif()
