# bindings/c/perf - current zlink C binding benchmark targets

if(NOT TARGET zlink_c)
    message(FATAL_ERROR "C binding target not found. Build bindings/c first.")
endif()

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Python3 COMPONENTS Interpreter)

get_filename_component(ZLINK_C_BINDINGS_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
get_filename_component(ZLINK_REPO_ROOT_DIR "${ZLINK_C_BINDINGS_DIR}/../.." ABSOLUTE)

set(PERF_BINDINGS_C_INCLUDE_DIR "${ZLINK_C_BINDINGS_DIR}/include")

set(PERF_EXTERNAL_INCLUDE_DIR "${ZLINK_REPO_ROOT_DIR}/core/external")
if(NOT EXISTS "${PERF_EXTERNAL_INCLUDE_DIR}")
    set(PERF_EXTERNAL_INCLUDE_DIR "${ZLINK_REPO_ROOT_DIR}/external")
endif()
set(PERF_BENCH_SINGLE_COMMON_DIR
    "${ZLINK_C_BINDINGS_DIR}/bench/with_zmq/single/common")
set(PERF_BENCH_MULTI_COMMON_DIR
    "${ZLINK_C_BINDINGS_DIR}/bench/with_zmq/multi/common")

if(MSVC)
    set(PERF_CXX_FLAGS "/O2")
else()
    set(PERF_CXX_FLAGS "-O3")
endif()

function(configure_current_bench_runtime target_name)
    if(WIN32)
        target_link_libraries(${target_name} PRIVATE Psapi)
    endif()
endfunction()

function(add_current_bench_single name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE zlink_c Threads::Threads)
    if(NOT WIN32)
        target_link_libraries(${name} PRIVATE dl)
    endif()
    target_include_directories(${name} PRIVATE
        ${PERF_BINDINGS_C_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/single/common
        ${PERF_EXTERNAL_INCLUDE_DIR})
    target_compile_options(${name} PRIVATE ${PERF_CXX_FLAGS})
    target_compile_features(${name} PRIVATE cxx_std_11)
    configure_current_bench_runtime(${name})
endfunction()

function(add_current_bench_single_recv name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE zlink_c Threads::Threads)
    if(NOT WIN32)
        target_link_libraries(${name} PRIVATE dl)
    endif()
    target_include_directories(${name} PRIVATE
        ${PERF_BINDINGS_C_INCLUDE_DIR}
        ${PERF_BENCH_SINGLE_COMMON_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/single/common
        ${PERF_EXTERNAL_INCLUDE_DIR})
    target_compile_options(${name} PRIVATE ${PERF_CXX_FLAGS})
    target_compile_features(${name} PRIVATE cxx_std_11)
    configure_current_bench_runtime(${name})
endfunction()

function(add_current_bench_multi name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE zlink_c Threads::Threads)
    if(NOT WIN32)
        target_link_libraries(${name} PRIVATE dl)
    endif()
    target_include_directories(${name} PRIVATE
        ${PERF_BINDINGS_C_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/multi/common
        ${PERF_BENCH_MULTI_COMMON_DIR})
    target_compile_options(${name} PRIVATE ${PERF_CXX_FLAGS})
    target_compile_features(${name} PRIVATE cxx_std_11)
    configure_current_bench_runtime(${name})
endfunction()

    add_current_bench_single(perf_pair single/src/perf_pair.cpp)
    add_current_bench_single(perf_pubsub single/src/perf_pubsub.cpp)
    add_current_bench_single(perf_dealer_dealer single/src/perf_dealer_dealer.cpp)
    add_current_bench_single(perf_dealer_router single/src/perf_dealer_router.cpp)
    add_current_bench_single(perf_router_router single/src/perf_router_router.cpp)
    add_current_bench_single(perf_spot single/src/perf_spot.cpp)

    add_executable(
        perf_stream_client
        common/streamclient/perf_stream_client.cpp)
    target_link_libraries(
        perf_stream_client PRIVATE
        Threads::Threads
        OpenSSL::SSL
        OpenSSL::Crypto)
    target_include_directories(
        perf_stream_client PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/common/streamclient
        ${PERF_EXTERNAL_INCLUDE_DIR}/boost)
    target_compile_options(perf_stream_client PRIVATE ${PERF_CXX_FLAGS})
    target_compile_features(perf_stream_client PRIVATE cxx_std_17)

    add_current_bench_multi(comp_src_dealer_dealer_server
                           multi/src/perf_multi_dealer_dealer_server.cpp)
    add_current_bench_multi(comp_src_dealer_router_server
                           multi/src/perf_multi_dealer_router_server.cpp)
    add_current_bench_multi(comp_src_router_router_server
                           multi/src/perf_multi_router_router_server.cpp)
    add_current_bench_multi(comp_src_pubsub_server
                           multi/src/perf_multi_pubsub_server.cpp)
    add_current_bench_multi(comp_src_spot_server
                           multi/src/perf_multi_spot_server.cpp)
    add_current_bench_multi(comp_src_spot_reqrep_server
                           multi/src/perf_multi_spot_reqrep_server.cpp)
    add_current_bench_multi(comp_src_spot_sendsend_server
                           multi/src/perf_multi_spot_sendsend_server.cpp)
    add_current_bench_multi(comp_src_dealer_dealer_client
                           multi/src/perf_multi_dealer_dealer_client.cpp)
    add_current_bench_multi(comp_src_dealer_router_client
                           multi/src/perf_multi_dealer_router_client.cpp)
    add_current_bench_multi(comp_src_router_router_client
                           multi/src/perf_multi_router_router_client.cpp)
    add_current_bench_multi(comp_src_pubsub_client
                           multi/src/perf_multi_pubsub_client.cpp)
    add_current_bench_multi(comp_src_spot_client
                           multi/src/perf_multi_spot_client.cpp)
    add_current_bench_multi(comp_src_spot_reqrep_client
                           multi/src/perf_multi_spot_reqrep_client.cpp)
    add_current_bench_multi(comp_src_spot_sendsend_client
                           multi/src/perf_multi_spot_sendsend_client.cpp)
    add_current_bench_multi(comp_src_stream_server
                           multi/src/perf_multi_stream_server.cpp)
target_compile_definitions(comp_src_stream_server PRIVATE
                           PERF_MULTI_STREAM_PATTERN_NAME="MULTI_STREAM")

if(ZLINK_C_REGISTER_CTEST AND Python3_Interpreter_FOUND AND UNIX AND NOT APPLE)
    function(add_perf_multi_spot_size_sweep_regression test_name pattern_name)
        add_test(
            NAME ${test_name}
            COMMAND ${CMAKE_COMMAND} -E env
                "PERF_ALLOW_MULTI=1"
                "PERF_FAIL_FAST=1"
                "PERF_TRANSPORTS=tcp"
                "PERF_MSG_SIZES=64,256,1024,65536,131072"
                "PERF_DURATION_SECONDS=5"
                "PERF_CTX_AUTO_HWM_ENABLE=1"
                "LD_LIBRARY_PATH=${ZLINK_C_CORE_RUNTIME_DIR}:$ENV{LD_LIBRARY_PATH}"
                ${Python3_EXECUTABLE}
                "${CMAKE_CURRENT_SOURCE_DIR}/run_comparison.py"
                "${pattern_name}"
                "--build-dir" "${CMAKE_BINARY_DIR}"
                "--result-file" "${CMAKE_CURRENT_BINARY_DIR}/${test_name}.txt")
        set_tests_properties(${test_name} PROPERTIES
            LABELS "perf;regression"
            TIMEOUT 90)
    endfunction()

    add_perf_multi_spot_size_sweep_regression(
        perf_multi_spot_reqrep_size_sweep_regression SPOT_REQREP)
    add_perf_multi_spot_size_sweep_regression(
        perf_multi_spot_sendsend_size_sweep_regression SPOT_SENDSEND)
    add_test(
        NAME perf_multi_spot_clean_latency_size_regression
        COMMAND ${CMAKE_COMMAND} -E env
            "PERF_ALLOW_MULTI=1"
            "PERF_FAIL_FAST=1"
            "PERF_TRANSPORTS=tcp"
            "PERF_MSG_SIZES=64,256,1024"
            "PERF_DURATION_SECONDS=5"
            "PERF_CTX_AUTO_HWM_ENABLE=1"
            "LD_LIBRARY_PATH=${ZLINK_C_CORE_RUNTIME_DIR}:$ENV{LD_LIBRARY_PATH}"
            ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/run_comparison.py"
            "SPOT"
            "--build-dir" "${CMAKE_BINARY_DIR}"
            "--result-file"
            "${CMAKE_CURRENT_BINARY_DIR}/perf_multi_spot_clean_latency_size_regression.txt")
    set_tests_properties(
        perf_multi_spot_clean_latency_size_regression PROPERTIES
        LABELS "perf;regression"
        TIMEOUT 75)
    add_test(
        NAME perf_multi_spot_reqrep_sendsend_transition_regression
        COMMAND ${CMAKE_COMMAND} -E env
            "PERF_ALLOW_MULTI=1"
            "PERF_FAIL_FAST=1"
            "PERF_TRANSPORTS=tcp"
            "PERF_MSG_SIZES=64,256,1024"
            "PERF_DURATION_SECONDS=5"
            "PERF_CTX_AUTO_HWM_ENABLE=1"
            "LD_LIBRARY_PATH=${ZLINK_C_CORE_RUNTIME_DIR}:$ENV{LD_LIBRARY_PATH}"
            ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/run_comparison.py"
            "SPOT_REQREP,SPOT_SENDSEND"
            "--build-dir" "${CMAKE_BINARY_DIR}"
            "--result-file"
            "${CMAKE_CURRENT_BINARY_DIR}/perf_multi_spot_reqrep_sendsend_transition_regression.txt")
    set_tests_properties(
        perf_multi_spot_reqrep_sendsend_transition_regression PROPERTIES
        LABELS "perf;regression"
        TIMEOUT 75)
endif()
