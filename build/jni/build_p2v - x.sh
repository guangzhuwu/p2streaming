export ANDROID_NDK="D:/android-ndk-r9d" 
export NDK_ROOT=$ANDROID_NDK

export PATH="$NDK_ROOT:$PATH_ORG" 

cd D:/project/p2v/trunk/build/jni
ndk-build clean
ndk-build -j4 HOST_AWK=gawk APP_ABI=armeabi APP_TOOLCHAIN_VERSION='4.4.3' NDK_PROJECT_PATH=../
