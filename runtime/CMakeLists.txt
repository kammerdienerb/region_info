add_library(region_info_rt SHARED region_info_rt.c)
add_compile_options("-fPIC" "-ftls-model=initial-exec")
