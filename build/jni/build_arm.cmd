set PATH_ORG=%PATH%

set BOOST_PATH=D:/lib/boost
set OPENSSL_PATH=D:/lib/openssl

set PROTOBUF_PATH=D:/lib/protobuf
set JEMALLOC_PATH=D:/lib/jemalloc-3.0.0

set ANDROID_NDK=D:/dev/android-ndk/android-ndk-r8b
set NDK_ROOT=%ANDROID_NDK%
set PATH=%NDK_ROOT%;%PATH_ORG% 

cd D:/dev/p2v/trunk/build/jni/

call %NDK_ROOT%/ndk-build.cmd -j3 APP_ABI=armeabi TARGET_PLATFORM='android-14' NDK_PROJECT_PATH=./.. APP_PROJECT_PATH:=.

PAUSE




