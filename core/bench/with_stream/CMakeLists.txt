if(MSVC)
    set(STREAMCOMPARE_CXX_FLAGS "/O2")
else()
    set(STREAMCOMPARE_CXX_FLAGS "-O3")
endif()

add_executable(
    bench_streamcompare_client
    ../../perf/common/streamclient/perf_stream_client.cpp)

target_link_libraries(bench_streamcompare_client PRIVATE Threads::Threads)
target_link_libraries(
    bench_streamcompare_client PRIVATE
    OpenSSL::SSL
    OpenSSL::Crypto)
target_compile_options(bench_streamcompare_client PRIVATE ${STREAMCOMPARE_CXX_FLAGS})
target_compile_features(bench_streamcompare_client PRIVATE cxx_std_17)
