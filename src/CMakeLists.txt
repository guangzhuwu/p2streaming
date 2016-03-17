
add_definitions(-DP2ENGINE_NO_LIB=1)
#add_definitions(-DGLIBCXX_USE_WCHAR_T=1)
#add_definitions(-D__WINT_TYPE__=1)

#contrib
include_directories(../contrib)
#libs
include_directories(lib)
include_directories(lib/p2engine)

file(WRITE lib/p2engine/CMakeLists.txt "make_library(p2engine)")
file(WRITE lib/common/CMakeLists.txt "make_library(common)")
file(WRITE lib/libupnp/CMakeLists.txt "make_library(libupnp)")
file(WRITE lib/natpunch/CMakeLists.txt "make_library(natpunch)")
file(WRITE lib/httpdownload/CMakeLists.txt "make_library(httpdownload)")
file(WRITE lib/urlcrack/CMakeLists.txt "make_library(urlcrack)")
file(WRITE lib/asfio/CMakeLists.txt "make_library(asfio)")
file(WRITE lib/client/CMakeLists.txt "make_library(client)")
file(WRITE lib/tracker/CMakeLists.txt "make_library(tracker)")
file(WRITE lib/server/CMakeLists.txt "make_library(server)")
file(WRITE lib/simple_server/CMakeLists.txt "make_library(simple_server)")
file(WRITE lib/shunt/CMakeLists.txt "make_library(shunt)")
#file(WRITE lib/interface/CMakeLists.txt "make_library(interface)")
#file(WRITE lib/service_logic/CMakeLists.txt "make_library(service_logic)")
if(NOT PPC_MAKE_SO)
	#file(WRITE lib/ppc/CMakeLists.txt "make_library(ppc)")
else(NOT PPC_MAKE_SO)
	file(WRITE lib/ppc/CMakeLists.txt "make_shared(ppc_so)
	target_link_libraries(ppc_so interface)
	target_link_libraries(ppc_so service_logic)
	target_link_libraries(ppc_so client)
	target_link_libraries(ppc_so server)
	target_link_libraries(ppc_so tracker)
	target_link_libraries(ppc_so urlcrack)
	target_link_libraries(ppc_so httpdownload)
	target_link_libraries(ppc_so natpunch)
	target_link_libraries(ppc_so common)
	target_link_libraries(ppc_so p2engine)
	target_link_libraries(ppc_so miniupnpc)
	target_link_libraries(ppc_so librt.a)
	target_link_libraries(ppc_so libstdc++.a)

	target_link_libraries(ppc_so libresolv.a)
	LINK_PROTOBUF(ppc_so)
	LINK_BOOST(ppc_so 
		boost_system 
		boost_regex
		boost_thread
		boost_date_time
		boost_filesystem
		boost_program_options
		)
	"
	)
	add_subdirectory(lib/ppc) 
endif(NOT PPC_MAKE_SO)

add_subdirectory(lib/p2engine)
add_subdirectory(lib/common)
add_subdirectory(lib/natpunch)
add_subdirectory(lib/libupnp)
add_subdirectory(lib/asfio)
add_subdirectory(lib/httpdownload)
add_subdirectory(lib/urlcrack)
add_subdirectory(lib/client)
add_subdirectory(lib/tracker)
add_subdirectory(lib/server)
add_subdirectory(lib/simple_server)
add_subdirectory(lib/shunt) 

#add_subdirectory(lib/ppc) 

 
#------------------------------------------------------------------------------------
#apps
#------------------------------------------------------------------------------------
include_directories(app)

add_subdirectory(app/p2s_ppc)
#add_subdirectory(app/p2s_mds)
#add_subdirectory(app/p2s_shunt)
