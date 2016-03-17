
include_directories(../)
make_executable(./ p2s_ppc)

##detail config for p2s_ppc
add_dependencies(p2s_ppc p2engine)
add_dependencies(p2s_ppc libupnp)
add_dependencies(p2s_ppc natpunch)
add_dependencies(p2s_ppc asfio)
add_dependencies(p2s_ppc httpdownload)
add_dependencies(p2s_ppc urlcrack)
add_dependencies(p2s_ppc common)
add_dependencies(p2s_ppc client)
add_dependencies(p2s_ppc tracker)
add_dependencies(p2s_ppc simple_server)
add_dependencies(p2s_ppc server)

target_link_libraries(p2s_ppc client)
target_link_libraries(p2s_ppc tracker)
target_link_libraries(p2s_ppc server)
target_link_libraries(p2s_ppc simple_server)
target_link_libraries(p2s_ppc urlcrack)
target_link_libraries(p2s_ppc httpdownload)
target_link_libraries(p2s_ppc asfio)
target_link_libraries(p2s_ppc natpunch)
target_link_libraries(p2s_ppc common)
target_link_libraries(p2s_ppc libupnp)
target_link_libraries(p2s_ppc p2engine)
target_link_libraries(p2s_ppc rt)

LINK_PROTOBUF(p2s_ppc)
LINK_OPENSSL(p2s_ppc)
LINK_BOOST(p2s_ppc boost_system boost_regex boost_thread boost_date_time boost_program_options boost_filesystem)

#set_target_properties(p2s_ppc PROPERTIES LINK_FLAGS "-s")
set_target_properties(p2s_ppc PROPERTIES LINK_FLAGS "-s")


