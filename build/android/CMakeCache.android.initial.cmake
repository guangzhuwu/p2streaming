########################
# Initial cache settings for opencv on android
# run cmake with:
# cmake -C 
########################

#Build shared libraries (.dll/.so CACHE BOOL "" ) instead of static ones (.lib/.a CACHE BOOL "" )
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" )

#Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel.
set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" )
