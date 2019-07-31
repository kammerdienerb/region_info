add_library(region_info MODULE region_info.cpp)
add_library(region_info_catch_dlclose SHARED region_info_catch_dlclose.c)

# Use C++11 to compile our pass (i.e., supply -std=c++11).
target_compile_features(region_info PRIVATE cxx_range_for cxx_auto_type)

# LLVM is (typically) built with no C++ RTTI. We need to match that;
# otherwise, we'll get linker errors about missing RTTI data.
set_target_properties(region_info PROPERTIES COMPILE_FLAGS "-fno-rtti")

# Get proper shared-library behavior (where symbols are not necessarily
# resolved when the shared library is linked) on OS X.
if(APPLE)
    set_target_properties(region_info PROPERTIES
        LINK_FLAGS "-undefined dynamic_lookup"
    )
endif(APPLE)
