LOCAL_CPP_FEATURES += exceptions

LOCAL_C_INCLUDES += \
			$(APP_PROJECT_PATH)/../../src/lib 		\
			$(APP_PROJECT_PATH)/../../src/lib/p2engine 	\
			$(APP_PROJECT_PATH)/../../src/app 		\
			\
			$(BOOST_PATH)					\
			$(OPENSSL_PATH)/include				\
			$(PROTOBUF_PATH)/include			\
			$(ANDROID_NDK)/sources/cxx-stl/gnu-libstdc++/include\
			