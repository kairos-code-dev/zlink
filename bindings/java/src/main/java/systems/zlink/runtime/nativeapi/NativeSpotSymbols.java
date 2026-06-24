/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.nativeapi;

import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;

final class NativeSpotSymbols {
    static final MethodHandle MH_SPOT_SEND_CHANNEL_PART = downcall(
      "zlink_spot_send_channel_part",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT,
        ValueLayout.JAVA_INT));
    static final MethodHandle MH_SPOT_SEND_SPOT_PART = downcall(
      "zlink_spot_send_spot_part",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS,
        ValueLayout.JAVA_INT, ValueLayout.JAVA_INT));
    static final MethodHandle MH_SPOT_REPLY_SPOT_PART = downcall(
      "zlink_spot_reply_spot_part",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG,
        ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    static final MethodHandle MH_SPOT_REPLY_ROUTER_PART = downcall(
      "zlink_spot_reply_router_part",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.JAVA_LONG, ValueLayout.ADDRESS,
        ValueLayout.JAVA_INT));
    static final MethodHandle MH_SPOT_PUBLISH_PART = downcall(
      "zlink_spot_publish_part",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT,
        ValueLayout.JAVA_INT));
    static final MethodHandle MH_SPOT_SUBSCRIBE_PART = downcall(
      "zlink_spot_subscribe_part",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS,
        ValueLayout.JAVA_INT));
    // DONT_WAIT-only critical variant for the spot subscribe hot path
    // (parity with MH_SUBSCRIBE_PART_CRITICAL).
    static final MethodHandle MH_SPOT_SUBSCRIBE_PART_CRITICAL =
      downcallCritical("zlink_spot_subscribe_part",
        FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
          ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG,
          ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS,
          ValueLayout.JAVA_INT));
    static final MethodHandle MH_SPOT_REQUEST_CHANNEL_PART = downcall(
      "zlink_spot_request_channel_part",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.JAVA_INT, ValueLayout.JAVA_INT,
        ValueLayout.JAVA_INT));
    static final MethodHandle MH_SPOT_REQUEST_SPOT_PART = downcall(
      "zlink_spot_request_spot_part",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT,
        ValueLayout.JAVA_INT, ValueLayout.JAVA_INT));
    static final MethodHandle MH_SPOT_REQUEST_ROUTER_PART = downcall(
      "zlink_spot_request_router_part",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.JAVA_INT, ValueLayout.JAVA_INT,
        ValueLayout.JAVA_INT));
    static final MethodHandle MH_SPOT_DISPATCH_EVENT_HANDLER = downcall(
      "zlink_spot_dispatch_event_handler",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    static final MethodHandle MH_SPOT_RECV_PART = downcall(
      "zlink_spot_recv_part",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    static final MethodHandle MH_SPOT_ACTOR_JOIN_RECV = downcall(
      "zlink_spot_actor_join_recv",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS,
        ValueLayout.JAVA_INT));
    static final MethodHandle MH_SPOT_RECV_ACTOR_LIFECYCLE = downcall(
      "zlink_spot_recv_actor_lifecycle",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    static final MethodHandle MH_SPOT_ACTOR_JOIN_REPLY = downcall(
      "zlink_spot_actor_join_reply",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.JAVA_LONG));
    static final MethodHandle MH_SPOT_ACTORS_SNAPSHOT = downcall(
      "zlink_spot_actors",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    static final MethodHandle MH_SPOT_NODE_NEW = downcall("zlink_spot_node_new",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                ValueLayout.ADDRESS));
    static final MethodHandle MH_SPOT_NEW = downcall("zlink_spot_new",
      FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    static final MethodHandle MH_SPOT_DESTROY = downcall("zlink_spot_destroy",
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    static final MethodHandle MH_SPOT_NODE_DESTROY = downcall("zlink_spot_node_destroy",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    static final MethodHandle MH_SPOT_NODE_ENTRY_SPOT = downcall(
            "zlink_spot_node_entry_spot",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    static final MethodHandle MH_SPOT_NODE_SPOT_LOOKUP = downcall(
            "zlink_spot_node_spot_lookup",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    static final MethodHandle MH_SPOT_NODE_SPOT_GET_OR_NEW = downcall(
            "zlink_spot_node_spot_get_or_new",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    static final MethodHandle MH_SPOT_NODE_SET_PUB_BIND = downcall("zlink_spot_node_set_pub_bind",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    static final MethodHandle MH_SPOT_NODE_SET_ROUTER_BIND = downcall(
            "zlink_spot_node_set_router_bind",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    static final MethodHandle MH_SPOT_NODE_CONN_PEER = downcall("zlink_spot_node_connect_peer",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    static final MethodHandle MH_SPOT_NODE_CONN_PEER_RID = downcall("zlink_spot_node_connect_peer_rid",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    static final MethodHandle MH_SPOT_NODE_DISC_PEER = downcall("zlink_spot_node_disconnect_peer",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    static final MethodHandle MH_SPOT_NODE_DISC_PEER_RID = downcall("zlink_spot_node_disconnect_peer_rid",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    static final MethodHandle MH_SPOT_NODE_ACTOR_NEW = downcall(
            "zlink_spot_node_actor_new",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    static final MethodHandle MH_SPOT_NODE_ACTOR_DESTROY = downcall(
            "zlink_spot_node_actor_destroy",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    static final MethodHandle MH_SPOT_NODE_ACTOR_LOOKUP = downcall(
            "zlink_spot_node_actor_lookup",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    static final MethodHandle MH_REMOTE_ACTOR_GET_REF = downcall(
            "zlink_remote_actor_get_ref",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT));
    static final MethodHandle MH_SPOT_NODE_ACTOR_JOIN_SPOT = downcall(
            "zlink_spot_node_actor_join_spot",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_LONG, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT,
                    ValueLayout.JAVA_INT));
    static final MethodHandle MH_SPOT_NODE_ACTOR_JOIN_ENTRY_SPOT = downcall(
            "zlink_spot_node_actor_join_entry_spot",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.JAVA_LONG,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT,
                    ValueLayout.JAVA_INT));
    static final MethodHandle MH_SPOT_NODE_ACTOR_LEAVE_SPOT = downcall(
            "zlink_spot_node_actor_leave_spot",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT));
    static final MethodHandle MH_SPOT_NODE_ACTOR_RECV_PART = downcall(
            "zlink_spot_node_actor_recv_part",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT));
    static final MethodHandle MH_SPOT_NODE_ACTOR_SEND_BOUND_SESSION_MSG = downcall(
            "zlink_spot_node_actor_send_bound_session_msg",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT));
    static final MethodHandle MH_SPOT_NODE_ACTOR_FORWARD_BOUND_SESSION_PART = downcall(
            "zlink_spot_node_actor_forward_bound_session_part",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT, ValueLayout.JAVA_INT));
    static final MethodHandle MH_SPOT_NODE_ACTOR_CLOSE_BOUND_SESSION = downcall(
            "zlink_spot_node_actor_close_bound_session",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    static final MethodHandle MH_SPOT_NODE_ATTACH_DISCOVERY = downcall(
            "zlink_spot_node_attach_discovery",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    static final MethodHandle MH_SPOT_ROUTE_BRIDGE_NEW = downcall(
            "zlink_spot_route_bridge_new",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    static final MethodHandle MH_SPOT_ROUTE_BRIDGE_ATTACH_DEALER_CHANNEL = downcall(
            "zlink_spot_route_bridge_attach_dealer_channel",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    static final MethodHandle MH_SPOT_ROUTE_BRIDGE_ATTACH_ROUTER_CHANNEL = downcall(
            "zlink_spot_route_bridge_attach_router_channel",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    static final MethodHandle MH_SPOT_ROUTE_BRIDGE_SET_TARGET_NODE = downcall(
            "zlink_spot_route_bridge_set_target_node",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    static final MethodHandle MH_SPOT_ROUTE_BRIDGE_SEND = downcall(
            "zlink_spot_route_bridge_send",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.JAVA_LONG,
                    ValueLayout.JAVA_INT));
    static final MethodHandle MH_SPOT_ROUTE_BRIDGE_REQUEST = downcall(
            "zlink_spot_route_bridge_request",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.JAVA_LONG,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT, ValueLayout.JAVA_INT));
    static final MethodHandle MH_SPOT_ROUTE_BRIDGE_HANDLE_ROUTER_RECEIVED = downcall(
            "zlink_spot_route_bridge_handle_router_received",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.JAVA_LONG,
                    ValueLayout.ADDRESS));
    static final MethodHandle MH_SPOT_ROUTE_BRIDGE_HANDLE_ROUTER_RECEIVED_WITH_METADATA = downcall(
            "zlink_spot_route_bridge_handle_router_received_with_metadata",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_LONG, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_LONG, ValueLayout.ADDRESS));
    static final MethodHandle MH_SPOT_ROUTE_BRIDGE_DRAIN = downcall(
            "zlink_spot_route_bridge_drain",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    static final MethodHandle MH_SPOT_ROUTE_BRIDGE_CLOSE = downcall(
            "zlink_spot_route_bridge_close",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    static final MethodHandle MH_SPOT_NODE_PUBLISHER_NEW = downcall(
            "zlink_spot_node_publisher_new",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    static final MethodHandle MH_SPOT_NODE_PUBLISHER_PUBLISH = downcall(
            "zlink_spot_node_publisher_publish",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_LONG, ValueLayout.JAVA_INT));
    static final MethodHandle MH_SPOT_NODE_PUBLISHER_CLOSE = downcall(
            "zlink_spot_node_publisher_close",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    static final MethodHandle MH_SPOT_NODE_REG = downcall("zlink_spot_node_register",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    static final MethodHandle MH_SPOT_NODE_UNREG = downcall("zlink_spot_node_unregister",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    static final MethodHandle MH_SPOT_NODE_SET_DISC = downcall("zlink_spot_node_set_discovery",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    static final MethodHandle MH_SPOT_NODE_STATUS_SNAPSHOT = downcall(
            "zlink_spot_node_status",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    static final MethodHandle MH_SPOT_NODE_PEERS = downcall(
            "zlink_spot_node_peers",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    static final MethodHandle MH_SPOT_NODE_SUBJECTS_SNAPSHOT = downcall(
            "zlink_spot_node_subjects",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    static final MethodHandle MH_SPOT_NODE_INTERNAL_SOCKETS_SNAPSHOT =
        downcall("zlink_spot_node_internal_sockets",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    static final MethodHandle MH_SPOT_NODE_SPOTS_SNAPSHOT = downcall(
            "zlink_spot_node_spots",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    static final MethodHandle MH_SPOT_NODE_ACTORS_SNAPSHOT = downcall(
            "zlink_spot_node_actors",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    private NativeSpotSymbols() {
    }

    private static MethodHandle downcall(String name, FunctionDescriptor fd) {
        return NativeSymbols.downcall(name, fd);
    }

    private static MethodHandle downcallCritical(String name,
                                                 FunctionDescriptor fd) {
        return NativeSymbols.downcallCritical(name, fd);
    }
}
