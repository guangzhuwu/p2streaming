#===================================================================
#p2s_ppc
#===================================================================
LOCAL_PATH:= $(call my-dir)
SRC_PATH_ROOT:=$(LOCAL_PATH)/../../src

include $(CLEAR_VARS)
include $(APP_PROJECT_PATH)/Include.mk

LOCAL_MODULE := p2s_ppc
LOCAL_MODULE_TAGS := release
LOCAL_FORCE_STATIC_EXECUTABLE := true

#LOCAL_LDLIBS +=../obj/local/$(APP_ABI)/libgnustl_static.a
  
LOCAL_STATIC_LIBRARIES += \
			libclient \
			libcommon \
			simple_server\
			liburlcrack\
			libhttpdownload\
			libasfio\
			libnatpunch \
			libupnp \
			libp2engine \
			libmessage \
			\
			libprotobuf\
			\
			libboost_filesystem\
			libboost_date_time\
			libboost_system\
			libboost_program_options\
			libboost_thread\
			libboost_regex\
			\
			libssl\
			libcrypto\
			\
			libgnustl_static\
			
			
LOCAL_LDFLAGS += \
	
							
#find all c files in source dir
local_src_files := $(wildcard $(SRC_PATH_ROOT)/app/p2s_ppc/*.cpp)

#remove parent path of all c files in source dir
local_src_files := $(local_src_files:$(LOCAL_PATH)/%=%)

LOCAL_SRC_FILES := $(local_src_files)

include $(BUILD_EXECUTABLE)



