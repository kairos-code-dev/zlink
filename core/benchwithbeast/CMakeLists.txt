# benchwithbeast - pure Asio/Beast stream benchmarks

if(NOT ZLINK_HAVE_ASIO)
  message(WARNING "ASIO headers not available, skipping benchwithbeast")
  return()
endif()

set(BENCH_BEAST_CXX_FLAGS "-O3")

add_executable(bench_beast_stream bench_beast_stream.cpp)
target_include_directories(bench_beast_stream PRIVATE ${BUNDLED_BOOST_INCLUDE_DIR})
target_compile_definitions(bench_beast_stream PRIVATE
  BOOST_ASIO_STANDALONE=1
  BOOST_ASIO_NO_DEPRECATED=1
  BOOST_ERROR_CODE_HEADER_ONLY=1)
target_compile_features(bench_beast_stream PRIVATE cxx_std_11)
target_compile_options(bench_beast_stream PRIVATE ${BENCH_BEAST_CXX_FLAGS})
target_link_libraries(bench_beast_stream PRIVATE Threads::Threads)

if(ZLINK_HAVE_ASIO_SSL)
  target_compile_definitions(bench_beast_stream PRIVATE BEAST_HAVE_TLS=1)
  target_link_libraries(bench_beast_stream PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()
