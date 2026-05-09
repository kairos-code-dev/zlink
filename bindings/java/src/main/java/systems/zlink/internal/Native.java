package systems.zlink.internal;

import systems.zlink.MonitorEvent;
import systems.zlink.MonitorEventType;
import systems.zlink.Message;
import systems.zlink.RecvException;
import systems.zlink.RecvResult;
import systems.zlink.RoutingId;
import systems.zlink.SubmitResult;
import systems.zlink.ZlinkException;
import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.SymbolLookup;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.util.ArrayList;
import java.util.List;

public final class Native {
    private static final Linker LINKER = Linker.nativeLinker();
    private static final SymbolLookup LOOKUP = LibraryLoader.lookup();
    private static final int ERRNO_EINTR = 4;
    public static final int PART_FINAL = 0;
    public static final int PART_MORE = 1;
    private static final ThreadLocal<MultipartReceiveScratch>
        MULTIPART_RECEIVE_SCRATCH =
            ThreadLocal.withInitial(MultipartReceiveScratch::new);

    private static final class MultipartReceiveScratch {
        private MemorySegment parts = MemorySegment.NULL;
        private long partCount;
        private final MultipartReceive result = new MultipartReceive();

        private void reset() {
            parts = MemorySegment.NULL;
            partCount = 0L;
        }

        private MemorySegment allocateParts(long newPartCount) {
            reset();
            if (newPartCount <= 0) {
                return MemorySegment.NULL;
            }
            parts = Arena.ofAuto().allocate(
                NativeLayouts.MSG_LAYOUT.byteSize() * newPartCount,
                NativeLayouts.MSG_LAYOUT.byteAlignment());
            partCount = newPartCount;
            return parts;
        }
    }

    private static MemorySegment requireSymbol(String name) {
        return LOOKUP.find(name).orElseThrow(
          () -> new IllegalStateException(
            "Missing native symbol '" + name
              + "'. Loaded libzlink is incompatible with this Java binding."));
    }

    private static MethodHandle downcall(String name, FunctionDescriptor fd) {
        return LOOKUP.find(name)
          .map(symbol -> LINKER.downcallHandle(symbol, fd))
          .orElseGet(() -> missingDowncall(name, fd));
    }

    private static MethodHandle missingDowncall(String name,
                                                FunctionDescriptor fd) {
        MethodType methodType = fd.toMethodType();
        IllegalStateException failure =
          new IllegalStateException(
            "Missing native symbol '" + name
              + "'. Loaded libzlink is incompatible with this Java binding.");
        MethodHandle throwing = MethodHandles.throwException(
          methodType.returnType(), IllegalStateException.class);
        throwing = MethodHandles.insertArguments(throwing, 0, failure);
        return MethodHandles.dropArguments(throwing, 0,
          methodType.parameterArray());
    }

    private static MethodHandle unsupportedLegacyDowncall(
        String name, FunctionDescriptor fd) {
        MethodType methodType = fd.toMethodType();
        UnsupportedOperationException failure =
          new UnsupportedOperationException(
            "Legacy native symbol '" + name
              + "' is not part of the canonical zlink surface.");
        MethodHandle throwing = MethodHandles.throwException(
          methodType.returnType(), UnsupportedOperationException.class);
        throwing = MethodHandles.insertArguments(throwing, 0, failure);
        return MethodHandles.dropArguments(throwing, 0,
          methodType.parameterArray());
    }

    private static MethodHandle optionalDowncall(String name,
                                                 FunctionDescriptor fd) {
        return LOOKUP.find(name)
          .map(symbol -> LINKER.downcallHandle(symbol, fd))
          .orElse(null);
    }

    private static final MethodHandle MH_VERSION = downcall("zlink_version",
            FunctionDescriptor.ofVoid(ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_CTX_NEW = downcall("zlink_ctx_new",
            FunctionDescriptor.of(ValueLayout.ADDRESS));
    private static final MethodHandle MH_CTX_TERM = downcall("zlink_ctx_term",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_CTX_SET = downcall("zlink_ctx_set",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_CTX_SET_DATA = downcall("zlink_ctx_set_data",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG));
    private static final MethodHandle MH_CTX_GET = downcall("zlink_ctx_get",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_CTX_SHUTDOWN = downcall("zlink_ctx_shutdown",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_CTX_AUTO_HWM_RECALCULATE = downcall(
            "zlink_ctx_auto_hwm_recalculate",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SOCKET = downcall("zlink_socket",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_CLOSE = downcall("zlink_close",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_BIND = downcall("zlink_bind",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_CONNECT = downcall("zlink_connect",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_UNBIND = downcall("zlink_unbind",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_DISCONNECT = downcall("zlink_disconnect",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_DISCONNECT_RID = downcall("zlink_disconnect_rid",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SOCKET_ATTACH_DISCOVERY = downcall(
            "zlink_socket_attach_discovery",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    private static final MethodHandle MH_SOCKET_SET_CHANNEL_NAME = downcall(
            "zlink_socket_set_channel_name",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    private static final MethodHandle MH_SOCKET_GET_CHANNEL_NAME = downcall(
            "zlink_socket_get_channel_name",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.JAVA_LONG,
                    ValueLayout.ADDRESS));
    private static final MethodHandle MH_RECV_HANDLER = downcall(
            "zlink_recv_handler",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SUBSCRIBE_HANDLER = downcall(
            "zlink_subscribe_handler",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SEND_READY_HANDLER = downcall(
            "zlink_send_ready_handler",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SEND_PART = downcall("zlink_send_part",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.JAVA_INT,
                    ValueLayout.JAVA_INT));
    private static final MethodHandle MH_SEND_PART_RID = downcall(
            "zlink_send_part_rid",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_JAVA_SEND_U32 = downcall(
            "zlink_java_send_u32",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_LONG, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_RECV_PART = downcall("zlink_recv_part",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_STREAM_ATTACH = downcall("zlink_stream_attach",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_STREAM_ATTACH_RAW = downcall("zlink_stream_attach_raw",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_STREAM_PACKET_HANDLER = downcall(
            "zlink_stream_packet_handler",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_STREAM_ATTACH_LEN32BE = downcall("zlink_stream_attach_len32be",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_STREAM_DETACH = downcall("zlink_stream_detach",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_STREAM_SEND = downcall("zlink_stream_send",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_STREAM_SEND_MSG = downcall("zlink_stream_send_msg",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_STREAM_BIND_ACTOR = downcall(
            "zlink_stream_bind_actor",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_STREAM_UNBIND_ACTOR = downcall(
            "zlink_stream_unbind_actor",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_STREAM_SEND_BOUND_ACTOR_PART = downcall(
            "zlink_stream_send_bound_actor_part",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_SETSOCKOPT = downcall("zlink_set_option",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG));
    private static final MethodHandle MH_GETSOCKOPT = downcall("zlink_get_option",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SET_ROUTER_OPTION = downcall(
            "zlink_set_router_option",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_LONG));
    private static final MethodHandle MH_GET_ROUTER_OPTION = downcall(
            "zlink_get_router_option",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    private static final MethodHandle MH_SET_DEALER_OPTION = downcall(
            "zlink_set_dealer_option",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_LONG));
    private static final MethodHandle MH_SET_SPOT_OPTION = downcall(
            "zlink_set_spot_option",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_LONG));
    private static final MethodHandle MH_GET_SPOT_OPTION = downcall(
            "zlink_get_spot_option",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    private static final MethodHandle MH_SET_SPOT_NODE_OPTION = downcall(
            "zlink_set_spot_node_option",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_LONG));
    private static final MethodHandle MH_GET_SPOT_NODE_OPTION = downcall(
            "zlink_get_spot_node_option",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    private static final MethodHandle MH_SET_PUB_OPTION = downcall(
            "zlink_set_pub_option",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_LONG));
    private static final MethodHandle MH_GET_PUB_OPTION = downcall(
            "zlink_get_pub_option",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    private static final MethodHandle MH_SET_SUB_OPTION = downcall(
            "zlink_set_sub_option",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_LONG));
    private static final MethodHandle MH_GET_SUB_OPTION = downcall(
            "zlink_get_sub_option",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    private static final MethodHandle MH_SET_STREAM_OPTION = downcall(
            "zlink_set_stream_option",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_LONG));
    private static final MethodHandle MH_GET_STREAM_OPTION = downcall(
            "zlink_get_stream_option",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    private static final MethodHandle MH_SET_ROUTING_ID = downcall("zlink_set_routing_id",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG));
    private static final MethodHandle MH_GET_ROUTING_ID = downcall("zlink_get_routing_id",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SET_SUBSCRIPTION = downcall("zlink_set_subscription",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_UNSET_SUBSCRIPTION = downcall("zlink_unset_subscription",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SUBSCRIPTION_AT = downcall("zlink_subscription_at",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_LONG, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_PUBLISH_PART = downcall(
            "zlink_publish_part",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_SUBSCRIBE_PART = downcall(
            "zlink_subscribe_part",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_LONG, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT));
    private static final MethodHandle MH_XPUB_RECV_PART = downcall(
            "zlink_xpub_recv_part",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.JAVA_LONG,
                    ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT));

    private static final MethodHandle MH_POLL = downcall("zlink_poll",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT, ValueLayout.JAVA_LONG,
                    ValueLayout.ADDRESS));
    private static final MethodHandle MH_POLLER_NEW = downcall("zlink_poller_new",
            FunctionDescriptor.of(ValueLayout.ADDRESS));
    private static final MethodHandle MH_POLLER_DESTROY = downcall("zlink_poller_destroy",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_POLLER_SIZE = downcall("zlink_poller_size",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    private static final MethodHandle MH_POLLER_ADD = downcall("zlink_poller_add",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_SHORT));
    private static final MethodHandle MH_POLLER_ADD_SPOT_SUB = MH_POLLER_ADD;
    private static final MethodHandle MH_POLLER_ADD_SPOT_PUB = MH_POLLER_ADD;
    private static final MethodHandle MH_POLLER_ADD_RECEIVER = MH_POLLER_ADD;
    private static final MethodHandle MH_POLLER_ADD_FD = downcall("zlink_poller_add_fd",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_SHORT));
    private static final MethodHandle MH_POLLER_ADD_TIMER = downcall("zlink_poller_add_timer",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_POLLER_MODIFY = downcall("zlink_poller_modify",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.JAVA_SHORT));
    private static final MethodHandle MH_POLLER_MODIFY_SPOT_SUB =
        MH_POLLER_MODIFY;
    private static final MethodHandle MH_POLLER_MODIFY_SPOT_PUB =
        MH_POLLER_MODIFY;
    private static final MethodHandle MH_POLLER_MODIFY_RECEIVER =
        MH_POLLER_MODIFY;
    private static final MethodHandle MH_POLLER_MODIFY_FD = downcall("zlink_poller_modify_fd",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT, ValueLayout.JAVA_SHORT));
    private static final MethodHandle MH_POLLER_REMOVE = downcall("zlink_poller_remove",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    private static final MethodHandle MH_POLLER_REMOVE_SPOT_SUB =
        MH_POLLER_REMOVE;
    private static final MethodHandle MH_POLLER_REMOVE_SPOT_PUB =
        MH_POLLER_REMOVE;
    private static final MethodHandle MH_POLLER_REMOVE_RECEIVER =
        MH_POLLER_REMOVE;
    private static final MethodHandle MH_POLLER_REMOVE_FD = downcall("zlink_poller_remove_fd",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT));
    private static final MethodHandle MH_POLLER_REMOVE_TIMER = downcall("zlink_poller_remove_timer",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    private static final MethodHandle MH_POLLER_WAIT = downcall("zlink_poller_wait",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.JAVA_LONG,
                    ValueLayout.ADDRESS));
    private static final MethodHandle MH_POLLER_WAIT_ALL = downcall("zlink_poller_wait_all",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.JAVA_INT, ValueLayout.JAVA_LONG,
                    ValueLayout.ADDRESS));

    private static final MethodHandle MH_MONITOR_OPEN = downcall("zlink_socket_monitor_open",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_MONITOR_HANDLER = downcall(
      "zlink_socket_monitor_handler",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_MONITOR_RECV = downcall("zlink_socket_monitor_recv",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_MONITOR_SNAPSHOT = downcall("zlink_monitor_snapshot",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_MONITOR_CLOSE = downcall("zlink_monitor_close",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_ERRNO = downcall("zlink_errno",
            FunctionDescriptor.of(ValueLayout.JAVA_INT));
    private static final MethodHandle MH_STRERROR = downcall("zlink_strerror",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_HAS = downcall("zlink_has",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SLEEP = downcall("zlink_sleep",
            FunctionDescriptor.ofVoid(ValueLayout.JAVA_INT));
    private static final MethodHandle MH_SET_TLS_SRV = downcall(
      "zlink_set_tls_server",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_SET_TLS_CLI = downcall(
      "zlink_set_tls_client",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_PROXY = downcall("zlink_proxy",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_PROXY_STEERABLE = downcall(
      "zlink_proxy_steerable",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    private static final MethodHandle MH_ATOMIC_COUNTER_NEW = downcall(
      "zlink_atomic_counter_new",
      FunctionDescriptor.of(ValueLayout.ADDRESS));
    private static final MethodHandle MH_ATOMIC_COUNTER_SET = downcall(
      "zlink_atomic_counter_set",
      FunctionDescriptor.ofVoid(ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_ATOMIC_COUNTER_INC = downcall(
      "zlink_atomic_counter_inc",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_ATOMIC_COUNTER_DEC = downcall(
      "zlink_atomic_counter_dec",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_ATOMIC_COUNTER_VALUE = downcall(
      "zlink_atomic_counter_value",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_ATOMIC_COUNTER_DESTROY = downcall(
      "zlink_atomic_counter_destroy",
      FunctionDescriptor.ofVoid(ValueLayout.ADDRESS));

    private static final MethodHandle MH_TIMER_NEW = downcall("zlink_timer_new",
      FunctionDescriptor.of(ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_TIMER_NEW = downcall(
      "zlink_spot_timer_new",
      FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_TIMER_DESTROY = downcall(
      "zlink_timer_destroy",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_TIMER_START = downcall(
      "zlink_timer_start",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.JAVA_LONG, ValueLayout.JAVA_LONG));
    private static final MethodHandle MH_TIMER_STOP = downcall(
      "zlink_timer_stop",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_TIMER_RECV = downcall(
      "zlink_timer_recv",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS));
    private static final MethodHandle MH_TIMER_HANDLER = downcall(
      "zlink_timer_handler",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    private static final MethodHandle MH_STOPWATCH_START = downcall(
      "zlink_stopwatch_start",
      FunctionDescriptor.of(ValueLayout.ADDRESS));
    private static final MethodHandle MH_STOPWATCH_INTERMEDIATE = downcall(
      "zlink_stopwatch_intermediate",
      FunctionDescriptor.of(ValueLayout.JAVA_LONG, ValueLayout.ADDRESS));
    private static final MethodHandle MH_STOPWATCH_STOP = downcall(
      "zlink_stopwatch_stop",
      FunctionDescriptor.of(ValueLayout.JAVA_LONG, ValueLayout.ADDRESS));

    private static final MethodHandle MH_THREAD_START = downcall(
      "zlink_thread_start",
      FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS));
    private static final MethodHandle MH_THREAD_JOIN = downcall(
      "zlink_thread_join",
      FunctionDescriptor.ofVoid(ValueLayout.ADDRESS));

    private static final MethodHandle MH_ROUTER_REQUEST_SPOT_PART = downcall(
      "zlink_router_request_spot_part",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT,
        ValueLayout.JAVA_INT, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_ROUTER_REPLY_SPOT_PART = downcall(
      "zlink_router_reply_spot_part",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG,
        ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_ROUTER_SEND_SPOT_PART = downcall(
      "zlink_router_send_spot_part",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS,
        ValueLayout.JAVA_INT, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_ROUTER_SPOT_HANDLER = downcall(
      "zlink_router_handler",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_ROUTER_RECV_PART = downcall(
      "zlink_router_recv_part",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_JAVA_ROUTER_RECV = optionalDowncall(
      "zlink_java_router_recv_compat",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_ROUTER_REQUEST_PART = downcall(
      "zlink_router_request_part",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT,
        ValueLayout.JAVA_INT, ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS));
    private static final MethodHandle MH_DEALER_REQUEST_PART = downcall(
      "zlink_dealer_request_part",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.JAVA_INT, ValueLayout.JAVA_INT,
        ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_ROUTER_REPLY_PART = downcall(
      "zlink_router_reply_part",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.JAVA_LONG, ValueLayout.ADDRESS,
        ValueLayout.JAVA_INT));

    private static final MethodHandle MH_SPOT_SEND_CHANNEL_PART = downcall(
      "zlink_spot_send_channel_part",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT,
        ValueLayout.JAVA_INT));
    private static final MethodHandle MH_SPOT_SEND_SPOT_PART = downcall(
      "zlink_spot_send_spot_part",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS,
        ValueLayout.JAVA_INT, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_SPOT_REPLY_SPOT_PART = downcall(
      "zlink_spot_reply_spot_part",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG,
        ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_SPOT_REPLY_ROUTER_PART = downcall(
      "zlink_spot_reply_router_part",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.JAVA_LONG, ValueLayout.ADDRESS,
        ValueLayout.JAVA_INT));
    private static final MethodHandle MH_SPOT_PUBLISH_PART = downcall(
      "zlink_spot_publish_part",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT,
        ValueLayout.JAVA_INT));
    private static final MethodHandle MH_SPOT_SUBSCRIBE_PART = downcall(
      "zlink_spot_subscribe_part",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_SPOT_REQUEST_CHANNEL_PART = downcall(
      "zlink_spot_request_channel_part",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.JAVA_INT, ValueLayout.JAVA_INT,
        ValueLayout.JAVA_INT));
    private static final MethodHandle MH_SPOT_REQUEST_SPOT_PART = downcall(
      "zlink_spot_request_spot_part",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT,
        ValueLayout.JAVA_INT, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_SPOT_REQUEST_ROUTER_PART = downcall(
      "zlink_spot_request_router_part",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.JAVA_INT, ValueLayout.JAVA_INT,
        ValueLayout.JAVA_INT));
    private static final MethodHandle MH_SOCKET_REQUEST_PROGRESS_INTERNAL =
      downcall("zlink_socket_request_progress_internal",
        FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_REQUEST_PROGRESS_INTERNAL =
      downcall("zlink_spot_request_progress_internal",
        FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_CHANNEL_REPLY_PROGRESS_FROM =
      downcall("zlink_spot_channel_reply_progress_from",
        FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
          ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_HANDLER = downcall(
      "zlink_spot_handler",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_DISPATCH_EVENT_HANDLER = downcall(
      "zlink_spot_dispatch_event_handler",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_RECV_PART = downcall(
      "zlink_spot_recv_part",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_SPOT_ACTOR_JOIN_RECV = downcall(
      "zlink_spot_actor_join_recv",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS,
        ValueLayout.JAVA_INT));
    private static final MethodHandle MH_SPOT_ACTOR_JOIN_REPLY = downcall(
      "zlink_spot_actor_join_reply",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.JAVA_LONG));
    private static final MethodHandle MH_SPOT_ACTORS_SNAPSHOT = downcall(
      "zlink_spot_actors_snapshot",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    private static final MethodHandle MH_REG_NEW = downcall("zlink_registry_new",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_REG_BIND = downcall("zlink_registry_bind",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_REG_SET = downcall("zlink_registry_set",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_REG_GET = downcall("zlink_registry_get",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    private static final MethodHandle MH_REG_ADD_PEER = downcall("zlink_registry_add_peer",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_REG_DESTROY = downcall("zlink_registry_destroy",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_REG_STATUS_SNAPSHOT = downcall(
            "zlink_registry_status_snapshot",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    private static final MethodHandle MH_REG_SERVICE_SUMMARY_SNAPSHOT = downcall(
            "zlink_registry_service_summary_snapshot",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    private static final MethodHandle MH_REG_MEMBER_PEERS = downcall(
            "zlink_registry_member_peers",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    private static final MethodHandle MH_REG_TOPOLOGY_SNAPSHOT = downcall(
            "zlink_registry_topology_snapshot",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_REG_TOPOLOGY_QUERY = downcall(
            "zlink_registry_topology_query",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    private static final MethodHandle MH_REG_QUERY_CLIENT_NEW = downcall(
            "zlink_registry_query_client_new",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_REG_QUERY_CLIENT_CONNECT = downcall(
            "zlink_registry_query_client_connect",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    private static final MethodHandle MH_REG_QUERY_SNAPSHOT = downcall(
            "zlink_registry_query_snapshot",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    private static final MethodHandle MH_REG_QUERY_DESTROY = downcall(
            "zlink_registry_query_destroy",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));

    private static final MethodHandle MH_DISC_NEW_FIXED = downcall(
            "zlink_discovery_new",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_DISC_CONNECT = downcall("zlink_discovery_connect_registry",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_DISC_RESOLVE_SPOT = downcall(
            "zlink_discovery_resolve_spot",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_DISC_RESOLVE_ACTOR = downcall(
            "zlink_discovery_resolve_actor",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_DISC_SET_VALUE = downcall(
            "zlink_discovery_set_value",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_LONG));
    private static final MethodHandle MH_DISC_GET_VALUE = downcall(
            "zlink_discovery_get_value",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    private static final MethodHandle MH_DISC_DESTROY = downcall("zlink_discovery_destroy",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_DISC_MEMBER_PEERS = downcall(
            "zlink_discovery_member_peers",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    private static final MethodHandle MH_PROVIDER_NEW =
        unsupportedLegacyDowncall("zlink_receiver_new",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                ValueLayout.ADDRESS));
    private static final MethodHandle MH_PROVIDER_BIND =
        unsupportedLegacyDowncall("zlink_receiver_bind",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                ValueLayout.ADDRESS));
    private static final MethodHandle MH_PROVIDER_CONN =
        unsupportedLegacyDowncall("zlink_receiver_connect_registry",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                ValueLayout.ADDRESS));
    private static final MethodHandle MH_PROVIDER_REG =
        unsupportedLegacyDowncall("zlink_receiver_register",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                ValueLayout.JAVA_INT));
    private static final MethodHandle MH_PROVIDER_UPD =
        unsupportedLegacyDowncall("zlink_receiver_update_weight",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_PROVIDER_UNREG =
        unsupportedLegacyDowncall("zlink_receiver_unregister",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                ValueLayout.ADDRESS));
    private static final MethodHandle MH_PROVIDER_RESULT =
        unsupportedLegacyDowncall("zlink_receiver_register_result",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_PROVIDER_TLS =
        unsupportedLegacyDowncall("zlink_receiver_set_tls_server",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_PROVIDER_RECV =
        unsupportedLegacyDowncall("zlink_receiver_recv",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_PROVIDER_LAST_ENDPOINT =
        unsupportedLegacyDowncall("zlink_receiver_last_endpoint",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_PROVIDER_PEER_INFO =
        unsupportedLegacyDowncall("zlink_receiver_peer_info",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_PROVIDER_ROUTER_PEERS =
        unsupportedLegacyDowncall("zlink_receiver_router_peers",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_PROVIDER_DESTROY =
        unsupportedLegacyDowncall("zlink_receiver_destroy",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_PROVIDER_SET_OPTION =
        unsupportedLegacyDowncall("zlink_receiver_set_option",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                ValueLayout.JAVA_LONG));
    private static final MethodHandle MH_REGISTRY_SETSOCKOPT =
        unsupportedLegacyDowncall("zlink_registry_setsockopt",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                ValueLayout.JAVA_INT, ValueLayout.JAVA_INT,
                ValueLayout.ADDRESS, ValueLayout.JAVA_LONG));
    private static final MethodHandle MH_PROVIDER_SET_ROUTING_ID =
        unsupportedLegacyDowncall("zlink_receiver_set_routing_id",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                ValueLayout.ADDRESS, ValueLayout.JAVA_LONG));
    private static final MethodHandle MH_PROVIDER_ROUTING_ID =
        unsupportedLegacyDowncall("zlink_receiver_routing_id",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                ValueLayout.ADDRESS));

    private static final MethodHandle MH_SPOT_NODE_NEW = downcall("zlink_spot_node_new",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_NEW = downcall("zlink_spot_new",
      FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_DESTROY = downcall("zlink_spot_destroy",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_NODE_DESTROY = downcall("zlink_spot_node_destroy",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_NODE_ENTRY_SPOT = downcall(
            "zlink_spot_node_entry_spot",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_NODE_SPOT_LOOKUP = downcall(
            "zlink_spot_node_spot_lookup",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_NODE_BIND = downcall("zlink_spot_node_bind",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_NODE_CONN_PEER = downcall("zlink_spot_node_connect_peer",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_NODE_DISC_PEER = downcall("zlink_spot_node_disconnect_peer",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_NODE_DISC_PEER_RID = downcall("zlink_spot_node_disconnect_peer_rid",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_NODE_ACTOR_NEW = downcall(
            "zlink_spot_node_actor_new",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_NODE_ACTOR_DESTROY = downcall(
            "zlink_spot_node_actor_destroy",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_SPOT_NODE_ACTOR_LOOKUP = downcall(
            "zlink_spot_node_actor_lookup",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_REMOTE_ACTOR_GET_REF = downcall(
            "zlink_remote_actor_get_ref",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_NODE_CREATE_REMOTE_ACTOR = downcall(
            "zlink_spot_node_create_remote_actor",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS,
                    ValueLayout.JAVA_LONG, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT));
    private static final MethodHandle MH_SPOT_NODE_ACTOR_ADMISSION_HANDLER = downcall(
            "zlink_spot_node_actor_admission_handler",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_NODE_ACTOR_JOIN_SPOT = downcall(
            "zlink_spot_node_actor_join_spot",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_LONG, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT,
                    ValueLayout.JAVA_INT));
    private static final MethodHandle MH_SPOT_NODE_ACTOR_LEAVE_SPOT = downcall(
            "zlink_spot_node_actor_leave_spot",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT));
    private static final MethodHandle MH_SPOT_NODE_ACTOR_RECV_PART = downcall(
            "zlink_spot_node_actor_recv_part",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT));
    private static final MethodHandle MH_SPOT_NODE_ACTOR_SEND_BOUND_SESSION_MSG = downcall(
            "zlink_spot_node_actor_send_bound_session_msg",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_SPOT_NODE_ACTOR_CLOSE_BOUND_SESSION = downcall(
            "zlink_spot_node_actor_close_bound_session",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_SPOT_NODE_ATTACH_DISCOVERY = downcall(
            "zlink_spot_node_attach_discovery",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_NODE_ATTACH_CHANNEL_DEALER = downcall(
            "zlink_spot_node_attach_channel_dealer",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_NODE_ATTACH_CHANNEL_DEALER_MANUAL = downcall(
            "zlink_spot_node_attach_channel_dealer_manual",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_NODE_ATTACH_PUB_INGRESS = downcall(
            "zlink_spot_node_attach_pub_ingress",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_NODE_REG = downcall("zlink_spot_node_register",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_NODE_UNREG = downcall("zlink_spot_node_unregister",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_NODE_SET_DISC = downcall("zlink_spot_node_set_discovery",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_NODE_TLS_SRV = downcall("zlink_spot_node_set_tls_server",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_NODE_TLS_CLI = downcall("zlink_spot_node_set_tls_client",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_SPOT_NODE_STATUS_SNAPSHOT = downcall(
            "zlink_spot_node_status_snapshot",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_NODE_PEERS_SNAPSHOT = downcall(
            "zlink_spot_node_peers_snapshot",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_NODE_PEERS_QUERY = downcall(
            "zlink_spot_node_peers_query",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_NODE_SUBJECTS_SNAPSHOT = downcall(
            "zlink_spot_node_subjects_snapshot",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_NODE_INTERNAL_SOCKETS_SNAPSHOT =
        downcall("zlink_spot_node_internal_sockets_snapshot",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_NODE_SPOTS_SNAPSHOT = downcall(
            "zlink_spot_node_spots_snapshot",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_NODE_ACTORS_SNAPSHOT = downcall(
            "zlink_spot_node_actors_snapshot",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_PUB_PEERS =
        unsupportedLegacyDowncall("zlink_spot_pub_peers",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_SUB_PEERS =
        unsupportedLegacyDowncall("zlink_spot_sub_peers",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_PUB_NEW =
        unsupportedLegacyDowncall("zlink_spot_pub_new",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_PUB_DESTROY =
        unsupportedLegacyDowncall("zlink_spot_pub_destroy",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_PUB_PUBLISH =
        unsupportedLegacyDowncall("zlink_spot_pub_publish",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                ValueLayout.JAVA_LONG, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_SPOT_SUB_NEW =
        unsupportedLegacyDowncall("zlink_spot_sub_new",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_SUB_DESTROY =
        unsupportedLegacyDowncall("zlink_spot_sub_destroy",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_SUB_SUBSCRIBE =
        unsupportedLegacyDowncall("zlink_spot_sub_subscribe",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_SUB_SUBSCRIBE_PATTERN =
        unsupportedLegacyDowncall("zlink_spot_sub_subscribe_pattern",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_SUB_UNSUBSCRIBE =
        unsupportedLegacyDowncall("zlink_spot_sub_unsubscribe",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_SUB_RECV =
        unsupportedLegacyDowncall("zlink_spot_sub_recv",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                ValueLayout.ADDRESS));
    private Native() {}

    public static final class MultipartReceive {
        private byte[] routingId;
        private MemorySegment parts;
        private long partCount;

        private MultipartReceive reset(byte[] routingId, MemorySegment parts,
                                       long partCount) {
            this.routingId = routingId;
            this.parts = parts;
            this.partCount = partCount;
            return this;
        }

        public byte[] routingId() {
            return routingId;
        }

        public MemorySegment parts() {
            return parts;
        }

        public long partCount() {
            return partCount;
        }
    }

    private static boolean invalidMultipart(MemorySegment parts,
                                            long partCount) {
        return partCount <= 0
            || parts == null
            || parts.address() == 0;
    }

    private static MemorySegment nthPart(MemorySegment parts, long index) {
        long msgSize = NativeLayouts.MSG_LAYOUT.byteSize();
        return parts.asSlice(index * msgSize, msgSize);
    }

    private static int sendMultipartLoop(MemorySegment socket,
                                         MemorySegment routingId,
                                         MemorySegment parts,
                                         long partCount,
                                         int flags) {
        if (invalidMultipart(parts, partCount)) {
            return SubmitResult.INVALID_ARGUMENT.value();
        }
        for (long i = 0; i < partCount; i++) {
            int partFlag = i + 1 < partCount ? PART_MORE : PART_FINAL;
            int rc = routingId == null || routingId.address() == 0
                ? sendPart(socket, nthPart(parts, i), flags, partFlag)
                : sendPartRid(socket, routingId, nthPart(parts, i), flags,
                    partFlag);
            if (rc != 0) {
                return rc;
            }
        }
        return 0;
    }

    private static byte[] decodeRoutingIdPointer(MemorySegment routingIdPtr) {
        if (routingIdPtr == null || routingIdPtr.address() == 0) {
            return null;
        }
        MemorySegment routingId = routingIdPtr.reinterpret(
            NativeLayouts.ROUTING_ID_LAYOUT.byteSize());
        int routingIdSize = routingId.get(ValueLayout.JAVA_BYTE,
            NativeLayouts.ROUTING_ID_SIZE_OFFSET) & 0xFF;
        if (routingIdSize == 0) {
            return null;
        }
        byte[] decoded = new byte[routingIdSize];
        MemorySegment.copy(routingId, NativeLayouts.ROUTING_ID_DATA_OFFSET,
            MemorySegment.ofArray(decoded), 0, routingIdSize);
        return decoded;
    }

    private static void copyRoutingIdOut(MemorySegment target,
                                         MemorySegment routingIdPtr) {
        if (target == null || target.address() == 0) {
            return;
        }
        target.set(ValueLayout.JAVA_BYTE, NativeLayouts.ROUTING_ID_SIZE_OFFSET,
            (byte) 0);
        if (routingIdPtr == null || routingIdPtr.address() == 0) {
            return;
        }
        MemorySegment routingId = routingIdPtr.reinterpret(
            NativeLayouts.ROUTING_ID_LAYOUT.byteSize());
        int routingIdSize = routingId.get(ValueLayout.JAVA_BYTE,
            NativeLayouts.ROUTING_ID_SIZE_OFFSET) & 0xFF;
        target.set(ValueLayout.JAVA_BYTE, NativeLayouts.ROUTING_ID_SIZE_OFFSET,
            (byte) routingIdSize);
        if (routingIdSize > 0) {
            MemorySegment.copy(routingId, NativeLayouts.ROUTING_ID_DATA_OFFSET,
                target, NativeLayouts.ROUTING_ID_DATA_OFFSET, routingIdSize);
        }
    }

    private static MemorySegment materializeParts(MultipartReceiveScratch scratch,
                                                  List<Message> receivedParts) {
        MemorySegment parts = scratch.allocateParts(receivedParts.size());
        long moved = 0L;
        try {
            for (int i = 0; i < receivedParts.size(); i++) {
                InternalAccess.messageMoveTo(receivedParts.get(i),
                    nthPart(parts, i));
                receivedParts.get(i).close();
                moved++;
            }
            return parts;
        } catch (RuntimeException ex) {
            if (parts.address() != 0 && moved > 0) {
                NativeMsg.multipartClose(parts, moved);
            }
            throw ex;
        } finally {
            for (int i = (int) moved; i < receivedParts.size(); i++) {
                try {
                    receivedParts.get(i).close();
                } catch (RuntimeException ignored) {
                }
            }
        }
    }

    public static int[] version() {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment major = arena.allocate(ValueLayout.JAVA_INT);
            MemorySegment minor = arena.allocate(ValueLayout.JAVA_INT);
            MemorySegment patch = arena.allocate(ValueLayout.JAVA_INT);
            MH_VERSION.invokeExact(major, minor, patch);
            return new int[] {
                    major.get(ValueLayout.JAVA_INT, 0),
                    minor.get(ValueLayout.JAVA_INT, 0),
                    patch.get(ValueLayout.JAVA_INT, 0)
            };
        } catch (Throwable t) {
            throw new RuntimeException("zlink_version failed", t);
        }
    }

    public static MemorySegment ctxNew() {
        try {
            return (MemorySegment) MH_CTX_NEW.invokeExact();
        } catch (Throwable t) {
            throw new RuntimeException("zlink_ctx_new failed", t);
        }
    }

    public static int ctxTerm(MemorySegment ctx) {
        try {
            return (int) MH_CTX_TERM.invokeExact(ctx);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_ctx_term failed", t);
        }
    }

    public static int ctxSet(MemorySegment ctx, int option, int value) {
        try {
            return (int) MH_CTX_SET.invokeExact(ctx, option, value);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_ctx_set failed", t);
        }
    }

    public static int ctxSetData(MemorySegment ctx, int option,
                                 MemorySegment value, long valueLength) {
        try {
            return (int) MH_CTX_SET_DATA.invokeExact(ctx, option, value,
              valueLength);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_ctx_set_data failed", t);
        }
    }

    public static int ctxGet(MemorySegment ctx, int option) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment errorOut = arena.allocate(ValueLayout.JAVA_INT);
            return (int) MH_CTX_GET.invokeExact(ctx, option, errorOut);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_ctx_get failed", t);
        }
    }

    public static int ctxShutdown(MemorySegment ctx) {
        try {
            return (int) MH_CTX_SHUTDOWN.invokeExact(ctx);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_ctx_shutdown failed", t);
        }
    }

    public static int ctxAutoHwmRecalculate(MemorySegment ctx) {
        try {
            return (int) MH_CTX_AUTO_HWM_RECALCULATE.invokeExact(ctx);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_ctx_auto_hwm_recalculate failed", t);
        }
    }

    public static MemorySegment socket(MemorySegment ctx, int type) {
        try {
            return (MemorySegment) MH_SOCKET.invokeExact(ctx, type);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_socket failed", t);
        }
    }

    public static int close(MemorySegment socket) {
        try {
            return (int) MH_CLOSE.invokeExact(socket);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_close failed", t);
        }
    }

    public static int bind(MemorySegment socket, MemorySegment addr) {
        try {
            return (int) MH_BIND.invokeExact(socket, addr);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_bind failed", t);
        }
    }

    public static int connect(MemorySegment socket, MemorySegment addr) {
        try {
            return (int) MH_CONNECT.invokeExact(socket, addr);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_connect failed", t);
        }
    }

    public static int unbind(MemorySegment socket, MemorySegment addr) {
        try {
            return (int) MH_UNBIND.invokeExact(socket, addr);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_unbind failed", t);
        }
    }

    public static int disconnect(MemorySegment socket, MemorySegment addr) {
        try {
            return (int) MH_DISCONNECT.invokeExact(socket, addr);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_disconnect failed", t);
        }
    }

    public static int disconnectRid(MemorySegment socket, MemorySegment peerRid) {
        try {
            return (int) MH_DISCONNECT_RID.invokeExact(socket, peerRid);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_disconnect_rid failed", t);
        }
    }

    public static int socketAttachDiscovery(MemorySegment socket,
                                            MemorySegment discovery) {
        try {
            return (int) MH_SOCKET_ATTACH_DISCOVERY.invokeExact(socket,
                discovery);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_socket_attach_discovery failed",
              t);
        }
    }

    public static int socketSetChannelName(MemorySegment socket,
                                           MemorySegment channelName) {
        try {
            return (int) MH_SOCKET_SET_CHANNEL_NAME.invokeExact(socket,
                channelName);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_socket_set_channel_name failed",
              t);
        }
    }

    public static int socketGetChannelName(MemorySegment socket,
                                           MemorySegment channelNameOut,
                                           long channelNameCapacity,
                                           MemorySegment channelNameLenOut) {
        try {
            return (int) MH_SOCKET_GET_CHANNEL_NAME.invokeExact(socket,
                channelNameOut, channelNameCapacity, channelNameLenOut);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_socket_get_channel_name failed",
              t);
        }
    }

    public static int recvHandler(MemorySegment handle, MemorySegment handler,
                                  MemorySegment userdata) {
        try {
            return (int) MH_RECV_HANDLER.invokeExact(handle, handler, userdata);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_recv_handler failed", t);
        }
    }

    public static int subscribeHandler(MemorySegment handle,
                                       MemorySegment handler,
                                       MemorySegment userdata) {
        try {
            return (int) MH_SUBSCRIBE_HANDLER.invokeExact(handle, handler,
                userdata);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_subscribe_handler failed", t);
        }
    }

    public static int sendReadyHandler(MemorySegment handle,
                                       MemorySegment handler,
                                       MemorySegment userdata) {
        try {
            return (int) MH_SEND_READY_HANDLER.invokeExact(handle, handler,
                userdata);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_send_ready_handler failed", t);
        }
    }

    public static int sendMultipart(MemorySegment socket, MemorySegment parts,
                                    long partCount, int flags) {
        try {
            return sendMultipartLoop(socket, MemorySegment.NULL, parts,
                partCount, flags);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_send_part failed", t);
        }
    }

    public static int sendPart(MemorySegment socket, MemorySegment part,
                               int flags, int partFlag) {
        try {
            return (int) MH_SEND_PART.invokeExact(socket, part, flags, partFlag);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_send_part failed", t);
        }
    }

    public static int sendMultipart(MemorySegment socket, MemorySegment routingId,
                                    MemorySegment parts, long partCount,
                                    int flags) {
        try {
            return sendMultipartLoop(socket, routingId, parts, partCount,
                flags);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_send_part_rid failed", t);
        }
    }

    public static int sendPartRid(MemorySegment socket,
                                  MemorySegment routingId,
                                  MemorySegment part,
                                  int flags,
                                  int partFlag) {
        try {
            return (int) MH_SEND_PART_RID.invokeExact(socket, routingId, part,
                flags, partFlag);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_send_part_rid failed", t);
        }
    }

    public static int sendMultipartU32(MemorySegment socket, int routingId,
                                       MemorySegment parts, long partCount,
                                       int flags) {
        try {
            return (int) MH_JAVA_SEND_U32.invokeExact(socket, routingId, parts,
                partCount, flags);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_java_send_u32 failed", t);
        }
    }

    public static int recvPart(MemorySegment socket, MemorySegment sourceRidOut,
                               MemorySegment partOut,
                               MemorySegment hasMoreOut,
                               int flags) {
        try {
            return (int) MH_RECV_PART.invokeExact(socket, sourceRidOut, partOut,
                hasMoreOut, flags);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_recv_part failed", t);
        }
    }

    public static MultipartReceive recvMultipart(MemorySegment socket,
                                                 int flags) {
        MultipartReceiveScratch scratch = MULTIPART_RECEIVE_SCRATCH.get();
        scratch.reset();
        try {
            while (true) {
                List<Message> receivedParts = new ArrayList<>();
                byte[] routingId = null;
                try (Arena arena = Arena.ofConfined()) {
                    MemorySegment routingIdOut = arena.allocate(
                        ValueLayout.ADDRESS);
                    MemorySegment hasMoreOut = arena.allocate(
                        ValueLayout.JAVA_INT);
                    while (true) {
                        Message part = new Message();
                        boolean success = false;
                        try {
                            int rc = recvPart(socket, routingIdOut,
                                InternalAccess.messageNativeHandle(part),
                                hasMoreOut, flags);
                            if (rc != 0) {
                                Message.closeAll(receivedParts);
                                if (errno() == ERRNO_EINTR) {
                                    break;
                                }
                                return null;
                            }
                            success = true;
                            if (receivedParts.isEmpty()) {
                                routingId = decodeRoutingIdPointer(
                                    routingIdOut.get(ValueLayout.ADDRESS, 0));
                            }
                            InternalAccess.messageFinishReceive(part,
                                hasMoreOut.get(ValueLayout.JAVA_INT, 0) != 0);
                            receivedParts.add(part);
                            if (!InternalAccess.messageMore(part)) {
                                MemorySegment parts =
                                    materializeParts(scratch, receivedParts);
                                return scratch.result.reset(routingId, parts,
                                    scratch.partCount);
                            }
                        } finally {
                            if (!success) {
                                try {
                                    part.close();
                                } catch (RuntimeException ignored) {
                                }
                            }
                        }
                    }
                }
            }
        } catch (Throwable t) {
            throw new RuntimeException("zlink_recv_part failed", t);
        }
    }

    public static int streamAttach(MemorySegment socket, MemorySegment callback,
                                   int flags) {
        try {
            return (int) MH_STREAM_ATTACH.invokeExact(socket, callback, flags);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_stream_attach failed", t);
        }
    }

    public static int streamAttachRaw(MemorySegment socket,
                                      MemorySegment callback) {
        try {
            return (int) MH_STREAM_ATTACH_RAW.invokeExact(socket, callback, MemorySegment.NULL);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_stream_attach_raw failed", t);
        }
    }

    public static int streamPacketHandler(MemorySegment socket,
                                          MemorySegment callback) {
        try {
            return (int) MH_STREAM_PACKET_HANDLER.invokeExact(socket, callback,
                MemorySegment.NULL);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_stream_packet_handler failed", t);
        }
    }

    public static int streamAttachLen32be(MemorySegment socket,
                                          MemorySegment callback) {
        try {
            return (int) MH_STREAM_ATTACH_LEN32BE.invokeExact(socket, callback);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_stream_attach_len32be failed", t);
        }
    }

    public static int streamDetach(MemorySegment socket) {
        try {
            return (int) MH_STREAM_DETACH.invokeExact(socket);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_stream_detach failed", t);
        }
    }

    public static int streamSend(MemorySegment socket, MemorySegment rid,
                                 MemorySegment payload, long len, int flags) {
        try {
            return (int) MH_STREAM_SEND.invokeExact(socket, rid, payload, len,
              flags);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_stream_send failed", t);
        }
    }

    public static int streamSendMsg(MemorySegment socket, MemorySegment rid,
                                    MemorySegment msg, int flags) {
        try {
            return (int) MH_STREAM_SEND_MSG.invokeExact(socket, rid, msg, flags);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_stream_send_msg failed", t);
        }
    }

    public static int streamBindActor(MemorySegment node, MemorySegment stream,
                                      MemorySegment sessionRid,
                                      MemorySegment actor,
                                      int timeoutMs) {
        try {
            return (int) MH_STREAM_BIND_ACTOR.invokeExact(node, stream,
              sessionRid, actor, timeoutMs);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_stream_bind_actor failed", t);
        }
    }

    public static int streamUnbindActor(MemorySegment node, MemorySegment stream,
                                        MemorySegment sessionRid,
                                        MemorySegment actorId,
                                        int timeoutMs) {
        try {
            return (int) MH_STREAM_UNBIND_ACTOR.invokeExact(node, stream,
              sessionRid, actorId, timeoutMs);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_stream_unbind_actor failed", t);
        }
    }

    public static int streamSendBoundActorPart(MemorySegment node,
                                               MemorySegment stream,
                                               MemorySegment sessionRid,
                                               MemorySegment actorId,
                                               MemorySegment msg,
                                               int flags,
                                               int partFlag) {
        try {
            return (int) MH_STREAM_SEND_BOUND_ACTOR_PART.invokeExact(node,
              stream, sessionRid, actorId, msg, flags, partFlag);
        } catch (Throwable t) {
            throw new RuntimeException(
              "zlink_stream_send_bound_actor_part failed", t);
        }
    }

    public static int setSockOpt(MemorySegment socket, int option, MemorySegment value, long len) {
        try {
            return (int) MH_SETSOCKOPT.invokeExact(socket, option, value, len);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_setsockopt failed", t);
        }
    }

    public static int getSockOpt(MemorySegment socket, int option, MemorySegment value, MemorySegment len) {
        try {
            return (int) MH_GETSOCKOPT.invokeExact(socket, option, value, len);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_getsockopt failed", t);
        }
    }

    public static int setRouterOption(MemorySegment handle, int option,
                                      MemorySegment value, long len) {
        try {
            return (int) MH_SET_ROUTER_OPTION.invokeExact(handle, option, value,
              len);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_set_router_option failed", t);
        }
    }

    public static int getRouterOption(MemorySegment handle, int option,
                                      MemorySegment value,
                                      MemorySegment len) {
        try {
            return (int) MH_GET_ROUTER_OPTION.invokeExact(handle, option, value,
              len);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_get_router_option failed", t);
        }
    }

    public static int setDealerOption(MemorySegment handle, int option,
                                      MemorySegment value, long len) {
        try {
            return (int) MH_SET_DEALER_OPTION.invokeExact(handle, option, value,
              len);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_set_dealer_option failed", t);
        }
    }

    public static int setSpotOption(MemorySegment handle, int option,
                                    MemorySegment value, long len) {
        try {
            return (int) MH_SET_SPOT_OPTION.invokeExact(handle, option, value,
              len);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_set_spot_option failed", t);
        }
    }

    public static int getSpotOption(MemorySegment handle, int option,
                                    MemorySegment value, MemorySegment len) {
        try {
            return (int) MH_GET_SPOT_OPTION.invokeExact(handle, option, value,
              len);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_get_spot_option failed", t);
        }
    }

    public static int setSpotNodeOption(MemorySegment handle, int option,
                                        MemorySegment value, long len) {
        try {
            return (int) MH_SET_SPOT_NODE_OPTION.invokeExact(handle, option,
              value, len);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_set_spot_node_option failed", t);
        }
    }

    public static int getSpotNodeOption(MemorySegment handle, int option,
                                        MemorySegment value,
                                        MemorySegment len) {
        try {
            return (int) MH_GET_SPOT_NODE_OPTION.invokeExact(handle, option,
              value, len);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_get_spot_node_option failed", t);
        }
    }

    public static int setPubOption(MemorySegment handle, int option,
                                   MemorySegment value, long len) {
        try {
            return (int) MH_SET_PUB_OPTION.invokeExact(handle, option, value,
              len);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_set_pub_option failed", t);
        }
    }

    public static int getPubOption(MemorySegment handle, int option,
                                   MemorySegment value, MemorySegment len) {
        try {
            return (int) MH_GET_PUB_OPTION.invokeExact(handle, option, value,
              len);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_get_pub_option failed", t);
        }
    }

    public static int setSubOption(MemorySegment handle, int option,
                                   MemorySegment value, long len) {
        try {
            return (int) MH_SET_SUB_OPTION.invokeExact(handle, option, value,
              len);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_set_sub_option failed", t);
        }
    }

    public static int getSubOption(MemorySegment handle, int option,
                                   MemorySegment value, MemorySegment len) {
        try {
            return (int) MH_GET_SUB_OPTION.invokeExact(handle, option, value,
              len);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_get_sub_option failed", t);
        }
    }

    public static int setStreamOption(MemorySegment handle, int option,
                                      MemorySegment value, long len) {
        try {
            return (int) MH_SET_STREAM_OPTION.invokeExact(handle, option, value,
              len);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_set_stream_option failed", t);
        }
    }

    public static int getStreamOption(MemorySegment handle, int option,
                                      MemorySegment value,
                                      MemorySegment len) {
        try {
            return (int) MH_GET_STREAM_OPTION.invokeExact(handle, option, value,
              len);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_get_stream_option failed", t);
        }
    }

    public static int setRoutingId(MemorySegment handle, MemorySegment value,
                                   long len) {
        try {
            return (int) MH_SET_ROUTING_ID.invokeExact(handle, value, len);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_set_routing_id failed", t);
        }
    }

    public static int getRoutingId(MemorySegment handle, MemorySegment outRid) {
        try {
            return (int) MH_GET_ROUTING_ID.invokeExact(handle, outRid);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_get_routing_id failed", t);
        }
    }

    public static int setSubscription(MemorySegment handle,
                                      MemorySegment filter) {
        try {
            return (int) MH_SET_SUBSCRIPTION.invokeExact(handle, filter);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_set_subscription failed", t);
        }
    }

    public static int unsetSubscription(MemorySegment handle,
                                        MemorySegment filter) {
        try {
            return (int) MH_UNSET_SUBSCRIPTION.invokeExact(handle, filter);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_unset_subscription failed", t);
        }
    }

    public static int subscriptionAt(MemorySegment handle, long index,
                                     MemorySegment filterOut,
                                     MemorySegment filterLenInOut,
                                     MemorySegment isPatternOut) {
        try {
            return (int) MH_SUBSCRIPTION_AT.invokeExact(handle, index,
              filterOut, filterLenInOut, isPatternOut);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_subscription_at failed", t);
        }
    }

    public static int publish(MemorySegment subject, MemorySegment topicId,
                              MemorySegment parts, long partCount,
                              int flags) {
        try {
            if (invalidMultipart(parts, partCount)) {
                return SubmitResult.INVALID_ARGUMENT.value();
            }
            for (long i = 0; i < partCount; i++) {
                int partFlag = i + 1 < partCount ? PART_MORE : PART_FINAL;
                int rc = publishPart(subject, topicId,
                    nthPart(parts, i), flags, partFlag);
                if (rc != 0) {
                    return rc;
                }
            }
            return 0;
        } catch (Throwable t) {
            throw new RuntimeException("zlink_publish_part failed", t);
        }
    }

    public static int publishPart(MemorySegment subject, MemorySegment topicId,
                                  MemorySegment part, int flags,
                                  int partFlag) {
        try {
            return (int) MH_PUBLISH_PART.invokeExact(subject, topicId, part,
              flags, partFlag);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_publish_part failed", t);
        }
    }

    public static int subscribe(MemorySegment subject, MemorySegment sourceRidOut,
                                MemorySegment partsOut,
                                MemorySegment partCountOut,
                                MemorySegment topicIdOut,
                                MemorySegment topicIdLenOut,
                                int flags) {
        try {
            MultipartReceiveScratch scratch = MULTIPART_RECEIVE_SCRATCH.get();
            scratch.reset();
            while (true) {
                List<Message> receivedParts = new ArrayList<>();
                try (Arena arena = Arena.ofConfined()) {
                    MemorySegment routingIdPtrOut = arena.allocate(
                        ValueLayout.ADDRESS);
                    MemorySegment hasMoreOut = arena.allocate(
                        ValueLayout.JAVA_INT);
                    long topicCapacity = topicIdLenOut == null
                        || topicIdLenOut.address() == 0
                        ? 0L
                        : Math.max(0L,
                            topicIdLenOut.get(ValueLayout.JAVA_LONG, 0));
                    while (true) {
                        Message part = new Message();
                        boolean success = false;
                        try {
                            int rc = subscribePart(subject, routingIdPtrOut,
                                topicIdOut, topicCapacity, topicIdLenOut,
                                InternalAccess.messageNativeHandle(part),
                                hasMoreOut, flags);
                            if (rc != 0) {
                                Message.closeAll(receivedParts);
                                if (errno() == ERRNO_EINTR) {
                                    break;
                                }
                                return rc;
                            }
                            success = true;
                            if (receivedParts.isEmpty()) {
                                copyRoutingIdOut(sourceRidOut,
                                    routingIdPtrOut.get(ValueLayout.ADDRESS, 0));
                            }
                            InternalAccess.messageFinishReceive(part,
                                hasMoreOut.get(ValueLayout.JAVA_INT, 0) != 0);
                            receivedParts.add(part);
                            if (!InternalAccess.messageMore(part)) {
                                MemorySegment parts = materializeParts(scratch,
                                    receivedParts);
                                partsOut.set(ValueLayout.ADDRESS, 0, parts);
                                partCountOut.set(ValueLayout.JAVA_LONG, 0,
                                    scratch.partCount);
                                return 0;
                            }
                        } finally {
                            if (!success) {
                                try {
                                    part.close();
                                } catch (RuntimeException ignored) {
                                }
                            }
                        }
                    }
                }
            }
        } catch (Throwable t) {
            throw new RuntimeException("zlink_subscribe_part failed", t);
        }
    }

    public static int subscribePart(MemorySegment subject,
                                    MemorySegment sourceRidOut,
                                    MemorySegment topicIdOut,
                                    long topicCapacity,
                                    MemorySegment topicIdLenOut,
                                    MemorySegment partOut,
                                    MemorySegment hasMoreOut,
                                    int flags) {
        try {
            return (int) MH_SUBSCRIBE_PART.invokeExact(subject, sourceRidOut,
              topicIdOut, topicCapacity, topicIdLenOut, partOut, hasMoreOut,
              flags);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_subscribe_part failed", t);
        }
    }

    public static int subscriptionEvent(MemorySegment subject,
                                        MemorySegment sourceRidOut,
                                        MemorySegment subscribedOut,
                                        MemorySegment topicIdOut,
                                        MemorySegment topicIdLenOut,
                                        int flags) {
        try {
            return (int) MH_XPUB_RECV_PART.invokeExact(subject, sourceRidOut,
              subscribedOut, topicIdOut,
              topicIdLenOut.get(ValueLayout.JAVA_LONG, 0), topicIdLenOut,
              flags);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_xpub_recv_part failed", t);
        }
    }

    public static MemorySegment monitorOpen(MemorySegment socket, int events) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment options = arena.allocate(
              NativeLayouts.SOCKET_MONITOR_OPEN_OPTIONS_LAYOUT);
            options.set(ValueLayout.JAVA_INT,
              NativeLayouts.SOCKET_MONITOR_OPEN_EVENTS_OFFSET, events);
            return (MemorySegment) MH_MONITOR_OPEN.invokeExact(socket, options);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_socket_monitor_open failed", t);
        }
    }

    public static int monitorHandler(MemorySegment monitor,
                                     MemorySegment handler,
                                     MemorySegment userdata) {
        try {
            return (int) MH_MONITOR_HANDLER.invokeExact(monitor, handler,
              userdata);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_socket_monitor_handler failed",
              t);
        }
    }

    public static int monitor(MemorySegment socket, MemorySegment endpoint,
                              int events) {
        throw new UnsupportedOperationException(
            "legacy zlink_socket_monitor is not supported; "
                + "use zlink_socket_monitor_open");
    }

    public static MonitorEvent monitorRecv(MemorySegment socket, int flags) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment evt = arena.allocate(NativeLayouts.MONITOR_EVENT_LAYOUT);
            int rc = (int) MH_MONITOR_RECV.invokeExact(socket, evt, flags);
            if (rc != 0)
                throw InternalAccess.zlinkExceptionFromLastError("zlink_socket_monitor_recv");
            long event = evt.get(ValueLayout.JAVA_LONG, NativeLayouts.MONITOR_EVENT_OFFSET);
            long value = evt.get(ValueLayout.JAVA_LONG, NativeLayouts.MONITOR_VALUE_OFFSET);
            int routingSize = evt.get(ValueLayout.JAVA_BYTE,
              NativeLayouts.MONITOR_ROUTING_OFFSET
                + NativeLayouts.ROUTING_ID_SIZE_OFFSET) & 0xFF;
            byte[] routing = new byte[routingSize];
            if (routingSize > 0) {
                MemorySegment.copy(evt,
                    NativeLayouts.MONITOR_ROUTING_OFFSET
                      + NativeLayouts.ROUTING_ID_DATA_OFFSET,
                    MemorySegment.ofArray(routing), 0, routingSize);
            }
            String local = NativeHelpers.fromCString(evt.asSlice(NativeLayouts.MONITOR_LOCAL_OFFSET, 256), 256);
            String remote = NativeHelpers.fromCString(evt.asSlice(NativeLayouts.MONITOR_REMOTE_OFFSET, 256), 256);
            return new MonitorEvent(EnumCodecs.monitorEventTypeFromValue(event), value,
              routingSize == 0 ? java.util.Optional.empty()
                : java.util.Optional.of(RoutingId.fromBytes(routing)),
              local, remote);
        } catch (ZlinkException ex) {
            throw ex;
        } catch (Throwable t) {
            throw new RuntimeException("monitor recv failed", t);
        }
    }

    public static int monitorSnapshot(MemorySegment monitor,
                                      MemorySegment snapshotOut) {
        try {
            return (int) MH_MONITOR_SNAPSHOT.invokeExact(monitor, snapshotOut);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_monitor_snapshot failed", t);
        }
    }

    public static int monitorClose(MemorySegment monitorPtr) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment holder = arena.allocate(ValueLayout.ADDRESS);
            holder.set(ValueLayout.ADDRESS, 0, monitorPtr);
            return (int) MH_MONITOR_CLOSE.invokeExact(holder);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_monitor_close failed", t);
        }
    }

    public static int setTlsServer(MemorySegment handle, MemorySegment cert,
                                   MemorySegment key, int requireClient) {
        try {
            return (int) MH_SET_TLS_SRV.invokeExact(handle, cert, key,
              requireClient);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_set_tls_server failed", t);
        }
    }

    public static int setTlsClient(MemorySegment handle, MemorySegment ca,
                                   MemorySegment host, int trust) {
        try {
            return (int) MH_SET_TLS_CLI.invokeExact(handle, ca, host, trust);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_set_tls_client failed", t);
        }
    }

    public static int errno() {
        try {
            return (int) MH_ERRNO.invokeExact();
        } catch (Throwable t) {
            throw new RuntimeException("zlink_errno failed", t);
        }
    }

    public static String strerror(int errnum) {
        try {
            MemorySegment cstr = (MemorySegment) MH_STRERROR.invokeExact(errnum);
            if (cstr == null || cstr.address() == 0)
                return "";
            return cstr.reinterpret(1024).getString(0);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_strerror failed", t);
        }
    }

    public static int has(MemorySegment capability) {
        try {
            return (int) MH_HAS.invokeExact(capability);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_has failed", t);
        }
    }

    public static void sleep(int seconds) {
        try {
            MH_SLEEP.invokeExact(seconds);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_sleep failed", t);
        }
    }

    public static int proxy(MemorySegment frontend, MemorySegment backend,
                            MemorySegment capture) {
        try {
            return (int) MH_PROXY.invokeExact(frontend, backend, capture);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_proxy failed", t);
        }
    }

    public static int proxySteerable(MemorySegment frontend,
                                     MemorySegment backend,
                                     MemorySegment capture,
                                     MemorySegment control) {
        try {
            return (int) MH_PROXY_STEERABLE.invokeExact(frontend, backend,
                capture, control);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_proxy_steerable failed", t);
        }
    }

    public static MemorySegment atomicCounterNew() {
        try {
            return (MemorySegment) MH_ATOMIC_COUNTER_NEW.invokeExact();
        } catch (Throwable t) {
            throw new RuntimeException("zlink_atomic_counter_new failed", t);
        }
    }

    public static void atomicCounterSet(MemorySegment counter, int value) {
        try {
            MH_ATOMIC_COUNTER_SET.invokeExact(counter, value);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_atomic_counter_set failed", t);
        }
    }

    public static int atomicCounterInc(MemorySegment counter) {
        try {
            return (int) MH_ATOMIC_COUNTER_INC.invokeExact(counter);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_atomic_counter_inc failed", t);
        }
    }

    public static int atomicCounterDec(MemorySegment counter) {
        try {
            return (int) MH_ATOMIC_COUNTER_DEC.invokeExact(counter);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_atomic_counter_dec failed", t);
        }
    }

    public static int atomicCounterValue(MemorySegment counter) {
        try {
            return (int) MH_ATOMIC_COUNTER_VALUE.invokeExact(counter);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_atomic_counter_value failed", t);
        }
    }

    public static void atomicCounterDestroy(MemorySegment counter) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment holder = arena.allocate(ValueLayout.ADDRESS);
            holder.set(ValueLayout.ADDRESS, 0, counter);
            MH_ATOMIC_COUNTER_DESTROY.invokeExact(holder);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_atomic_counter_destroy failed", t);
        }
    }

    public static MemorySegment timerNew() {
        try {
            return (MemorySegment) MH_TIMER_NEW.invokeExact();
        } catch (Throwable t) {
            throw new RuntimeException("zlink_timer_new failed", t);
        }
    }

    public static MemorySegment spotTimerNew(MemorySegment spot) {
        try {
            return (MemorySegment) MH_SPOT_TIMER_NEW.invokeExact(spot);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_timer_new failed", t);
        }
    }

    public static int timerDestroy(MemorySegment timer) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment holder = arena.allocate(ValueLayout.ADDRESS);
            holder.set(ValueLayout.ADDRESS, 0, timer);
            return (int) MH_TIMER_DESTROY.invokeExact(holder);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_timer_destroy failed", t);
        }
    }

    public static int timerStart(MemorySegment timer, long intervalNs,
                                 long repeatCount) {
        try {
            return (int) MH_TIMER_START.invokeExact(timer, intervalNs,
                repeatCount);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_timer_start failed", t);
        }
    }

    public static int timerStop(MemorySegment timer) {
        try {
            return (int) MH_TIMER_STOP.invokeExact(timer);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_timer_stop failed", t);
        }
    }

    public static int timerRecv(MemorySegment timer, MemorySegment fireCountOut) {
        try {
            return (int) MH_TIMER_RECV.invokeExact(timer, fireCountOut);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_timer_recv failed", t);
        }
    }

    public static int timerHandler(MemorySegment timer, MemorySegment handler,
                                   MemorySegment userdata) {
        try {
            return (int) MH_TIMER_HANDLER.invokeExact(timer, handler, userdata);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_timer_handler failed", t);
        }
    }

    public static MemorySegment stopwatchStart() {
        try {
            return (MemorySegment) MH_STOPWATCH_START.invokeExact();
        } catch (Throwable t) {
            throw new RuntimeException("zlink_stopwatch_start failed", t);
        }
    }

    public static long stopwatchIntermediate(MemorySegment watch) {
        try {
            return (long) MH_STOPWATCH_INTERMEDIATE.invokeExact(watch);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_stopwatch_intermediate failed", t);
        }
    }

    public static long stopwatchStop(MemorySegment watch) {
        try {
            return (long) MH_STOPWATCH_STOP.invokeExact(watch);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_stopwatch_stop failed", t);
        }
    }

    public static MemorySegment threadStart(MemorySegment func,
                                            MemorySegment arg) {
        try {
            return (MemorySegment) MH_THREAD_START.invokeExact(func, arg);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_thread_start failed", t);
        }
    }

    public static void threadJoin(MemorySegment thread) {
        try {
            MH_THREAD_JOIN.invokeExact(thread);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_thread_join failed", t);
        }
    }

    public static int routerRequestSpotPart(MemorySegment router,
                                            MemorySegment destNodeRid,
                                            MemorySegment destSpotRid,
                                            MemorySegment part,
                                            MemorySegment handler,
                                            MemorySegment userdata,
                                            int flags,
                                            int partFlag,
                                            int timeoutMs) {
        try {
            return (int) MH_ROUTER_REQUEST_SPOT_PART.invokeExact(router,
                destNodeRid, destSpotRid, part, handler, userdata, flags,
                partFlag, timeoutMs);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_router_request_spot_part failed",
                t);
        }
    }

    public static int routerReplySpotPart(MemorySegment router,
                                          MemorySegment destNodeRid,
                                          MemorySegment destSpotRid,
                                          long requestSeq,
                                          MemorySegment part,
                                          int partFlag) {
        try {
            return (int) MH_ROUTER_REPLY_SPOT_PART.invokeExact(router,
                destNodeRid, destSpotRid, requestSeq, part, partFlag);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_router_reply_spot_part failed",
                t);
        }
    }

    public static int routerSendSpotPart(MemorySegment router,
                                         MemorySegment destNodeRid,
                                         MemorySegment destSpotRid,
                                         MemorySegment part,
                                         int flags,
                                         int partFlag) {
        try {
            return (int) MH_ROUTER_SEND_SPOT_PART.invokeExact(router,
                destNodeRid, destSpotRid, part, flags, partFlag);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_router_send_spot_part failed", t);
        }
    }

    public static int routerHandler(MemorySegment router,
                                    MemorySegment handler,
                                    MemorySegment userdata) {
        try {
            return (int) MH_ROUTER_SPOT_HANDLER.invokeExact(router, handler,
                userdata);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_router_handler failed", t);
        }
    }

    public static int routerRecv(MemorySegment router,
                                 MemorySegment sourceNodeRidOut,
                                 MemorySegment sourceSpotRidOut,
                                 MemorySegment requestSeqOut,
                                 MemorySegment partsOut,
                                 MemorySegment partCountOut, int flags) {
        try {
            if (MH_JAVA_ROUTER_RECV != null) {
                return (int) MH_JAVA_ROUTER_RECV.invokeExact(router,
                    sourceNodeRidOut, sourceSpotRidOut, requestSeqOut,
                    partsOut, partCountOut, flags);
            }
            MultipartReceiveScratch scratch = MULTIPART_RECEIVE_SCRATCH.get();
            scratch.reset();
            while (true) {
                List<Message> receivedParts = new ArrayList<>();
                try (Arena arena = Arena.ofConfined()) {
                    MemorySegment nodeRidPtrOut = arena.allocate(
                        ValueLayout.ADDRESS);
                    MemorySegment spotRidPtrOut = arena.allocate(
                        ValueLayout.ADDRESS);
                    MemorySegment seqOut = arena.allocate(
                        ValueLayout.JAVA_LONG);
                    MemorySegment hasMoreOut = arena.allocate(
                        ValueLayout.JAVA_INT);
                    while (true) {
                        Message part = new Message();
                        boolean success = false;
                        try {
                            int rc = routerRecvPart(router, nodeRidPtrOut,
                                spotRidPtrOut, seqOut,
                                InternalAccess.messageNativeHandle(part),
                                hasMoreOut, flags);
                            if (rc != 0) {
                                Message.closeAll(receivedParts);
                                if (errno() == ERRNO_EINTR) {
                                    break;
                                }
                                return rc;
                            }
                            success = true;
                            if (receivedParts.isEmpty()) {
                                sourceNodeRidOut.set(ValueLayout.ADDRESS, 0,
                                    nodeRidPtrOut.get(ValueLayout.ADDRESS, 0));
                                sourceSpotRidOut.set(ValueLayout.ADDRESS, 0,
                                    spotRidPtrOut.get(ValueLayout.ADDRESS, 0));
                                requestSeqOut.set(ValueLayout.JAVA_LONG, 0,
                                    seqOut.get(ValueLayout.JAVA_LONG, 0));
                            }
                            InternalAccess.messageFinishReceive(part,
                                hasMoreOut.get(ValueLayout.JAVA_INT, 0) != 0);
                            receivedParts.add(part);
                            if (!InternalAccess.messageMore(part)) {
                                MemorySegment parts = materializeParts(scratch,
                                    receivedParts);
                                partsOut.set(ValueLayout.ADDRESS, 0, parts);
                                partCountOut.set(ValueLayout.JAVA_LONG, 0,
                                    scratch.partCount);
                                return 0;
                            }
                        } finally {
                            if (!success) {
                                try {
                                    part.close();
                                } catch (RuntimeException ignored) {
                                }
                            }
                        }
                    }
                }
            }
        } catch (Throwable t) {
            throw new RuntimeException("zlink_router_recv_part failed", t);
        }
    }

    public static int routerRecvPart(MemorySegment router,
                                     MemorySegment sourceNodeRidOut,
                                     MemorySegment sourceSpotRidOut,
                                     MemorySegment requestSeqOut,
                                     MemorySegment partOut,
                                     MemorySegment hasMoreOut,
                                     int flags) {
        try {
            return (int) MH_ROUTER_RECV_PART.invokeExact(router,
                sourceNodeRidOut, sourceSpotRidOut, requestSeqOut, partOut,
                hasMoreOut, flags);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_router_recv_part failed", t);
        }
    }

    public static int dealerRequestPart(MemorySegment dealer,
                                        MemorySegment part,
                                        int flags,
                                        int partFlag,
                                        int timeoutMs,
                                        MemorySegment handler,
                                        MemorySegment userdata) {
        try {
            return (int) MH_DEALER_REQUEST_PART.invokeExact(dealer, part, flags,
                partFlag, timeoutMs, handler, userdata);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_dealer_request_part failed", t);
        }
    }

    public static int routerRequestPart(MemorySegment router,
                                        MemorySegment peerRid,
                                        MemorySegment part,
                                        int flags,
                                        int partFlag,
                                        int timeoutMs,
                                        MemorySegment handler,
                                        MemorySegment userdata) {
        try {
            return (int) MH_ROUTER_REQUEST_PART.invokeExact(router, peerRid,
                part, flags, partFlag, timeoutMs, handler, userdata);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_router_request_part failed", t);
        }
    }

    public static int routerReplyPart(MemorySegment router,
                                      MemorySegment peerRid,
                                      long requestSeq,
                                      MemorySegment part,
                                      int partFlag) {
        try {
            return (int) MH_ROUTER_REPLY_PART.invokeExact(router, peerRid,
                requestSeq, part, partFlag);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_router_reply_part failed", t);
        }
    }

    public static int spotReplySpotPart(MemorySegment spot,
                                        MemorySegment destNodeRid,
                                        MemorySegment destSpotRid,
                                        long requestSeq,
                                        MemorySegment part,
                                        int partFlag) {
        try {
            return (int) MH_SPOT_REPLY_SPOT_PART.invokeExact(spot,
                destNodeRid, destSpotRid, requestSeq, part, partFlag);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_reply_spot_part failed", t);
        }
    }

    public static int spotSendSpotPart(MemorySegment spot,
                                       MemorySegment destNodeRid,
                                       MemorySegment destSpotRid,
                                       MemorySegment part,
                                       int flags,
                                       int partFlag) {
        try {
            return (int) MH_SPOT_SEND_SPOT_PART.invokeExact(spot, destNodeRid,
                destSpotRid, part, flags, partFlag);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_send_spot_part failed", t);
        }
    }

    public static int spotReplyRouterPart(MemorySegment spot,
                                          MemorySegment peerRid,
                                          long requestSeq,
                                          MemorySegment part,
                                          int partFlag) {
        try {
            return (int) MH_SPOT_REPLY_ROUTER_PART.invokeExact(spot, peerRid,
                requestSeq, part, partFlag);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_reply_router_part failed",
                t);
        }
    }

    public static int spotHandler(MemorySegment spot, MemorySegment handler,
                                  MemorySegment userdata) {
        try {
            return (int) MH_SPOT_HANDLER.invokeExact(spot, handler, userdata);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_handler failed", t);
        }
    }

    public static int spotDispatchEventHandler(MemorySegment spot,
                                               MemorySegment handler,
                                               MemorySegment userdata) {
        try {
            return (int) MH_SPOT_DISPATCH_EVENT_HANDLER.invokeExact(spot,
                handler, userdata);
        } catch (Throwable t) {
            throw new RuntimeException(
                "zlink_spot_dispatch_event_handler failed", t);
        }
    }

    public static int spotRecvPart(MemorySegment spot,
                                   MemorySegment sourceRidOut,
                                   MemorySegment spotRidOut,
                                   MemorySegment requestSeqOut,
                                   MemorySegment partOut,
                                   MemorySegment hasMoreOut,
                                   int flags) {
        try {
            return (int) MH_SPOT_RECV_PART.invokeExact(spot, sourceRidOut,
                spotRidOut, requestSeqOut, partOut, hasMoreOut, flags);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_recv_part failed", t);
        }
    }

    public static int pollRaw(MemorySegment items, int count, int timeoutMs) {
        if (items == null || items.address() == 0 || count <= 0)
            return 0;
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment errorOut = arena.allocate(ValueLayout.JAVA_INT);
            errorOut.set(ValueLayout.JAVA_INT, 0, 0);
            int rc = (int) MH_POLL.invokeExact(items, count, (long) timeoutMs,
                errorOut);
            if (rc < 0) {
                int error = errorOut.get(ValueLayout.JAVA_INT, 0);
                if (error != 0) {
                    throw new systems.zlink.ConfigException(
                        systems.zlink.ConfigResult.fromValue(error),
                        errno());
                }
            }
            return rc;
        } catch (RuntimeException ex) {
            throw ex;
        } catch (Throwable t) {
            throw new RuntimeException("poll failed", t);
        }
    }

    public static MemorySegment pollerNew() {
        try {
            return (MemorySegment) MH_POLLER_NEW.invokeExact();
        } catch (Throwable t) {
            throw new RuntimeException("zlink_poller_new failed", t);
        }
    }

    public static int pollerDestroy(MemorySegment pollerPtr) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment p = arena.allocate(ValueLayout.ADDRESS);
            p.set(ValueLayout.ADDRESS, 0, pollerPtr);
            return (int) MH_POLLER_DESTROY.invokeExact(p);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_poller_destroy failed", t);
        }
    }

    public static int pollerSize(MemorySegment poller) {
        try {
            return (int) MH_POLLER_SIZE.invokeExact(poller, MemorySegment.NULL);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_poller_size failed", t);
        }
    }

    public static int pollerAdd(MemorySegment poller, MemorySegment socket,
                                MemorySegment userData, int events) {
        try {
            return (int) MH_POLLER_ADD.invokeExact(poller, socket, userData,
                (short) events);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_poller_add failed", t);
        }
    }

    public static int pollerAddSpotSub(MemorySegment poller,
                                        MemorySegment spotSub,
                                        MemorySegment userData,
                                        int events) {
        try {
            return (int) MH_POLLER_ADD_SPOT_SUB.invokeExact(poller, spotSub,
                userData, (short) events);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_poller_add_spot_sub failed", t);
        }
    }

    public static int pollerAddSpotPub(MemorySegment poller,
                                        MemorySegment spotPub,
                                        MemorySegment userData,
                                        int events) {
        try {
            return (int) MH_POLLER_ADD_SPOT_PUB.invokeExact(poller, spotPub,
                userData, (short) events);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_poller_add_spot_pub failed", t);
        }
    }

    public static int pollerAddReceiver(MemorySegment poller,
                                         MemorySegment receiver,
                                         MemorySegment userData,
                                         int events) {
        try {
            return (int) MH_POLLER_ADD_RECEIVER.invokeExact(poller, receiver,
                userData, (short) events);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_poller_add_receiver failed", t);
        }
    }

    public static int pollerAddFd(MemorySegment poller, int fd,
                                  MemorySegment userData, int events) {
        try {
            return (int) MH_POLLER_ADD_FD.invokeExact(poller, fd, userData,
                (short) events);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_poller_add_fd failed", t);
        }
    }

    public static int pollerAddTimer(MemorySegment poller, MemorySegment timer,
                                     MemorySegment userData) {
        try {
            return (int) MH_POLLER_ADD_TIMER.invokeExact(poller, timer,
              userData);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_poller_add_timer failed", t);
        }
    }

    public static int pollerModify(MemorySegment poller, MemorySegment socket,
                                   int events) {
        try {
            return (int) MH_POLLER_MODIFY.invokeExact(poller, socket,
                (short) events);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_poller_modify failed", t);
        }
    }

    public static int pollerModifySpotSub(MemorySegment poller,
                                           MemorySegment spotSub,
                                           int events) {
        try {
            return (int) MH_POLLER_MODIFY_SPOT_SUB.invokeExact(poller, spotSub,
                (short) events);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_poller_modify_spot_sub failed",
              t);
        }
    }

    public static int pollerModifySpotPub(MemorySegment poller,
                                           MemorySegment spotPub,
                                           int events) {
        try {
            return (int) MH_POLLER_MODIFY_SPOT_PUB.invokeExact(poller, spotPub,
                (short) events);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_poller_modify_spot_pub failed",
              t);
        }
    }

    public static int pollerModifyReceiver(MemorySegment poller,
                                            MemorySegment receiver,
                                            int events) {
        try {
            return (int) MH_POLLER_MODIFY_RECEIVER.invokeExact(poller,
                receiver, (short) events);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_poller_modify_receiver failed",
              t);
        }
    }

    public static int pollerModifyFd(MemorySegment poller, int fd, int events) {
        try {
            return (int) MH_POLLER_MODIFY_FD.invokeExact(poller, fd,
                (short) events);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_poller_modify_fd failed", t);
        }
    }

    public static int pollerRemove(MemorySegment poller, MemorySegment socket) {
        try {
            return (int) MH_POLLER_REMOVE.invokeExact(poller, socket);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_poller_remove failed", t);
        }
    }

    public static int pollerRemoveSpotSub(MemorySegment poller,
                                           MemorySegment spotSub) {
        try {
            return (int) MH_POLLER_REMOVE_SPOT_SUB.invokeExact(poller, spotSub);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_poller_remove_spot_sub failed",
              t);
        }
    }

    public static int pollerRemoveSpotPub(MemorySegment poller,
                                           MemorySegment spotPub) {
        try {
            return (int) MH_POLLER_REMOVE_SPOT_PUB.invokeExact(poller, spotPub);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_poller_remove_spot_pub failed",
              t);
        }
    }

    public static int pollerRemoveReceiver(MemorySegment poller,
                                            MemorySegment receiver) {
        try {
            return (int) MH_POLLER_REMOVE_RECEIVER.invokeExact(poller,
                receiver);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_poller_remove_receiver failed",
              t);
        }
    }

    public static int pollerRemoveFd(MemorySegment poller, int fd) {
        try {
            return (int) MH_POLLER_REMOVE_FD.invokeExact(poller, fd);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_poller_remove_fd failed", t);
        }
    }

    public static int pollerRemoveTimer(MemorySegment poller,
                                        MemorySegment timer) {
        try {
            return (int) MH_POLLER_REMOVE_TIMER.invokeExact(poller, timer);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_poller_remove_timer failed", t);
        }
    }

    public static int pollerWaitAll(MemorySegment poller, MemorySegment events,
                                    int count, int timeoutMs) {
        if (events == null || events.address() == 0 || count <= 0)
            return 0;
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment errorOut = arena.allocate(ValueLayout.JAVA_INT);
            errorOut.set(ValueLayout.JAVA_INT, 0, 0);
            int rc = (int) MH_POLLER_WAIT_ALL.invokeExact(poller, events, count,
                (long) timeoutMs, errorOut);
            if (rc < 0) {
                int error = errorOut.get(ValueLayout.JAVA_INT, 0);
                if (error != 0) {
                    throw new systems.zlink.ConfigException(
                        systems.zlink.ConfigResult.fromValue(error),
                        errno());
                }
            }
            return rc;
        } catch (RuntimeException ex) {
            throw ex;
        } catch (Throwable t) {
            throw new RuntimeException("zlink_poller_wait_all failed", t);
        }
    }

    public static int pollerWait(MemorySegment poller, MemorySegment event,
                                 int timeoutMs) {
        if (event == null || event.address() == 0)
            return 0;
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment errorOut = arena.allocate(ValueLayout.JAVA_INT);
            errorOut.set(ValueLayout.JAVA_INT, 0, 0);
            int rc = (int) MH_POLLER_WAIT.invokeExact(poller, event,
                (long) timeoutMs, errorOut);
            if (rc < 0) {
                int error = errorOut.get(ValueLayout.JAVA_INT, 0);
                if (error != 0) {
                    throw new systems.zlink.ConfigException(
                        systems.zlink.ConfigResult.fromValue(error),
                        errno());
                }
            }
            return rc;
        } catch (RuntimeException ex) {
            throw ex;
        } catch (Throwable t) {
            throw new RuntimeException("zlink_poller_wait failed", t);
        }
    }

    public static MemorySegment registryNew(MemorySegment ctx) {
        try {
            return (MemorySegment) MH_REG_NEW.invokeExact(ctx);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_registry_new failed", t);
        }
    }

    public static int registryBind(MemorySegment reg, MemorySegment pub,
                                   MemorySegment router) {
        try {
            return (int) MH_REG_BIND.invokeExact(reg, pub, router);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_registry_bind failed", t);
        }
    }

    public static int registrySetOption(MemorySegment reg, int option, int value) {
        try {
            return (int) MH_REG_SET.invokeExact(reg, option, value);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_registry_set failed", t);
        }
    }

    public static int registryGetOption(MemorySegment reg, int option,
                                        MemorySegment valueOut,
                                        MemorySegment errorOut) {
        try {
            return (int) MH_REG_GET.invokeExact(reg, option, valueOut, errorOut);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_registry_get failed", t);
        }
    }

    public static int registryAddPeer(MemorySegment reg, MemorySegment peer) {
        try {
            return (int) MH_REG_ADD_PEER.invokeExact(reg, peer);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_registry_add_peer failed", t);
        }
    }

    public static int registryDestroy(MemorySegment regPtr) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment p = arena.allocate(ValueLayout.ADDRESS);
            p.set(ValueLayout.ADDRESS, 0, regPtr);
            return (int) MH_REG_DESTROY.invokeExact(p);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_registry_destroy failed", t);
        }
    }

    public static int registryStatusSnapshot(MemorySegment registry,
                                             MemorySegment out) {
        try {
            return (int) MH_REG_STATUS_SNAPSHOT.invokeExact(registry, out);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_registry_status_snapshot failed",
              t);
        }
    }

    public static int registryServiceSummarySnapshot(MemorySegment registry,
                                                     MemorySegment filter,
                                                     MemorySegment entries,
                                                     MemorySegment count) {
        try {
            return (int) MH_REG_SERVICE_SUMMARY_SNAPSHOT.invokeExact(registry,
              filter, entries, count);
        } catch (Throwable t) {
            throw new RuntimeException(
              "zlink_registry_service_summary_snapshot failed", t);
        }
    }

    public static int registryMemberPeers(MemorySegment registry,
                                          MemorySegment channelName,
                                          MemorySegment entries,
                                          MemorySegment count) {
        try {
            return (int) MH_REG_MEMBER_PEERS.invokeExact(registry, channelName,
              entries, count);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_registry_member_peers failed", t);
        }
    }

    public static int registryTopologySnapshot(MemorySegment registry,
                                               MemorySegment entries,
                                               MemorySegment count) {
        try {
            return (int) MH_REG_TOPOLOGY_SNAPSHOT.invokeExact(registry, entries,
              count);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_registry_topology_snapshot failed",
              t);
        }
    }

    public static int registryTopologyQuery(MemorySegment registry,
                                            MemorySegment filter,
                                            MemorySegment entries,
                                            MemorySegment count) {
        try {
            return (int) MH_REG_TOPOLOGY_QUERY.invokeExact(registry, filter,
              entries, count);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_registry_topology_query failed",
              t);
        }
    }

    public static MemorySegment registryQueryClientNew(MemorySegment ctx) {
        try {
            return (MemorySegment) MH_REG_QUERY_CLIENT_NEW.invokeExact(ctx);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_registry_query_client_new failed",
              t);
        }
    }

    public static int registryQueryClientConnect(MemorySegment client,
                                                 MemorySegment endpoint) {
        try {
            return (int) MH_REG_QUERY_CLIENT_CONNECT.invokeExact(client,
              endpoint);
        } catch (Throwable t) {
            throw new RuntimeException(
              "zlink_registry_query_client_connect failed", t);
        }
    }

    public static int registryQuerySnapshot(MemorySegment client,
                                            MemorySegment filter,
                                            MemorySegment entries,
                                            MemorySegment count) {
        try {
            return (int) MH_REG_QUERY_SNAPSHOT.invokeExact(client, filter,
              entries, count);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_registry_query_snapshot failed",
              t);
        }
    }

    public static int registryQueryDestroy(MemorySegment clientPtr) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment holder = arena.allocate(ValueLayout.ADDRESS);
            holder.set(ValueLayout.ADDRESS, 0, clientPtr);
            return (int) MH_REG_QUERY_DESTROY.invokeExact(holder);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_registry_query_destroy failed",
              t);
        }
    }

    public static MemorySegment discoveryNewFixed(MemorySegment ctx,
                                                  int autoConnectType,
                                                  MemorySegment channelName) {
        try {
            return (MemorySegment) MH_DISC_NEW_FIXED.invokeExact(ctx,
              autoConnectType, channelName);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_discovery_new failed", t);
        }
    }

    public static int discoveryConnectRegistry(MemorySegment disc, MemorySegment pub) {
        try {
            return (int) MH_DISC_CONNECT.invokeExact(disc, pub);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_discovery_connect_registry failed", t);
        }
    }

    public static int discoveryResolveSpot(MemorySegment discovery,
                                           MemorySegment spotRid,
                                           MemorySegment ownerNodeRidOut) {
        try {
            return (int) MH_DISC_RESOLVE_SPOT.invokeExact(discovery, spotRid,
              ownerNodeRidOut);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_discovery_resolve_spot failed",
              t);
        }
    }

    public static int discoveryResolveActor(MemorySegment discovery,
                                            MemorySegment actorId,
                                            MemorySegment routeOut) {
        try {
            return (int) MH_DISC_RESOLVE_ACTOR.invokeExact(discovery, actorId,
              routeOut);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_discovery_resolve_actor failed",
              t);
        }
    }

    public static int discoverySetValue(MemorySegment disc, long value) {
        try {
            return (int) MH_DISC_SET_VALUE.invokeExact(disc, value);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_discovery_set_value failed", t);
        }
    }

    public static int discoveryGetValue(MemorySegment disc,
                                        MemorySegment valueOut) {
        try {
            return (int) MH_DISC_GET_VALUE.invokeExact(disc, valueOut);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_discovery_get_value failed", t);
        }
    }

    public static int discoveryDestroy(MemorySegment discPtr) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment p = arena.allocate(ValueLayout.ADDRESS);
            p.set(ValueLayout.ADDRESS, 0, discPtr);
            return (int) MH_DISC_DESTROY.invokeExact(p);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_discovery_destroy failed", t);
        }
    }

    public static int discoveryMemberPeers(MemorySegment discovery,
                                           MemorySegment entries,
                                           MemorySegment count) {
        try {
            return (int) MH_DISC_MEMBER_PEERS.invokeExact(discovery, entries,
              count);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_discovery_member_peers failed",
              t);
        }
    }

    public static MemorySegment providerNew(MemorySegment ctx, MemorySegment routingId) {
        try {
            return (MemorySegment) MH_PROVIDER_NEW.invokeExact(ctx, routingId);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_receiver_new failed", t);
        }
    }

    public static int providerBind(MemorySegment p, MemorySegment ep) {
        try {
            return (int) MH_PROVIDER_BIND.invokeExact(p, ep);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_receiver_bind failed", t);
        }
    }

    public static int providerConnectRegistry(MemorySegment p, MemorySegment ep) {
        try {
            return (int) MH_PROVIDER_CONN.invokeExact(p, ep);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_receiver_connect_registry failed", t);
        }
    }

    public static int providerRegister(MemorySegment p, MemorySegment service, MemorySegment ep, int weight) {
        try {
            return (int) MH_PROVIDER_REG.invokeExact(p, service, ep, weight);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_receiver_register failed", t);
        }
    }

    public static int providerUpdateWeight(MemorySegment p, MemorySegment service, int weight) {
        try {
            return (int) MH_PROVIDER_UPD.invokeExact(p, service, weight);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_receiver_update_weight failed", t);
        }
    }

    public static int providerUnregister(MemorySegment p, MemorySegment service) {
        try {
            return (int) MH_PROVIDER_UNREG.invokeExact(p, service);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_receiver_unregister failed", t);
        }
    }

    public static int providerRegisterResult(MemorySegment p, MemorySegment service, MemorySegment status, MemorySegment resolved, MemorySegment error) {
        try {
            return (int) MH_PROVIDER_RESULT.invokeExact(p, service, status, resolved, error);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_receiver_register_result failed", t);
        }
    }

    public static int providerSetTlsServer(MemorySegment p, MemorySegment cert, MemorySegment key) {
        try {
            return (int) MH_PROVIDER_TLS.invokeExact(p, cert, key);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_receiver_set_tls_server failed", t);
        }
    }

    public static int providerRecv(MemorySegment p, MemorySegment parts,
                                   MemorySegment partCount, int flags,
                                   MemorySegment routingId) {
        try {
            return (int) MH_PROVIDER_RECV.invokeExact(p, parts, partCount, flags,
              routingId);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_receiver_recv failed", t);
        }
    }

    public static int providerLastEndpoint(MemorySegment p,
                                           MemorySegment endpoint,
                                           MemorySegment size) {
        try {
            return (int) MH_PROVIDER_LAST_ENDPOINT.invokeExact(p, endpoint, size);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_receiver_last_endpoint failed", t);
        }
    }

    public static int providerPeerInfo(MemorySegment p, MemorySegment routingId,
                                       MemorySegment info) {
        try {
            return (int) MH_PROVIDER_PEER_INFO.invokeExact(p, routingId, info);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_receiver_peer_info failed", t);
        }
    }

    public static int providerRouterPeers(MemorySegment p, MemorySegment peers,
                                          MemorySegment count) {
        try {
            return (int) MH_PROVIDER_ROUTER_PEERS.invokeExact(p, peers, count);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_receiver_router_peers failed", t);
        }
    }

    public static int providerDestroy(MemorySegment pPtr) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment p = arena.allocate(ValueLayout.ADDRESS);
            p.set(ValueLayout.ADDRESS, 0, pPtr);
            return (int) MH_PROVIDER_DESTROY.invokeExact(p);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_receiver_destroy failed", t);
        }
    }

    public static int providerSetOption(MemorySegment p, int option,
                                        MemorySegment value, long len) {
        try {
            return (int) MH_PROVIDER_SET_OPTION.invokeExact(p, option, value, len);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_receiver_set_option failed", t);
        }
    }

    public static int registrySetSockOpt(MemorySegment r, int role, int option, MemorySegment value, long len) {
        try {
            return (int) MH_REGISTRY_SETSOCKOPT.invokeExact(r, role, option, value, len);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_registry_setsockopt failed", t);
        }
    }

    public static int providerSetRoutingId(MemorySegment p, MemorySegment value,
                                           long len) {
        try {
            return (int) MH_PROVIDER_SET_ROUTING_ID.invokeExact(p, value, len);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_receiver_set_routing_id failed", t);
        }
    }

    public static int providerRoutingId(MemorySegment p, MemorySegment routingId) {
        try {
            return (int) MH_PROVIDER_ROUTING_ID.invokeExact(p, routingId);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_receiver_routing_id failed", t);
        }
    }

    public static MemorySegment spotNodeNew(MemorySegment ctx) {
        return spotNodeNew(ctx, MemorySegment.NULL);
    }

    public static MemorySegment spotNodeNew(MemorySegment ctx,
                                            MemorySegment options) {
        try {
            return (MemorySegment) MH_SPOT_NODE_NEW.invokeExact(ctx,
                options);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_new failed", t);
        }
    }

    public static MemorySegment spotNew(MemorySegment node) {
        try {
            return (MemorySegment) MH_SPOT_NEW.invokeExact(node);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_new failed", t);
        }
    }

    public static int spotDestroy(MemorySegment spotPtr) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment holder = arena.allocate(ValueLayout.ADDRESS);
            holder.set(ValueLayout.ADDRESS, 0, spotPtr);
            return (int) MH_SPOT_DESTROY.invokeExact(holder);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_destroy failed", t);
        }
    }

    public static int spotNodeDestroy(MemorySegment nodePtr) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment p = arena.allocate(ValueLayout.ADDRESS);
            p.set(ValueLayout.ADDRESS, 0, nodePtr);
            return (int) MH_SPOT_NODE_DESTROY.invokeExact(p);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_destroy failed", t);
        }
    }

    public static int spotNodeEntrySpot(MemorySegment node,
                                        MemorySegment spotOut) {
        try {
            return (int) MH_SPOT_NODE_ENTRY_SPOT.invokeExact(node, spotOut);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_entry_spot failed", t);
        }
    }

    public static int spotNodeSpotLookup(MemorySegment node,
                                         MemorySegment spotRid,
                                         MemorySegment spotOut) {
        try {
            return (int) MH_SPOT_NODE_SPOT_LOOKUP.invokeExact(node, spotRid,
              spotOut);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_spot_lookup failed", t);
        }
    }

    public static int spotNodeBind(MemorySegment node, MemorySegment ep) {
        try {
            return (int) MH_SPOT_NODE_BIND.invokeExact(node, ep);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_bind failed", t);
        }
    }

    public static int spotNodeConnectPeer(MemorySegment node, MemorySegment ep) {
        try {
            return (int) MH_SPOT_NODE_CONN_PEER.invokeExact(node, ep);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_connect_peer failed", t);
        }
    }

    public static int spotNodeDisconnectPeer(MemorySegment node, MemorySegment ep) {
        try {
            return (int) MH_SPOT_NODE_DISC_PEER.invokeExact(node, ep);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_disconnect_peer failed", t);
        }
    }

    public static int spotNodeDisconnectPeerRid(MemorySegment node,
                                                MemorySegment rid) {
        try {
            return (int) MH_SPOT_NODE_DISC_PEER_RID.invokeExact(node, rid);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_disconnect_peer_rid failed", t);
        }
    }

    public static int spotNodeActorNew(MemorySegment node,
                                       MemorySegment actorId,
                                       MemorySegment out) {
        try {
            return (int) MH_SPOT_NODE_ACTOR_NEW.invokeExact(node, actorId, out);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_actor_new failed", t);
        }
    }

    public static int spotNodeActorDestroy(MemorySegment node,
                                           MemorySegment actor,
                                           int timeoutMs) {
        try {
            return (int) MH_SPOT_NODE_ACTOR_DESTROY.invokeExact(node, actor,
              timeoutMs);
        } catch (Throwable t) {
            throw new RuntimeException(
              "zlink_spot_node_actor_destroy failed", t);
        }
    }

    public static int spotNodeActorLookup(MemorySegment node,
                                          MemorySegment actorId,
                                          MemorySegment out) {
        try {
            return (int) MH_SPOT_NODE_ACTOR_LOOKUP.invokeExact(node, actorId,
              out);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_actor_lookup failed", t);
        }
    }

    public static int remoteActorGetRef(MemorySegment targetNodeRid,
                                        MemorySegment actorId,
                                        MemorySegment out) {
        try {
            return (int) MH_REMOTE_ACTOR_GET_REF.invokeExact(targetNodeRid,
              actorId, out);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_remote_actor_get_ref failed", t);
        }
    }

    public static int spotNodeCreateRemoteActor(MemorySegment node,
                                                MemorySegment targetNodeRid,
                                                MemorySegment actorId,
                                                MemorySegment parts,
                                                long partCount,
                                                MemorySegment out,
                                                int timeoutMs) {
        try {
            return (int) MH_SPOT_NODE_CREATE_REMOTE_ACTOR.invokeExact(node,
              targetNodeRid, actorId, parts, partCount, out, timeoutMs);
        } catch (Throwable t) {
            throw new RuntimeException(
              "zlink_spot_node_create_remote_actor failed", t);
        }
    }

    public static int spotNodeActorAdmissionHandler(MemorySegment node,
                                                    MemorySegment handler,
                                                    MemorySegment userdata) {
        try {
            return (int) MH_SPOT_NODE_ACTOR_ADMISSION_HANDLER.invokeExact(node,
              handler, userdata);
        } catch (Throwable t) {
            throw new RuntimeException(
              "zlink_spot_node_actor_admission_handler failed", t);
        }
    }

    public static int spotNodeActorJoinSpot(MemorySegment node,
                                            MemorySegment actor,
                                            MemorySegment destNodeRid,
                                            MemorySegment destSpotRid,
                                            MemorySegment parts,
                                            long partCount,
                                            MemorySegment handler,
                                            MemorySegment userdata,
                                            int flags,
                                            int timeoutMs) {
        try {
            return (int) MH_SPOT_NODE_ACTOR_JOIN_SPOT.invokeExact(node, actor,
              destNodeRid, destSpotRid, parts, partCount, handler, userdata, flags,
              timeoutMs);
        } catch (Throwable t) {
            throw new RuntimeException(
              "zlink_spot_node_actor_join_spot failed", t);
        }
    }

    public static int spotNodeActorLeaveSpot(MemorySegment node,
                                             MemorySegment actor,
                                             MemorySegment destSpotRid,
                                             int timeoutMs) {
        try {
            return (int) MH_SPOT_NODE_ACTOR_LEAVE_SPOT.invokeExact(node, actor,
              destSpotRid, timeoutMs);
        } catch (Throwable t) {
            throw new RuntimeException(
              "zlink_spot_node_actor_leave_spot failed", t);
        }
    }

    public static int spotNodeActorRecvPart(MemorySegment node,
                                            MemorySegment actor,
                                            MemorySegment infoOut,
                                            MemorySegment messageOut,
                                            MemorySegment hasMoreOut,
                                            int flags) {
        try {
            return (int) MH_SPOT_NODE_ACTOR_RECV_PART.invokeExact(node, actor,
              infoOut, messageOut, hasMoreOut, flags);
        } catch (Throwable t) {
            throw new RuntimeException(
              "zlink_spot_node_actor_recv_part failed", t);
        }
    }

    public static int spotNodeActorSendBoundSessionMsg(MemorySegment node,
                                                       MemorySegment actor,
                                                       MemorySegment message,
                                                       int flags) {
        try {
            return (int) MH_SPOT_NODE_ACTOR_SEND_BOUND_SESSION_MSG.invokeExact(
              node, actor, message, flags);
        } catch (Throwable t) {
            throw new RuntimeException(
              "zlink_spot_node_actor_send_bound_session_msg failed", t);
        }
    }

    public static int spotNodeActorCloseBoundSession(MemorySegment node,
                                                     MemorySegment actor,
                                                     int timeoutMs) {
        try {
            return (int) MH_SPOT_NODE_ACTOR_CLOSE_BOUND_SESSION.invokeExact(
              node, actor, timeoutMs);
        } catch (Throwable t) {
            throw new RuntimeException(
              "zlink_spot_node_actor_close_bound_session failed", t);
        }
    }

    public static int spotNodeRegister(MemorySegment node, MemorySegment service,
                                       MemorySegment ep) {
        try {
            return (int) MH_SPOT_NODE_REG.invokeExact(node, service, ep);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_register failed", t);
        }
    }

    public static int spotNodeUnregister(MemorySegment node,
                                         MemorySegment service) {
        try {
            return (int) MH_SPOT_NODE_UNREG.invokeExact(node, service);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_unregister failed", t);
        }
    }

    public static int spotNodeSetDiscovery(MemorySegment node,
                                           MemorySegment disc,
                                           MemorySegment service) {
        try {
            return (int) MH_SPOT_NODE_SET_DISC.invokeExact(node, disc, service);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_set_discovery failed",
              t);
        }
    }

    public static int spotNodeSetTlsServer(MemorySegment node,
                                           MemorySegment cert,
                                           MemorySegment key) {
        try {
            return (int) MH_SET_TLS_SRV.invokeExact(node, cert, key, 0);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_set_tls_server failed",
              t);
        }
    }

    public static int spotNodeSetTlsClient(MemorySegment node,
                                           MemorySegment ca,
                                           MemorySegment host,
                                           int trust) {
        try {
            return (int) MH_SET_TLS_CLI.invokeExact(node, ca, host,
              trust);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_set_tls_client failed",
              t);
        }
    }

    public static int spotNodeAttachDiscovery(MemorySegment node,
                                              MemorySegment discovery) {
        try {
            return (int) MH_SPOT_NODE_ATTACH_DISCOVERY.invokeExact(node,
              discovery);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_attach_discovery failed",
              t);
        }
    }

    public static int spotNodeAttachChannelDealer(MemorySegment node,
                                                  MemorySegment discovery,
                                                  MemorySegment dealer) {
        try {
            return (int) MH_SPOT_NODE_ATTACH_CHANNEL_DEALER.invokeExact(node,
              discovery, dealer);
        } catch (Throwable t) {
            throw new RuntimeException(
              "zlink_spot_node_attach_channel_dealer failed",
              t);
        }
    }

    public static int spotNodeAttachChannelDealerManual(MemorySegment node,
                                                        MemorySegment channelName,
                                                        MemorySegment dealer) {
        try {
            return (int) MH_SPOT_NODE_ATTACH_CHANNEL_DEALER_MANUAL.invokeExact(
              node, channelName, dealer);
        } catch (Throwable t) {
            throw new RuntimeException(
              "zlink_spot_node_attach_channel_dealer_manual failed",
              t);
        }
    }

    public static int spotNodeAttachPubIngress(MemorySegment node,
                                               MemorySegment pub) {
        try {
            return (int) MH_SPOT_NODE_ATTACH_PUB_INGRESS.invokeExact(node, pub);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_attach_pub_ingress failed",
              t);
        }
    }

    public static int spotNodeStatusSnapshot(MemorySegment node,
                                             MemorySegment out) {
        try {
            return (int) MH_SPOT_NODE_STATUS_SNAPSHOT.invokeExact(node, out);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_status_snapshot failed",
              t);
        }
    }

    public static int spotNodePeersSnapshot(MemorySegment node,
                                            MemorySegment entries,
                                            MemorySegment count) {
        try {
            return (int) MH_SPOT_NODE_PEERS_SNAPSHOT.invokeExact(node, entries,
              count);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_peers_snapshot failed",
              t);
        }
    }

    public static int spotNodePeersQuery(MemorySegment node,
                                         MemorySegment filter,
                                         MemorySegment entries,
                                         MemorySegment count) {
        try {
            return (int) MH_SPOT_NODE_PEERS_QUERY.invokeExact(node, filter,
              entries, count);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_peers_query failed", t);
        }
    }

    public static int spotNodeSubjectsSnapshot(MemorySegment node,
                                               MemorySegment filter,
                                               MemorySegment entries,
                                               MemorySegment count) {
        try {
            return (int) MH_SPOT_NODE_SUBJECTS_SNAPSHOT.invokeExact(node, filter,
              entries, count);
        } catch (Throwable t) {
            throw new RuntimeException(
              "zlink_spot_node_subjects_snapshot failed", t);
        }
    }

    public static int spotNodeInternalSocketsSnapshot(MemorySegment node,
                                                      MemorySegment filter,
                                                      MemorySegment entries,
                                                      MemorySegment count) {
        try {
            return (int) MH_SPOT_NODE_INTERNAL_SOCKETS_SNAPSHOT.invokeExact(
              node, filter, entries, count);
        } catch (Throwable t) {
            throw new RuntimeException(
              "zlink_spot_node_internal_sockets_snapshot failed", t);
        }
    }

    public static int spotNodeSpotsSnapshot(MemorySegment node,
                                            MemorySegment entries,
                                            MemorySegment count) {
        try {
            return (int) MH_SPOT_NODE_SPOTS_SNAPSHOT.invokeExact(node, entries,
              count);
        } catch (Throwable t) {
            throw new RuntimeException(
              "zlink_spot_node_spots_snapshot failed", t);
        }
    }

    public static int spotNodeActorsSnapshot(MemorySegment node,
                                             MemorySegment entries,
                                             MemorySegment count) {
        try {
            return (int) MH_SPOT_NODE_ACTORS_SNAPSHOT.invokeExact(node,
              entries, count);
        } catch (Throwable t) {
            throw new RuntimeException(
              "zlink_spot_node_actors_snapshot failed", t);
        }
    }

    public static int spotPubPeers(MemorySegment pub, MemorySegment peers,
                                   MemorySegment count) {
        try {
            return (int) MH_SPOT_PUB_PEERS.invokeExact(pub, peers, count);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_pub_peers failed", t);
        }
    }

    public static int spotSubPeers(MemorySegment sub, MemorySegment peers,
                                   MemorySegment count) {
        try {
            return (int) MH_SPOT_SUB_PEERS.invokeExact(sub, peers, count);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_sub_peers failed", t);
        }
    }

    public static MemorySegment spotPubNew(MemorySegment node) {
        try {
            return (MemorySegment) MH_SPOT_PUB_NEW.invokeExact(node);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_pub_new failed", t);
        }
    }

    public static int spotPubDestroy(MemorySegment pubPtr) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment holder = arena.allocate(ValueLayout.ADDRESS);
            holder.set(ValueLayout.ADDRESS, 0, pubPtr);
            return (int) MH_SPOT_PUB_DESTROY.invokeExact(holder);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_pub_destroy failed", t);
        }
    }

    public static int spotPubPublish(MemorySegment pub, MemorySegment topic, MemorySegment parts, long count, int flags) {
        try {
            return (int) MH_SPOT_PUB_PUBLISH.invokeExact(pub, topic, parts, count, flags);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_pub_publish failed", t);
        }
    }

    public static int spotSendChannelPart(MemorySegment spot,
                                          MemorySegment channelName,
                                          MemorySegment part,
                                          int flags,
                                          int partFlag) {
        try {
            return (int) MH_SPOT_SEND_CHANNEL_PART.invokeExact(spot,
              channelName, part, flags, partFlag);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_send_channel_part failed", t);
        }
    }

    public static int spotPublishPart(MemorySegment spot,
                                      MemorySegment topicId,
                                      MemorySegment part,
                                      int flags,
                                      int partFlag) {
        try {
            return (int) MH_SPOT_PUBLISH_PART.invokeExact(spot, topicId, part,
              flags, partFlag);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_publish_part failed", t);
        }
    }

    public static MemorySegment spotSubNew(MemorySegment node) {
        try {
            return (MemorySegment) MH_SPOT_SUB_NEW.invokeExact(node);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_sub_new failed", t);
        }
    }

    public static int spotSubDestroy(MemorySegment subPtr) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment holder = arena.allocate(ValueLayout.ADDRESS);
            holder.set(ValueLayout.ADDRESS, 0, subPtr);
            return (int) MH_SPOT_SUB_DESTROY.invokeExact(holder);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_sub_destroy failed", t);
        }
    }

    public static int spotSubSubscribe(MemorySegment sub, MemorySegment topic) {
        try {
            return (int) MH_SPOT_SUB_SUBSCRIBE.invokeExact(sub, topic);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_sub_subscribe failed", t);
        }
    }

    public static int spotSubSubscribePattern(MemorySegment sub, MemorySegment pattern) {
        try {
            return (int) MH_SPOT_SUB_SUBSCRIBE_PATTERN.invokeExact(sub, pattern);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_sub_subscribe_pattern failed", t);
        }
    }

    public static int spotSubUnsubscribe(MemorySegment sub, MemorySegment topic) {
        try {
            return (int) MH_SPOT_SUB_UNSUBSCRIBE.invokeExact(sub, topic);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_sub_unsubscribe failed", t);
        }
    }

    public static int spotSubRecv(MemorySegment sub, MemorySegment partsPtr, MemorySegment count, int flags, MemorySegment topicOut, MemorySegment topicLen) {
        try {
            return (int) MH_SPOT_SUB_RECV.invokeExact(sub, partsPtr, count, flags, topicOut, topicLen);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_sub_recv failed", t);
        }
    }

    public static int spotSubscribePart(MemorySegment spot,
                                        MemorySegment sourceRidOut,
                                        MemorySegment topicIdOut,
                                        long topicIdCapacity,
                                        MemorySegment topicIdLenOut,
                                        MemorySegment partOut,
                                        MemorySegment hasMoreOut,
                                        int flags) {
        try {
            return (int) MH_SPOT_SUBSCRIBE_PART.invokeExact(spot,
              sourceRidOut, topicIdOut, topicIdCapacity, topicIdLenOut, partOut,
              hasMoreOut, flags);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_subscribe_part failed", t);
        }
    }

    public static int spotRequestChannelPart(MemorySegment spot,
                                             MemorySegment channelName,
                                             MemorySegment part,
                                             MemorySegment handler,
                                             MemorySegment userdata,
                                             int flags,
                                             int partFlag,
                                             int timeoutMs) {
        try {
            return (int) MH_SPOT_REQUEST_CHANNEL_PART.invokeExact(spot,
              channelName, part, handler, userdata, flags, partFlag,
              timeoutMs);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_request_channel_part failed",
              t);
        }
    }

    public static int spotRequestSpotPart(MemorySegment spot,
                                          MemorySegment destNodeRid,
                                          MemorySegment destSpotRid,
                                          MemorySegment part,
                                          MemorySegment handler,
                                          MemorySegment userdata,
                                          int flags,
                                          int partFlag,
                                          int timeoutMs) {
        try {
            return (int) MH_SPOT_REQUEST_SPOT_PART.invokeExact(spot,
              destNodeRid, destSpotRid, part, handler, userdata, flags,
              partFlag, timeoutMs);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_request_spot_part failed",
              t);
        }
    }

    public static int spotRequestRouterPart(MemorySegment spot,
                                            MemorySegment peerRid,
                                            MemorySegment part,
                                            MemorySegment handler,
                                            MemorySegment userdata,
                                            int flags,
                                            int partFlag,
                                            int timeoutMs) {
        try {
            return (int) MH_SPOT_REQUEST_ROUTER_PART.invokeExact(spot,
              peerRid, part, handler, userdata, flags, partFlag, timeoutMs);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_request_router_part failed",
              t);
        }
    }

    public static int socketRequestProgressInternal(MemorySegment socket) {
        try {
            return (int) MH_SOCKET_REQUEST_PROGRESS_INTERNAL.invokeExact(socket);
        } catch (Throwable t) {
            throw new RuntimeException(
              "zlink_socket_request_progress_internal failed", t);
        }
    }

    public static int spotRequestProgressInternal(MemorySegment spot) {
        try {
            return (int) MH_SPOT_REQUEST_PROGRESS_INTERNAL.invokeExact(spot);
        } catch (Throwable t) {
            throw new RuntimeException(
              "zlink_spot_request_progress_internal failed", t);
        }
    }

    public static int spotChannelReplyProgressFrom(MemorySegment spot,
                                                   MemorySegment subject) {
        try {
            return (int) MH_SPOT_CHANNEL_REPLY_PROGRESS_FROM.invokeExact(spot,
                subject);
        } catch (Throwable t) {
            throw new RuntimeException(
              "zlink_spot_channel_reply_progress_from failed", t);
        }
    }

    public static int spotActorJoinRecv(MemorySegment spot,
                                        MemorySegment infoOut,
                                        MemorySegment partsOut,
                                        MemorySegment partCountOut,
                                        int flags) {
        try {
            return (int) MH_SPOT_ACTOR_JOIN_RECV.invokeExact(spot, infoOut,
              partsOut, partCountOut, flags);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_actor_join_recv failed", t);
        }
    }

    public static int spotActorJoinReply(MemorySegment spot,
                                         MemorySegment info,
                                         int accepted,
                                         MemorySegment parts,
                                         long partCount) {
        try {
            return (int) MH_SPOT_ACTOR_JOIN_REPLY.invokeExact(spot, info,
              accepted, parts, partCount);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_actor_join_reply failed", t);
        }
    }

    public static int spotActorsSnapshot(MemorySegment spot,
                                         MemorySegment entries,
                                         MemorySegment count) {
        try {
            return (int) MH_SPOT_ACTORS_SNAPSHOT.invokeExact(spot, entries,
              count);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_actors_snapshot failed", t);
        }
    }

}
