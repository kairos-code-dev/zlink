ACLOCAL_AMFLAGS = -I config

SUBDIRS = doc

DIST_SUBDIRS = doc builds builds/deprecated-msvc

pkgconfig_DATA = src/libzmq.pc

AM_CPPFLAGS = \
	-I$(top_builddir)/include \
	-I$(top_srcdir)/include

#
# libraries/binaries
#
lib_LTLIBRARIES = src/libzmq.la

include_HEADERS = \
	include/zmq.h \
	include/zmq_utils.h

src_libzmq_la_SOURCES = \
	src/address.cpp \
	src/address.hpp \
	src/array.hpp \
	src/asio/asio_debug.hpp \
	src/asio/asio_engine.cpp \
	src/asio/asio_engine.hpp \
	src/asio/asio_error_handler.hpp \
	src/asio/asio_ipc_connecter.cpp \
	src/asio/asio_ipc_connecter.hpp \
	src/asio/asio_ipc_listener.cpp \
	src/asio/asio_ipc_listener.hpp \
	src/asio/asio_poller.cpp \
	src/asio/asio_poller.hpp \
	src/asio/asio_tcp_connecter.cpp \
	src/asio/asio_tcp_connecter.hpp \
	src/asio/asio_tcp_listener.cpp \
	src/asio/asio_tcp_listener.hpp \
	src/asio/asio_tls_connecter.cpp \
	src/asio/asio_tls_connecter.hpp \
	src/asio/asio_tls_listener.cpp \
	src/asio/asio_tls_listener.hpp \
	src/asio/asio_ws_connecter.cpp \
	src/asio/asio_ws_connecter.hpp \
	src/asio/asio_ws_engine.cpp \
	src/asio/asio_ws_engine.hpp \
	src/asio/asio_ws_listener.cpp \
	src/asio/asio_ws_listener.hpp \
	src/asio/asio_zmtp_engine.cpp \
	src/asio/asio_zmtp_engine.hpp \
	src/asio/i_asio_transport.hpp \
	src/asio/ssl_context_helper.cpp \
	src/asio/ssl_context_helper.hpp \
	src/asio/ssl_transport.cpp \
	src/asio/ssl_transport.hpp \
	src/asio/tcp_transport.cpp \
	src/asio/tcp_transport.hpp \
	src/asio/ws_transport.cpp \
	src/asio/ws_transport.hpp \
	src/asio/wss_transport.cpp \
	src/asio/wss_transport.hpp \
	src/atomic_counter.hpp \
	src/atomic_ptr.hpp \
	src/blob.hpp \
	src/clock.cpp \
	src/clock.hpp \
	src/command.hpp \
	src/compat.hpp \
	src/condition_variable.hpp \
	src/config.hpp \
	src/ctx.cpp \
	src/ctx.hpp \
	src/dbuffer.hpp \
	src/dealer.cpp \
	src/dealer.hpp \
	src/decoder.hpp \
	src/decoder_allocators.cpp \
	src/decoder_allocators.hpp \
	src/dist.cpp \
	src/dist.hpp \
	src/encoder.hpp \
	src/endpoint.cpp \
	src/endpoint.hpp \
	src/err.cpp \
	src/err.hpp \
	src/fd.hpp \
	src/fq.cpp \
	src/fq.hpp \
	src/generic_mtrie.hpp \
	src/generic_mtrie_impl.hpp \
	src/gssapi_client.cpp \
	src/gssapi_client.hpp \
	src/gssapi_mechanism_base.cpp \
	src/gssapi_mechanism_base.hpp \
	src/gssapi_server.cpp \
	src/gssapi_server.hpp \
	src/i_decoder.hpp \
	src/i_encoder.hpp \
	src/i_engine.hpp \
	src/i_mailbox.hpp \
	src/i_poll_events.hpp \
	src/io_object.cpp \
	src/io_object.hpp \
	src/io_thread.cpp \
	src/io_thread.hpp \
	src/ip.cpp \
	src/ip.hpp \
	src/ip_resolver.cpp \
	src/ip_resolver.hpp \
	src/ipc_address.cpp \
	src/ipc_address.hpp \
	src/lb.cpp \
	src/lb.hpp \
	src/likely.hpp \
	src/macros.hpp \
	src/mailbox.cpp \
	src/mailbox.hpp \
	src/mailbox_safe.cpp \
	src/mailbox_safe.hpp \
	src/mechanism.cpp \
	src/mechanism.hpp \
	src/mechanism_base.cpp \
	src/mechanism_base.hpp \
	src/metadata.cpp \
	src/metadata.hpp \
	src/msg.cpp \
	src/msg.hpp \
	src/mtrie.cpp \
	src/mtrie.hpp \
	src/mutex.hpp \
	src/null_mechanism.cpp \
	src/null_mechanism.hpp \
	src/object.cpp \
	src/object.hpp \
	src/options.cpp \
	src/options.hpp \
	src/own.cpp \
	src/own.hpp \
	src/pair.cpp \
	src/pair.hpp \
	src/pipe.cpp \
	src/pipe.hpp \
	src/plain_client.cpp \
	src/plain_client.hpp \
	src/plain_common.hpp \
	src/plain_server.cpp \
	src/plain_server.hpp \
	src/poller.hpp \
	src/poller_base.cpp \
	src/poller_base.hpp \
	src/polling_util.cpp \
	src/polling_util.hpp \
	src/precompiled.cpp \
	src/precompiled.hpp \
	src/proxy.cpp \
	src/proxy.hpp \
	src/pub.cpp \
	src/pub.hpp \
	src/radix_tree.cpp \
	src/radix_tree.hpp \
	src/random.cpp \
	src/random.hpp \
	src/reaper.cpp \
	src/reaper.hpp \
	src/router.cpp \
	src/router.hpp \
	src/secure_allocator.hpp \
	src/session_base.cpp \
	src/session_base.hpp \
	src/signaler.cpp \
	src/signaler.hpp \
	src/socket_base.cpp \
	src/socket_base.hpp \
	src/socket_poller.cpp \
	src/socket_poller.hpp \
	src/stdint.hpp \
	src/sub.cpp \
	src/sub.hpp \
	src/tcp.cpp \
	src/tcp.hpp \
	src/tcp_address.cpp \
	src/tcp_address.hpp \
	src/thread.cpp \
	src/thread.hpp \
	src/timers.cpp \
	src/timers.hpp \
	src/trie.cpp \
	src/trie.hpp \
	src/v1_decoder.cpp \
	src/v1_decoder.hpp \
	src/v1_encoder.cpp \
	src/v1_encoder.hpp \
	src/v2_decoder.cpp \
	src/v2_decoder.hpp \
	src/v2_encoder.cpp \
	src/v2_encoder.hpp \
	src/v2_protocol.hpp \
	src/v3_1_encoder.cpp \
	src/v3_1_encoder.hpp \
	src/windows.hpp \
	src/wire.hpp \
	src/ws_address.cpp \
	src/ws_address.hpp \
	src/wss_address.cpp \
	src/wss_address.hpp \
	src/xpub.cpp \
	src/xpub.hpp \
	src/xsub.cpp \
	src/xsub.hpp \
	src/ypipe.hpp \
	src/ypipe_base.hpp \
	src/ypipe_conflate.hpp \
	src/yqueue.hpp \
	src/zap_client.cpp \
	src/zap_client.hpp \
	src/zmq.cpp \
	src/zmq_draft.h \
	src/zmq_utils.cpp

if USE_BUILTIN_SHA1
src_libzmq_la_SOURCES += \
	external/sha1/sha1.c \
	external/sha1/sha1.h
endif

if ON_MINGW
src_libzmq_la_LDFLAGS = \
	-no-undefined \
	-avoid-version \
	-version-info @LTVER@ \
	@LIBZMQ_EXTRA_LDFLAGS@
else
if ON_CYGWIN
src_libzmq_la_LDFLAGS = \
	-no-undefined \
	-avoid-version \
	-version-info @LTVER@ \
	@LIBZMQ_EXTRA_LDFLAGS@
else
if ON_ANDROID
src_libzmq_la_LDFLAGS = \
	-avoid-version \
	-version-info @LTVER@ \
	@LIBZMQ_EXTRA_LDFLAGS@
else
src_libzmq_la_LDFLAGS = \
	-version-info @LTVER@ \
	@LIBZMQ_EXTRA_LDFLAGS@
endif
endif
endif

if HAVE_VSCRIPT_COMPLEX
src_libzmq_la_LDFLAGS += $(VSCRIPT_LDFLAGS),$(srcdir)/src/libzmq.vers
endif

src_libzmq_la_CPPFLAGS = $(CODE_COVERAGE_CPPFLAGS) $(LIBUNWIND_CFLAGS) $(LIBBSD_CFLAGS)
src_libzmq_la_CFLAGS = $(CODE_COVERAGE_CFLAGS) $(LIBUNWIND_CFLAGS) $(LIBBSD_CFLAGS)
src_libzmq_la_CXXFLAGS = @LIBZMQ_EXTRA_CXXFLAGS@ $(CODE_COVERAGE_CXXFLAGS) \
	$(LIBUNWIND_CFLAGS) $(LIBBSD_CFLAGS)
src_libzmq_la_LIBADD = $(CODE_COVERAGE_LDFLAGS) $(LIBUNWIND_LIBS) $(LIBBSD_LIBS)


if USE_LIBSODIUM
src_libzmq_la_CPPFLAGS += ${sodium_CFLAGS}
src_libzmq_la_LIBADD += ${sodium_LIBS}
endif


if BUILD_GSSAPI
src_libzmq_la_CPPFLAGS += ${gssapi_krb5_CFLAGS}
src_libzmq_la_LIBADD += ${gssapi_krb5_LIBS}
endif

if ENABLE_PERF
noinst_PROGRAMS = \
	perf/local_lat \
	perf/remote_lat \
	perf/local_thr \
	perf/remote_thr \
	perf/inproc_lat \
	perf/inproc_thr \
	perf/proxy_thr

perf_local_lat_LDADD = src/libzmq.la
perf_local_lat_SOURCES = perf/local_lat.cpp

perf_remote_lat_LDADD = src/libzmq.la
perf_remote_lat_SOURCES = perf/remote_lat.cpp

perf_local_thr_LDADD = src/libzmq.la
perf_local_thr_SOURCES = perf/local_thr.cpp

perf_remote_thr_LDADD = src/libzmq.la
perf_remote_thr_SOURCES = perf/remote_thr.cpp

perf_inproc_lat_LDADD = src/libzmq.la
perf_inproc_lat_SOURCES = perf/inproc_lat.cpp

perf_inproc_thr_LDADD = src/libzmq.la
perf_inproc_thr_SOURCES = perf/inproc_thr.cpp

perf_proxy_thr_LDADD = src/libzmq.la
perf_proxy_thr_SOURCES = perf/proxy_thr.cpp

if ENABLE_STATIC
noinst_PROGRAMS += \
	perf/benchmark_radix_tree

perf_benchmark_radix_tree_DEPENDENCIES = src/libzmq.la
perf_benchmark_radix_tree_CPPFLAGS = -I$(top_srcdir)/src
perf_benchmark_radix_tree_LDADD = $(top_builddir)/src/.libs/libzmq.a \
	${src_libzmq_la_LIBADD}
perf_benchmark_radix_tree_SOURCES = perf/benchmark_radix_tree.cpp
endif
endif

if ENABLE_CURVE_KEYGEN
bin_PROGRAMS = tools/curve_keygen

tools_curve_keygen_LDADD = src/libzmq.la
tools_curve_keygen_SOURCES = tools/curve_keygen.cpp
endif

#
# tests
#
test_apps = \
	tests/test_abstract_ipc \
	tests/test_ancillaries \
	tests/test_asio_connect \
	tests/test_asio_poller \
	tests/test_asio_ssl \
	tests/test_asio_tcp \
	tests/test_asio_ws \
	tests/test_bind_after_connect_tcp \
	tests/test_bind_src_address \
	tests/test_capabilities \
	tests/test_connect_resolve \
	tests/test_connect_rid \
	tests/test_ctx_destroy \
	tests/test_ctx_options \
	tests/test_diffserv \
	tests/test_disconnect_inproc \
	tests/test_getsockopt_memset \
	tests/test_heartbeats \
	tests/test_hwm_pubsub \
	tests/test_immediate \
	tests/test_ipc_wildcard \
	tests/test_issue_566 \
	tests/test_last_endpoint \
	tests/test_many_sockets \
	tests/test_mock_pub_sub \
	tests/test_msg_ffn \
	tests/test_msg_flags \
	tests/test_pair_inproc \
	tests/test_pair_ipc \
	tests/test_pair_tcp \
	tests/test_pair_transports \
	tests/test_probe_router \
	tests/test_proxy \
	tests/test_pub_invert_matching \
	tests/test_pubsub \
	tests/test_pubsub_transports \
	tests/test_reconnect_ivl \
	tests/test_reconnect_options \
	tests/test_router_handover \
	tests/test_router_mandatory \
	tests/test_router_mandatory_hwm \
	tests/test_router_transports \
	tests/test_shutdown_stress \
	tests/test_socket_null \
	tests/test_spec_router \
	tests/test_sub_forward \
	tests/test_system \
	tests/test_timeo \
	tests/test_use_fd \
	tests/test_xpub_manual \
	tests/test_xpub_nodrop \
	tests/test_xpub_topic \
	tests/test_xpub_verbose \
	tests/test_xpub_welcome_msg
UNITY_CPPFLAGS = -I$(top_srcdir)/external/unity -DUNITY_USE_COMMAND_LINE_ARGS -DUNITY_EXCLUDE_FLOAT
UNITY_LIBS = $(top_builddir)/external/unity/libunity.a
external_unity_libunity_a_SOURCES = external/unity/unity.c \
	external/unity/unity.h \
	external/unity/unity_internals.h

TESTUTIL_CPPFLAGS = ${UNITY_CPPFLAGS}
TESTUTIL_LIBS = $(top_builddir)/tests/libtestutil.a ${UNITY_LIBS}
tests_libtestutil_a_SOURCES = \
        tests/testutil.cpp \
        tests/testutil.hpp \
        tests/testutil_monitoring.cpp \
        tests/testutil_monitoring.hpp \
        tests/testutil_security.cpp \
        tests/testutil_security.hpp \
        tests/testutil_unity.cpp \
        tests/testutil_unity.hpp
tests_libtestutil_a_CPPFLAGS = ${UNITY_CPPFLAGS}

noinst_LIBRARIES = external/unity/libunity.a tests/libtestutil.a

tests_test_abstract_ipc_SOURCES = tests/test_abstract_ipc.cpp
tests_test_abstract_ipc_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_abstract_ipc_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_ancillaries_SOURCES = tests/test_ancillaries.cpp
tests_test_ancillaries_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_ancillaries_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_asio_connect_SOURCES = tests/test_asio_connect.cpp
tests_test_asio_connect_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_asio_connect_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_asio_poller_SOURCES = tests/test_asio_poller.cpp
tests_test_asio_poller_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_asio_poller_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_asio_ssl_SOURCES = tests/test_asio_ssl.cpp
tests_test_asio_ssl_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_asio_ssl_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_asio_tcp_SOURCES = tests/test_asio_tcp.cpp
tests_test_asio_tcp_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_asio_tcp_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_asio_ws_SOURCES = tests/test_asio_ws.cpp
tests_test_asio_ws_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_asio_ws_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_bind_after_connect_tcp_SOURCES = tests/test_bind_after_connect_tcp.cpp
tests_test_bind_after_connect_tcp_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_bind_after_connect_tcp_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_bind_src_address_SOURCES = tests/test_bind_src_address.cpp
tests_test_bind_src_address_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_bind_src_address_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_capabilities_SOURCES = tests/test_capabilities.cpp
tests_test_capabilities_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_capabilities_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_connect_resolve_SOURCES = tests/test_connect_resolve.cpp
tests_test_connect_resolve_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_connect_resolve_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_connect_rid_SOURCES = tests/test_connect_rid.cpp
tests_test_connect_rid_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_connect_rid_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_ctx_destroy_SOURCES = tests/test_ctx_destroy.cpp
tests_test_ctx_destroy_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_ctx_destroy_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_ctx_options_SOURCES = tests/test_ctx_options.cpp
tests_test_ctx_options_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_ctx_options_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_diffserv_SOURCES = tests/test_diffserv.cpp
tests_test_diffserv_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_diffserv_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_disconnect_inproc_SOURCES = tests/test_disconnect_inproc.cpp
tests_test_disconnect_inproc_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_disconnect_inproc_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_getsockopt_memset_SOURCES = tests/test_getsockopt_memset.cpp
tests_test_getsockopt_memset_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_getsockopt_memset_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_heartbeats_SOURCES = tests/test_heartbeats.cpp
tests_test_heartbeats_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_heartbeats_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_hwm_pubsub_SOURCES = tests/test_hwm_pubsub.cpp
tests_test_hwm_pubsub_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_hwm_pubsub_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_immediate_SOURCES = tests/test_immediate.cpp
tests_test_immediate_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_immediate_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_ipc_wildcard_SOURCES = tests/test_ipc_wildcard.cpp
tests_test_ipc_wildcard_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_ipc_wildcard_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_issue_566_SOURCES = tests/test_issue_566.cpp
tests_test_issue_566_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_issue_566_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_last_endpoint_SOURCES = tests/test_last_endpoint.cpp
tests_test_last_endpoint_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_last_endpoint_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_many_sockets_SOURCES = tests/test_many_sockets.cpp
tests_test_many_sockets_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_many_sockets_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_mock_pub_sub_SOURCES = tests/test_mock_pub_sub.cpp
tests_test_mock_pub_sub_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_mock_pub_sub_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_msg_ffn_SOURCES = tests/test_msg_ffn.cpp
tests_test_msg_ffn_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_msg_ffn_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_msg_flags_SOURCES = tests/test_msg_flags.cpp
tests_test_msg_flags_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_msg_flags_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_pair_inproc_SOURCES = tests/test_pair_inproc.cpp
tests_test_pair_inproc_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_pair_inproc_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_pair_ipc_SOURCES = tests/test_pair_ipc.cpp
tests_test_pair_ipc_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_pair_ipc_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_pair_tcp_SOURCES = tests/test_pair_tcp.cpp
tests_test_pair_tcp_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_pair_tcp_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_pair_transports_SOURCES = tests/test_pair_transports.cpp
tests_test_pair_transports_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_pair_transports_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_probe_router_SOURCES = tests/test_probe_router.cpp
tests_test_probe_router_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_probe_router_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_proxy_SOURCES = tests/test_proxy.cpp
tests_test_proxy_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_proxy_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_pub_invert_matching_SOURCES = tests/test_pub_invert_matching.cpp
tests_test_pub_invert_matching_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_pub_invert_matching_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_pubsub_SOURCES = tests/test_pubsub.cpp
tests_test_pubsub_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_pubsub_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_pubsub_transports_SOURCES = tests/test_pubsub_transports.cpp
tests_test_pubsub_transports_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_pubsub_transports_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_reconnect_ivl_SOURCES = tests/test_reconnect_ivl.cpp
tests_test_reconnect_ivl_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_reconnect_ivl_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_reconnect_options_SOURCES = tests/test_reconnect_options.cpp
tests_test_reconnect_options_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_reconnect_options_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_router_handover_SOURCES = tests/test_router_handover.cpp
tests_test_router_handover_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_router_handover_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_router_mandatory_SOURCES = tests/test_router_mandatory.cpp
tests_test_router_mandatory_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_router_mandatory_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_router_mandatory_hwm_SOURCES = tests/test_router_mandatory_hwm.cpp
tests_test_router_mandatory_hwm_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_router_mandatory_hwm_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_router_transports_SOURCES = tests/test_router_transports.cpp
tests_test_router_transports_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_router_transports_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_shutdown_stress_SOURCES = tests/test_shutdown_stress.cpp
tests_test_shutdown_stress_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_shutdown_stress_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_socket_null_SOURCES = tests/test_socket_null.cpp
tests_test_socket_null_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_socket_null_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_spec_router_SOURCES = tests/test_spec_router.cpp
tests_test_spec_router_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_spec_router_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_sub_forward_SOURCES = tests/test_sub_forward.cpp
tests_test_sub_forward_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_sub_forward_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_system_SOURCES = tests/test_system.cpp
tests_test_system_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_system_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_timeo_SOURCES = tests/test_timeo.cpp
tests_test_timeo_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_timeo_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_use_fd_SOURCES = tests/test_use_fd.cpp
tests_test_use_fd_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_use_fd_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_xpub_manual_SOURCES = tests/test_xpub_manual.cpp
tests_test_xpub_manual_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_xpub_manual_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_xpub_nodrop_SOURCES = tests/test_xpub_nodrop.cpp
tests_test_xpub_nodrop_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_xpub_nodrop_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_xpub_topic_SOURCES = tests/test_xpub_topic.cpp
tests_test_xpub_topic_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_xpub_topic_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_xpub_verbose_SOURCES = tests/test_xpub_verbose.cpp
tests_test_xpub_verbose_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_xpub_verbose_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_xpub_welcome_msg_SOURCES = tests/test_xpub_welcome_msg.cpp
tests_test_xpub_welcome_msg_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_xpub_welcome_msg_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

if FUZZING_ENGINE_LIB
fuzzer_apps = tests/test_bind_null_fuzzer \
	tests/test_connect_null_fuzzer \
	tests/test_bind_fuzzer \
	tests/test_connect_fuzzer \
	tests/test_socket_options_fuzzer \
	tests/test_z85_decode_fuzzer

tests_test_bind_null_fuzzer_DEPENDENCIES = src/libzmq.la
tests_test_bind_null_fuzzer_SOURCES = tests/test_bind_null_fuzzer.cpp
tests_test_bind_null_fuzzer_LDADD = ${TESTUTIL_LIBS} ${FUZZING_ENGINE_LIB} \
		$(top_builddir)/src/.libs/libzmq.a \
		${src_libzmq_la_LIBADD}
tests_test_bind_null_fuzzer_CPPFLAGS = ${TESTUTIL_CPPFLAGS}
tests_test_bind_null_fuzzer_CXXFLAGS = -std=c++11

tests_test_connect_null_fuzzer_DEPENDENCIES = src/libzmq.la
tests_test_connect_null_fuzzer_SOURCES = tests/test_connect_null_fuzzer.cpp
tests_test_connect_null_fuzzer_LDADD = ${TESTUTIL_LIBS} ${FUZZING_ENGINE_LIB} \
		$(top_builddir)/src/.libs/libzmq.a \
		${src_libzmq_la_LIBADD}
tests_test_connect_null_fuzzer_CPPFLAGS = ${TESTUTIL_CPPFLAGS}
tests_test_connect_null_fuzzer_CXXFLAGS = -std=c++11

tests_test_bind_fuzzer_DEPENDENCIES = src/libzmq.la
tests_test_bind_fuzzer_SOURCES = tests/test_bind_fuzzer.cpp
tests_test_bind_fuzzer_LDADD = ${TESTUTIL_LIBS} ${FUZZING_ENGINE_LIB} \
		$(top_builddir)/src/.libs/libzmq.a \
		${src_libzmq_la_LIBADD}
tests_test_bind_fuzzer_CPPFLAGS = ${TESTUTIL_CPPFLAGS}
tests_test_bind_fuzzer_CXXFLAGS = -std=c++11

tests_test_connect_fuzzer_DEPENDENCIES = src/libzmq.la
tests_test_connect_fuzzer_SOURCES = tests/test_connect_fuzzer.cpp
tests_test_connect_fuzzer_LDADD = ${TESTUTIL_LIBS} ${FUZZING_ENGINE_LIB} \
		$(top_builddir)/src/.libs/libzmq.a \
		${src_libzmq_la_LIBADD}
tests_test_connect_fuzzer_CPPFLAGS = ${TESTUTIL_CPPFLAGS}
tests_test_connect_fuzzer_CXXFLAGS = -std=c++11

tests_test_socket_options_fuzzer_DEPENDENCIES = src/libzmq.la
tests_test_socket_options_fuzzer_SOURCES = tests/test_socket_options_fuzzer.cpp
tests_test_socket_options_fuzzer_LDADD = ${TESTUTIL_LIBS} ${FUZZING_ENGINE_LIB} \
		$(top_builddir)/src/.libs/libzmq.a \
		${src_libzmq_la_LIBADD}
tests_test_socket_options_fuzzer_CPPFLAGS = ${TESTUTIL_CPPFLAGS}
tests_test_socket_options_fuzzer_CXXFLAGS = -std=c++11

tests_test_z85_decode_fuzzer_DEPENDENCIES = src/libzmq.la
tests_test_z85_decode_fuzzer_SOURCES = tests/test_z85_decode_fuzzer.cpp
tests_test_z85_decode_fuzzer_LDADD = ${TESTUTIL_LIBS} ${FUZZING_ENGINE_LIB} \
		$(top_builddir)/src/.libs/libzmq.a \
		${src_libzmq_la_LIBADD}
tests_test_z85_decode_fuzzer_CPPFLAGS = ${TESTUTIL_CPPFLAGS}
tests_test_z85_decode_fuzzer_CXXFLAGS = -std=c++11

if HAVE_WS
fuzzer_apps += tests/test_bind_ws_fuzzer

tests_test_bind_ws_fuzzer_DEPENDENCIES = src/libzmq.la
tests_test_bind_ws_fuzzer_SOURCES = tests/test_bind_ws_fuzzer.cpp
tests_test_bind_ws_fuzzer_LDADD = ${TESTUTIL_LIBS} ${FUZZING_ENGINE_LIB} \
		$(top_builddir)/src/.libs/libzmq.a \
		${src_libzmq_la_LIBADD}
tests_test_bind_ws_fuzzer_CPPFLAGS = ${TESTUTIL_CPPFLAGS}
tests_test_bind_ws_fuzzer_CXXFLAGS = -std=c++11
endif

FUZZINGdir = ${prefix}/${FUZZING_INSTALLDIR}
FUZZING_PROGRAMS = ${fuzzer_apps}
else
test_apps += tests/test_bind_null_fuzzer \
	tests/test_connect_null_fuzzer \
	tests/test_bind_fuzzer \
	tests/test_connect_fuzzer \
	tests/test_socket_options_fuzzer \
	tests/test_z85_decode_fuzzer

tests_test_bind_null_fuzzer_SOURCES = tests/test_bind_null_fuzzer.cpp
tests_test_bind_null_fuzzer_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_bind_null_fuzzer_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_connect_null_fuzzer_SOURCES = tests/test_connect_null_fuzzer.cpp
tests_test_connect_null_fuzzer_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_connect_null_fuzzer_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_bind_fuzzer_SOURCES = tests/test_bind_fuzzer.cpp
tests_test_bind_fuzzer_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_bind_fuzzer_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_connect_fuzzer_SOURCES = tests/test_connect_fuzzer.cpp
tests_test_connect_fuzzer_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_connect_fuzzer_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_socket_options_fuzzer_SOURCES = tests/test_socket_options_fuzzer.cpp
tests_test_socket_options_fuzzer_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_socket_options_fuzzer_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

tests_test_z85_decode_fuzzer_SOURCES = tests/test_z85_decode_fuzzer.cpp
tests_test_z85_decode_fuzzer_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_z85_decode_fuzzer_CPPFLAGS = ${TESTUTIL_CPPFLAGS}

if HAVE_WS
test_apps += tests/test_bind_ws_fuzzer

tests_test_bind_ws_fuzzer_SOURCES = tests/test_bind_ws_fuzzer.cpp
tests_test_bind_ws_fuzzer_LDADD = ${TESTUTIL_LIBS} src/libzmq.la
tests_test_bind_ws_fuzzer_CPPFLAGS = ${TESTUTIL_CPPFLAGS}
endif
endif

if ENABLE_STATIC
# unit tests - these include individual source files and test the internal functions
test_apps += \
	unittests/unittest_poller \
	unittests/unittest_ypipe \
	unittests/unittest_mtrie \
	unittests/unittest_ip_resolver \
	unittests/unittest_radix_tree

unittests_unittest_poller_SOURCES = unittests/unittest_poller.cpp
unittests_unittest_poller_CPPFLAGS = -I$(top_srcdir)/src ${TESTUTIL_CPPFLAGS} $(CODE_COVERAGE_CPPFLAGS)
unittests_unittest_poller_CXXFLAGS = $(CODE_COVERAGE_CXXFLAGS)
unittests_unittest_poller_LDADD = \
        ${TESTUTIL_LIBS} \
        $(top_builddir)/src/.libs/libzmq.a \
        ${src_libzmq_la_LIBADD} \
        $(CODE_COVERAGE_LDFLAGS)

unittests_unittest_ypipe_SOURCES = unittests/unittest_ypipe.cpp
unittests_unittest_ypipe_CPPFLAGS = -I$(top_srcdir)/src ${TESTUTIL_CPPFLAGS} $(CODE_COVERAGE_CPPFLAGS)
unittests_unittest_ypipe_CXXFLAGS = $(CODE_COVERAGE_CXXFLAGS)
unittests_unittest_ypipe_LDADD = \
        ${TESTUTIL_LIBS} \
        $(top_builddir)/src/.libs/libzmq.a \
        ${src_libzmq_la_LIBADD} \
        $(CODE_COVERAGE_LDFLAGS)

unittests_unittest_mtrie_SOURCES = unittests/unittest_mtrie.cpp
unittests_unittest_mtrie_CPPFLAGS = -I$(top_srcdir)/src ${TESTUTIL_CPPFLAGS} $(CODE_COVERAGE_CPPFLAGS)
unittests_unittest_mtrie_CXXFLAGS = $(CODE_COVERAGE_CXXFLAGS)
unittests_unittest_mtrie_LDADD = \
        ${TESTUTIL_LIBS} \
        $(top_builddir)/src/.libs/libzmq.a \
        ${src_libzmq_la_LIBADD} \
        $(CODE_COVERAGE_LDFLAGS)

unittests_unittest_ip_resolver_SOURCES = unittests/unittest_ip_resolver.cpp unittests/unittest_resolver_common.hpp
unittests_unittest_ip_resolver_CPPFLAGS = -I$(top_srcdir)/src ${TESTUTIL_CPPFLAGS} $(CODE_COVERAGE_CPPFLAGS)
unittests_unittest_ip_resolver_CXXFLAGS = $(CODE_COVERAGE_CXXFLAGS)
unittests_unittest_ip_resolver_LDADD = \
        ${TESTUTIL_LIBS} \
        $(top_builddir)/src/.libs/libzmq.a \
        ${src_libzmq_la_LIBADD} \
        $(CODE_COVERAGE_LDFLAGS)

unittests_unittest_radix_tree_SOURCES = unittests/unittest_radix_tree.cpp
unittests_unittest_radix_tree_CPPFLAGS = -I$(top_srcdir)/src ${TESTUTIL_CPPFLAGS} $(CODE_COVERAGE_CPPFLAGS)
unittests_unittest_radix_tree_CXXFLAGS = $(CODE_COVERAGE_CXXFLAGS)
unittests_unittest_radix_tree_LDADD =  \
        ${TESTUTIL_LIBS} \
        $(top_builddir)/src/.libs/libzmq.a \
        ${src_libzmq_la_LIBADD} \
        $(CODE_COVERAGE_LDFLAGS)

endif

check_PROGRAMS = ${test_apps}

#  Run the test cases
TESTS = $(test_apps)
XFAIL_TESTS =

if !ON_LINUX
XFAIL_TESTS += tests/test_abstract_ipc
endif

#  GNU/Hurd does not support getsockname on IPC, so ZMQ_LAST_ENDPOINT cannot be
#  used with IPC, so the following tests will fail
if ON_GNU
XFAIL_TESTS += tests/test_ipc_wildcard \
		tests/test_reqrep_ipc \
		tests/test_pair_ipc \
		tests/test_term_endpoint
endif

EXTRA_DIST = \
	external/unity/license.txt \
	external/unity/version.txt \
	external/wepoll/license.txt \
	external/wepoll/version.txt \
	external/wepoll/README.md \
	CMakeLists.txt \
	autogen.sh	\
	version.sh	\
	ci_build.sh \
	LICENSE \
	src/libzmq.vers \
	src/version.rc.in \
	tests/CMakeLists.txt \
        tests/test_pair_tcp_cap_net_admin.cpp \
	unittests/CMakeLists.txt \
        tools/curve_keygen.cpp

MAINTAINERCLEANFILES = \
	$(srcdir)/aclocal.m4 \
	$(srcdir)/autom4te.cache \
	$(srcdir)/configure \
	`find "$(srcdir)" -type f -name Makefile.in -print`

if WITH_CLANG_FORMAT
ALL_SOURCE_FILES = $(wildcard \
	$(top_srcdir)/src/*.c \
	$(top_srcdir)/src/*.cc \
	$(top_srcdir)/src/*.cpp \
	$(top_srcdir)/src/*.h \
	$(top_srcdir)/src/*.hpp \
	$(top_srcdir)/tests/*.c \
	$(top_srcdir)/tests/*.cc \
	$(top_srcdir)/tests/*.cpp \
	$(top_srcdir)/tests/*.h \
	$(top_srcdir)/tests/*.hpp \
	$(top_srcdir)/perf/*.c \
	$(top_srcdir)/perf/*.cc \
	$(top_srcdir)/perf/*.cpp \
	$(top_srcdir)/perf/*.h \
	$(top_srcdir)/perf/*.hpp \
	$(top_srcdir)/tools/*.c \
	$(top_srcdir)/tools/*.cc \
	$(top_srcdir)/tools/*.cpp \
	$(top_srcdir)/tools/*.h \
	$(top_srcdir)/tools/*.hpp \
	$(top_srcdir)/include/*.h \
 )

# Check if any sources need to be fixed, report the filenames and an error code
clang-format-check: $(ALL_SOURCE_FILES)
	@FAILED=0 ; IFS=";" ; IDS="`printf '\n\b'`" ; export IFS IDS; \
	 for FILE in $(ALL_SOURCE_FILES) ; do \
		test -s $$FILE || continue ; \
		$(CLANG_FORMAT) -style=file -output-replacements-xml "$$FILE" | grep "<replacement " >/dev/null && \
		{ echo "$$FILE is not correctly formatted" >&2 ; FAILED=1; } ; \
	 done; \
	 if test "$$FAILED" != 0 ; then \
		exit 1 ; \
	 fi

# Change source formatting
clang-format: $(ALL_SOURCE_FILES)
	$(CLANG_FORMAT) -style=file -i $(ALL_SOURCE_FILES)

# Change source formatting AND report the diff
clang-format-diff: clang-format
	git diff $(ALL_SOURCE_FILES)

else
clang-format clang-format-check clang-format-diff:
	@echo "Install the clang-format program, reconfigure and re-run this request"
	@exit 1
endif

@CODE_COVERAGE_RULES@

dist-hook:
	-rm $(distdir)/src/platform.hpp
	@if test -d "$(srcdir)/.git"; \
	then \
		echo Creating ChangeLog && \
		( cd "$(top_srcdir)" && \
		  echo '# Generated by Makefile. Do not edit.'; echo; \
		  $(top_srcdir)/config/missing --run git log --stat ) > ChangeLog.tmp \
		  && mv -f ChangeLog.tmp $(top_distdir)/ChangeLog \
		  || ( rm -f ChangeLog.tmp ; \
		       echo Failed to generate ChangeLog >&2 ); \
	else \
		echo A git clone is required to generate a ChangeLog >&2; \
	fi

maintainer-clean-local:
	-rm -rf $(top_srcdir)/config

@VALGRIND_CHECK_RULES@

VALGRIND_SUPPRESSIONS_FILES = builds/valgrind/valgrind.supp
