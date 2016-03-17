APP_PROJECT_PATH := $(shell pwd)

APP_CPPFLAGS += -fexceptions
APP_CPPFLAGS += -frtti

APP_STL := gnustl_static

APP_CFLAGS+=-DANDROID=1\
		-D_GLIBCXX_USE_WCHAR_T=1

APP_BUILD_SCRIPT := $(APP_PROJECT_PATH)/Android.mk
