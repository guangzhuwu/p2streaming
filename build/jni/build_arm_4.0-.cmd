set PATH_ORG=%PATH%

set BOOST_PATH=D:/lib/boost
set OPENSSL_PATH=D:/lib/openssl_android

set PROTOBUF_PATH=D:/lib/protobuf

set ANDROID_NDK=D:/dev/android-ndk/android-ndk-r9d
set NDK_ROOT=%ANDROID_NDK%
set PATH=%NDK_ROOT%;%PATH_ORG% 

set ROOT_JNI_PATH=D:/dev/p2v/trunk/build/jni/
cd %ROOT_JNI_PATH%;

call %NDK_ROOT%/ndk-build.cmd -j3 NDK_DEBUG=0 APP_ABI:=armeabi TARGET_PLATFORM='android-8' NDK_PROJECT_PATH=./.. APP_PROJECT_PATH:=.

PAUSE




