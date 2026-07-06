find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

set(WG_PROTO_SRC "${CMAKE_CURRENT_SOURCE_DIR}/proto/bench.proto")
set(WG_PROTO_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/proto_gen")
file(MAKE_DIRECTORY "${WG_PROTO_GEN_DIR}")

find_program(WG_PROTOC protoc REQUIRED)
find_program(WG_GRPC_CPP_PLUGIN grpc_cpp_plugin REQUIRED)
pkg_check_modules(WG_GRPCPP REQUIRED grpc++)
pkg_check_modules(WG_PROTOBUF REQUIRED protobuf)

set(WG_PROTO_CC "${WG_PROTO_GEN_DIR}/bench.pb.cc")
set(WG_PROTO_H "${WG_PROTO_GEN_DIR}/bench.pb.h")
set(WG_GRPC_CC "${WG_PROTO_GEN_DIR}/bench.grpc.pb.cc")
set(WG_GRPC_H "${WG_PROTO_GEN_DIR}/bench.grpc.pb.h")

add_custom_command(
    OUTPUT ${WG_PROTO_CC} ${WG_PROTO_H} ${WG_GRPC_CC} ${WG_GRPC_H}
    COMMAND ${WG_PROTOC}
        --cpp_out=${WG_PROTO_GEN_DIR}
        --grpc_out=${WG_PROTO_GEN_DIR}
        --plugin=protoc-gen-grpc=${WG_GRPC_CPP_PLUGIN}
        -I${CMAKE_CURRENT_SOURCE_DIR}/proto
        ${WG_PROTO_SRC}
    DEPENDS ${WG_PROTO_SRC}
    COMMENT "Generating bindings/c/bench/with_grpc protobuf sources")

set(WG_CXX_FLAGS)
if(NOT MSVC)
    list(APPEND WG_CXX_FLAGS -O3)
endif()

add_executable(bench_c_with_grpc_zlink_server zlink/bench_zlink_server.cpp)
target_link_libraries(bench_c_with_grpc_zlink_server PRIVATE zlink_c Threads::Threads)
target_include_directories(bench_c_with_grpc_zlink_server PRIVATE common)
target_compile_features(bench_c_with_grpc_zlink_server PRIVATE cxx_std_17)
target_compile_options(bench_c_with_grpc_zlink_server PRIVATE ${WG_CXX_FLAGS})
set_target_properties(bench_c_with_grpc_zlink_server PROPERTIES
    BUILD_RPATH "${ZLINK_C_CORE_RUNTIME_DIR}")

add_executable(bench_c_with_grpc_zlink_client zlink/bench_zlink_client.cpp)
target_link_libraries(bench_c_with_grpc_zlink_client PRIVATE zlink_c Threads::Threads)
target_include_directories(bench_c_with_grpc_zlink_client PRIVATE common)
target_compile_features(bench_c_with_grpc_zlink_client PRIVATE cxx_std_17)
target_compile_options(bench_c_with_grpc_zlink_client PRIVATE ${WG_CXX_FLAGS})
set_target_properties(bench_c_with_grpc_zlink_client PROPERTIES
    BUILD_RPATH "${ZLINK_C_CORE_RUNTIME_DIR}")

add_executable(bench_c_with_grpc_grpc_server
    grpc/bench_grpc_server.cpp
    ${WG_PROTO_CC}
    ${WG_GRPC_CC})
target_include_directories(bench_c_with_grpc_grpc_server PRIVATE
    common ${WG_PROTO_GEN_DIR} ${WG_GRPCPP_INCLUDE_DIRS} ${WG_PROTOBUF_INCLUDE_DIRS})
target_link_libraries(bench_c_with_grpc_grpc_server PRIVATE
    ${WG_GRPCPP_LIBRARIES} ${WG_PROTOBUF_LIBRARIES} Threads::Threads)
target_link_directories(bench_c_with_grpc_grpc_server PRIVATE
    ${WG_GRPCPP_LIBRARY_DIRS} ${WG_PROTOBUF_LIBRARY_DIRS})
target_compile_features(bench_c_with_grpc_grpc_server PRIVATE cxx_std_17)
target_compile_options(bench_c_with_grpc_grpc_server PRIVATE ${WG_CXX_FLAGS})

add_executable(bench_c_with_grpc_grpc_client
    grpc/bench_grpc_client.cpp
    ${WG_PROTO_CC}
    ${WG_GRPC_CC})
target_include_directories(bench_c_with_grpc_grpc_client PRIVATE
    common ${WG_PROTO_GEN_DIR} ${WG_GRPCPP_INCLUDE_DIRS} ${WG_PROTOBUF_INCLUDE_DIRS})
target_link_libraries(bench_c_with_grpc_grpc_client PRIVATE
    ${WG_GRPCPP_LIBRARIES} ${WG_PROTOBUF_LIBRARIES} Threads::Threads)
target_link_directories(bench_c_with_grpc_grpc_client PRIVATE
    ${WG_GRPCPP_LIBRARY_DIRS} ${WG_PROTOBUF_LIBRARY_DIRS})
target_compile_features(bench_c_with_grpc_grpc_client PRIVATE cxx_std_17)
target_compile_options(bench_c_with_grpc_grpc_client PRIVATE ${WG_CXX_FLAGS})
