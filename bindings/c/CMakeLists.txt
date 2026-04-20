add_library(zlink_c SHARED src/zlink_c.c)
target_include_directories(zlink_c PUBLIC include)
target_link_libraries(zlink_c PUBLIC libzlink)
set_target_properties(zlink_c PROPERTIES VERSION 1.0.0 SOVERSION 1)

option(ZLINK_C_BUILD_SAMPLES "Build C binding samples" OFF)
if(ZLINK_C_BUILD_SAMPLES)
  add_subdirectory(samples)
endif()

option(ZLINK_C_BUILD_BENCHMARKS "Build C binding benchmarks" OFF)
if(ZLINK_C_BUILD_BENCHMARKS)
  add_subdirectory(perf)
endif()

option(ZLINK_C_BUILD_BENCHES "Build C binding comparison benches" ${BUILD_BENCHMARKS})
if(ZLINK_C_BUILD_BENCHES
   OR ZLINK_BUILD_BENCH_ZMQ
   OR ZLINK_BUILD_BENCH_STREAMCOMPARE
   OR ZLINK_BUILD_BENCH_ROUTER_COMPARE)
  add_subdirectory(bench)
endif()
