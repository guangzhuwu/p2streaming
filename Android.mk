####################################################################
#libs
####################################################################

#===================================================================
#client
#===================================================================
LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

include $(APP_PROJECT_PATH)/Include.mk

lib_name=client
lib_path=$(LOCAL_PATH)/src/lib/$(lib_name)
LOCAL_MODULE:= lib$(lib_name)

#find all c files in source dir
local_src_files := $(wildcard $(lib_path)/*.cpp) \
			$(wildcard $(lib_path)/cache/*.cpp) \
			$(wildcard $(lib_path)/hub/*.cpp) \
			$(wildcard $(lib_path)/stream/*.cpp) \
			$(wildcard $(lib_path)/stun/*.cpp) 

#remove parent path of all c files in source dir
local_src_files := $(local_src_files:$(LOCAL_PATH)/%=%)

LOCAL_SRC_FILES := $(local_src_files)

include $(BUILD_STATIC_LIBRARY)


#===================================================================
#common
#===================================================================
include $(CLEAR_VARS)

include $(APP_PROJECT_PATH)/Include.mk

LOCAL_MODULE:= libmessage

#find all c files in source dir
LOCAL_CPP_EXTENSION := .cc
local_src_files := $(wildcard $(LOCAL_PATH)/src/lib/common/*.cc) 

#remove parent path of all c files in source dir
local_src_files := $(local_src_files:$(LOCAL_PATH)/%=%)

LOCAL_SRC_FILES := $(local_src_files)

include $(BUILD_STATIC_LIBRARY)


#===================================================================
#libcommon
#===================================================================
include $(CLEAR_VARS)

include $(APP_PROJECT_PATH)/Include.mk

lib_name=common
lib_path=$(LOCAL_PATH)/src/lib/$(lib_name)
LOCAL_MODULE:= lib$(lib_name)

#find all c files in source dir
local_src_files := $(wildcard $(lib_name)/*.cpp) 

#remove parent path of all c files in source dir
local_src_files := $(local_src_files:$(LOCAL_PATH)/%=%)

LOCAL_SRC_FILES := $(local_src_files)

include $(BUILD_STATIC_LIBRARY)


#===================================================================
#libupnp
#===================================================================
include $(CLEAR_VARS)

LOCAL_MODULE:= libupnp

include $(APP_PROJECT_PATH)/Include.mk

#find all c files in source dir
local_src_files := $(wildcard $(LOCAL_PATH)/src/lib/libupnp/src/*.cpp) 

#remove parent path of all c files in source dir
local_src_files := $(local_src_files:$(LOCAL_PATH)/%=%)

LOCAL_SRC_FILES := $(local_src_files)

include $(BUILD_STATIC_LIBRARY)


#===================================================================
#natpunch
#===================================================================
include $(CLEAR_VARS)

LOCAL_MODULE:= libnatpunch

include $(APP_PROJECT_PATH)/Include.mk

#find all c files in source dir
local_src_files := $(wildcard $(LOCAL_PATH)/src/lib/natpunch/src/*.cpp) 

#remove parent path of all c files in source dir
local_src_files := $(local_src_files:$(LOCAL_PATH)/%=%)

LOCAL_SRC_FILES := $(local_src_files)

include $(BUILD_STATIC_LIBRARY)


#===================================================================
#p2engine
#===================================================================
include $(CLEAR_VARS)

LOCAL_MODULE:= libp2engine

include $(APP_PROJECT_PATH)/Include.mk

#find all c files in source dir
local_src_files := $(wildcard $(LOCAL_PATH)/src/lib/p2engine/src/*.cpp) \
                   $(wildcard $(LOCAL_PATH)/src/lib/p2engine/src/file/*.cpp) \
                   $(wildcard $(LOCAL_PATH)/src/lib/p2engine/src/http/*.cpp) \
                   $(wildcard $(LOCAL_PATH)/src/lib/p2engine/src/nedmalloc/*.cpp) \
                   $(wildcard $(LOCAL_PATH)/src/lib/p2engine/src/rdp/*.cpp) \

#remove parent path of all c files in source dir
local_src_files := $(local_src_files:$(LOCAL_PATH)/%=%)

LOCAL_SRC_FILES := $(local_src_files)

include $(BUILD_STATIC_LIBRARY)




####################################################################
#apps
####################################################################
