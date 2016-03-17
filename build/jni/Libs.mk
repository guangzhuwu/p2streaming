####################################################################
#libs
####################################################################

LOCAL_PATH:= $(call my-dir)
SRC_PATH_ROOT:=$(LOCAL_PATH)/../../src/lib

#===================================================================
#asfio
#===================================================================
include $(CLEAR_VARS)
include $(APP_PROJECT_PATH)/Include.mk

LOCAL_MODULE := libasfio

#find all c files in source dir
local_src_files := $(wildcard $(SRC_PATH_ROOT)/asfio/*.cpp) 

#remove parent path of all c files in source dir
local_src_files := $(local_src_files:$(LOCAL_PATH)/%=%)

LOCAL_SRC_FILES := $(local_src_files)

include $(BUILD_STATIC_LIBRARY)


#===================================================================
#client
#===================================================================
include $(CLEAR_VARS)
include $(APP_PROJECT_PATH)/Include.mk

lib_name := client
LOCAL_MODULE := lib$(lib_name)

#find all c files in source dir
local_src_files := $(wildcard $(SRC_PATH_ROOT)/client/*.cpp) \
			$(wildcard $(SRC_PATH_ROOT)/client/cache/*.cpp) \
			$(wildcard $(SRC_PATH_ROOT)/client/hub/*.cpp) \
			$(wildcard $(SRC_PATH_ROOT)/client/stream/*.cpp) \
			$(wildcard $(SRC_PATH_ROOT)/client/stun/*.cpp) 

#remove parent path of all c files in source dir
local_src_files := $(local_src_files:$(LOCAL_PATH)/%=%)

LOCAL_SRC_FILES := $(local_src_files)

include $(BUILD_STATIC_LIBRARY)


#===================================================================
#common
#===================================================================
#--------------
#libcommon
#--------------
include $(CLEAR_VARS)
include $(APP_PROJECT_PATH)/Include.mk

LOCAL_MODULE := libcommon

#find all c files in source dir
local_src_files := $(wildcard $(SRC_PATH_ROOT)/common/*.cpp) 

#remove parent path of all c files in source dir
local_src_files := $(local_src_files:$(LOCAL_PATH)/%=%)

LOCAL_SRC_FILES := $(local_src_files)

include $(BUILD_STATIC_LIBRARY)

#--------------
#libmessage
#--------------
include $(CLEAR_VARS)
include $(APP_PROJECT_PATH)/Include.mk

LOCAL_MODULE := libmessage

#find all c files in source dir
LOCAL_CPP_EXTENSION := .cc
local_src_files := $(wildcard $(SRC_PATH_ROOT)/common/*.cc) 

#remove parent path of all c files in source dir
local_src_files := $(local_src_files:$(LOCAL_PATH)/%=%)

LOCAL_SRC_FILES := $(local_src_files)

include $(BUILD_STATIC_LIBRARY)

#===================================================================
#httpdownload
#===================================================================
include $(CLEAR_VARS)
include $(APP_PROJECT_PATH)/Include.mk

LOCAL_MODULE:= libhttpdownload

#find all c files in source dir
local_src_files := $(wildcard $(SRC_PATH_ROOT)/httpdownload/*.cpp) 

#remove parent path of all c files in source dir
local_src_files := $(local_src_files:$(LOCAL_PATH)/%=%)

LOCAL_SRC_FILES := $(local_src_files)

include $(BUILD_STATIC_LIBRARY)

#===================================================================
#libupnp
#===================================================================
include $(CLEAR_VARS)
include $(APP_PROJECT_PATH)/Include.mk

LOCAL_MODULE:= libupnp

#find all c files in source dir
local_src_files := $(wildcard $(SRC_PATH_ROOT)/libupnp/src/*.cpp) 

#remove parent path of all c files in source dir
local_src_files := $(local_src_files:$(LOCAL_PATH)/%=%)

LOCAL_SRC_FILES := $(local_src_files)

include $(BUILD_STATIC_LIBRARY)


#===================================================================
#natpunch
#===================================================================
include $(CLEAR_VARS)
include $(APP_PROJECT_PATH)/Include.mk

LOCAL_MODULE:= libnatpunch

#find all c files in source dir
local_src_files := $(wildcard $(SRC_PATH_ROOT)/natpunch/src/*.cpp) 

#remove parent path of all c files in source dir
local_src_files := $(local_src_files:$(LOCAL_PATH)/%=%)

LOCAL_SRC_FILES := $(local_src_files)

include $(BUILD_STATIC_LIBRARY)


#===================================================================
#p2engine
#===================================================================
include $(CLEAR_VARS)
include $(APP_PROJECT_PATH)/Include.mk

LOCAL_MODULE:= libp2engine

#find all c files in source dir
local_src_files := $(wildcard $(SRC_PATH_ROOT)/p2engine/src/*.cpp) \
                   $(wildcard $(SRC_PATH_ROOT)/p2engine/src/file/*.cpp) \
                   $(wildcard $(SRC_PATH_ROOT)/p2engine/src/http/*.cpp) \
                   $(wildcard $(SRC_PATH_ROOT)/p2engine/src/nedmalloc/*.cpp) \
                   $(wildcard $(SRC_PATH_ROOT)/p2engine/src/rdp/*.cpp) \

#remove parent path of all c files in source dir
local_src_files := $(local_src_files:$(LOCAL_PATH)/%=%)

LOCAL_SRC_FILES := $(local_src_files)

include $(BUILD_STATIC_LIBRARY)


#===================================================================
#simple_server
#===================================================================
include $(CLEAR_VARS)
include $(APP_PROJECT_PATH)/Include.mk

LOCAL_MODULE:= libsimple_server

#find all c files in source dir
local_src_files := $(wildcard $(SRC_PATH_ROOT)/simple_server/*.cpp)
                 
#remove parent path of all c files in source dir
local_src_files := $(local_src_files:$(LOCAL_PATH)/%=%)

LOCAL_SRC_FILES := $(local_src_files)

include $(BUILD_STATIC_LIBRARY)

#===================================================================
#urlcrack
#===================================================================
include $(CLEAR_VARS)
include $(APP_PROJECT_PATH)/Include.mk

LOCAL_MODULE:= liburlcrack

#find all c files in source dir
local_src_files := $(wildcard $(SRC_PATH_ROOT)/urlcrack/*.cpp)
                 
#remove parent path of all c files in source dir
local_src_files := $(local_src_files:$(LOCAL_PATH)/%=%)

LOCAL_SRC_FILES := $(local_src_files)

include $(BUILD_STATIC_LIBRARY)