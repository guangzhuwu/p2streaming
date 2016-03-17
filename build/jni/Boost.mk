####################################################################
#boost libs
####################################################################

#===================================================================
#filesystem
#===================================================================
include $(CLEAR_VARS)

libname:=filesystem
LOCAL_PATH:= $(BOOST_PATH)
SRC_PATH_ROOT:=$(LOCAL_PATH)/libs
LOCAL_MODULE:= libboost_$(libname)

include $(APP_PROJECT_PATH)/Include.mk

#find all c files in source dir
local_src_files := $(wildcard $(SRC_PATH_ROOT)/$(libname)/src/*.cpp) 

#remove parent path of all c files in source dir
local_src_files := $(local_src_files:$(LOCAL_PATH)/%=%)

LOCAL_SRC_FILES := $(local_src_files)

include $(BUILD_STATIC_LIBRARY)

#===================================================================
#date_time
#===================================================================
include $(CLEAR_VARS)

libname:=date_time
LOCAL_PATH:= $(BOOST_PATH)
SRC_PATH_ROOT:=$(LOCAL_PATH)/libs
LOCAL_MODULE:= libboost_$(libname)

include $(APP_PROJECT_PATH)/Include.mk

#find all c files in source dir
local_src_files := $(wildcard $(SRC_PATH_ROOT)/$(libname)/src/gregorian/*.cpp) \
			$(wildcard $(SRC_PATH_ROOT)/$(libname)/src/posix_time/*.cpp) 

#remove parent path of all c files in source dir
local_src_files := $(local_src_files:$(LOCAL_PATH)/%=%)

LOCAL_SRC_FILES := $(local_src_files)

include $(BUILD_STATIC_LIBRARY)

#===================================================================
#program_options
#===================================================================
include $(CLEAR_VARS)

libname:=program_options
LOCAL_PATH:= $(BOOST_PATH)
SRC_PATH_ROOT:=$(LOCAL_PATH)/libs
LOCAL_MODULE:= libboost_$(libname)

include $(APP_PROJECT_PATH)/Include.mk

#find all c files in source dir
local_src_files := $(wildcard $(SRC_PATH_ROOT)/$(libname)/src/*.cpp)

#remove parent path of all c files in source dir
local_src_files := $(local_src_files:$(LOCAL_PATH)/%=%)

LOCAL_SRC_FILES := $(local_src_files)

include $(BUILD_STATIC_LIBRARY)


#===================================================================
#system
#===================================================================
include $(CLEAR_VARS)

libname:=system
LOCAL_PATH:= $(BOOST_PATH)
SRC_PATH_ROOT:=$(LOCAL_PATH)/libs
LOCAL_MODULE:= libboost_$(libname)

include $(APP_PROJECT_PATH)/Include.mk

#find all c files in source dir
local_src_files := $(wildcard $(SRC_PATH_ROOT)/$(libname)/src/*.cpp)

#remove parent path of all c files in source dir
local_src_files := $(local_src_files:$(LOCAL_PATH)/%=%)

LOCAL_SRC_FILES := $(local_src_files)

include $(BUILD_STATIC_LIBRARY)

#===================================================================
#thread
#===================================================================
include $(CLEAR_VARS)

libname:=thread
LOCAL_PATH:= $(BOOST_PATH)
SRC_PATH_ROOT:=$(LOCAL_PATH)/libs
LOCAL_MODULE:= libboost_$(libname)

include $(APP_PROJECT_PATH)/Include.mk

#find all c files in source dir
local_src_files := $(wildcard $(SRC_PATH_ROOT)/$(libname)/src/*.cpp)\
			$(wildcard $(SRC_PATH_ROOT)/$(libname)/src/pthread/*.cpp)

#remove parent path of all c files in source dir
local_src_files := $(local_src_files:$(LOCAL_PATH)/%=%)

LOCAL_SRC_FILES := $(local_src_files)

include $(BUILD_STATIC_LIBRARY)


#===================================================================
#regex
#===================================================================
include $(CLEAR_VARS)

libname:=regex
LOCAL_PATH:= $(BOOST_PATH)
SRC_PATH_ROOT:=$(LOCAL_PATH)/libs
LOCAL_MODULE:= libboost_$(libname)

include $(APP_PROJECT_PATH)/Include.mk

#find all c files in source dir
local_src_files := $(wildcard $(SRC_PATH_ROOT)/$(libname)/src/*.cpp)

#remove parent path of all c files in source dir
local_src_files := $(local_src_files:$(LOCAL_PATH)/%=%)

LOCAL_SRC_FILES := $(local_src_files)

include $(BUILD_STATIC_LIBRARY)
