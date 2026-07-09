/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.nativeapi;

import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.MemorySegment;
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
    static final MethodHandle MH_SPOT_RECV_ACTOR_LIFECYCLE_WITH_REQUEST =
      downcall("zlink_spot_recv_actor_lifecycle_with_request",
        FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
          ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS,
          ValueLayout.JAVA_INT));
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
    static final MethodHandle MH_SPOT_NODE_SET_PUB_ROUTING_ID = downcall(
            "zlink_spot_node_set_pub_routing_id",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.JAVA_LONG));
    static final MethodHandle MH_SPOT_NODE_SET_SUB_ROUTING_ID = downcall(
            "zlink_spot_node_set_sub_routing_id",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.JAVA_LONG));
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
    static final MethodHandle MH_SPOT_NODE_ACTOR_NEW_WITH_REQUEST =
      downcall("zlink_spot_node_actor_new_with_request",
        FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
          ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG,
          ValueLayout.ADDRESS));
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
    static final MethodHandle MH_SPOT_NODE_SEND_TO_ACTOR = downcall(
            "zlink_spot_node_send_to_actor",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_LONG, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT, ValueLayout.JAVA_INT));
    static final MethodHandle MH_SPOT_NODE_REQUEST_TO_ACTOR = downcall(
            "zlink_spot_node_request_to_actor",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_LONG, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.JAVA_INT,
                    ValueLayout.JAVA_INT));
    static final MethodHandle MH_SPOT_NODE_ACTOR_REPLY_NO_BIND = downcall(
            "zlink_spot_node_actor_reply_no_bind",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_LONG, ValueLayout.JAVA_INT));
    static final MethodHandle MH_SPOT_NODE_ACTOR_FORWARD_BOUND_SESSION_PART = downcall(
            "zlink_spot_node_actor_forward_bound_session_part",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT, ValueLayout.JAVA_INT));
    static final MethodHandle MH_SPOT_NODE_ACTOR_BIND_REMOTE_SESSION = downcall(
            "zlink_spot_node_actor_bind_remote_session",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    static final MethodHandle MH_SPOT_NODE_ACTOR_CLOSE_BOUND_SESSION = downcall(
            "zlink_spot_node_actor_close_bound_session",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    static final MethodHandle MH_SPOT_ROUTE_BRIDGE_NEW = downcall(
            "zlink_spot_route_bridge_new",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    static final MethodHandle MH_SPOT_ROUTE_BRIDGE_ATTACH_ROUTER_CHANNEL = downcall(
            "zlink_spot_route_bridge_attach_router_channel",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    static final MethodHandle MH_SPOT_ROUTE_BRIDGE_SEND = downcall(
            "zlink_spot_route_bridge_send",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_LONG, ValueLayout.JAVA_INT));
    static final MethodHandle MH_SPOT_ROUTE_BRIDGE_REQUEST = downcall(
            "zlink_spot_route_bridge_request",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_LONG, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.JAVA_INT,
                    ValueLayout.JAVA_INT));
    static final MethodHandle MH_SPOT_ROUTE_BRIDGE_HANDLE_ROUTER_RECEIVED = downcall(
            "zlink_spot_route_bridge_handle_router_received",
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


    static int spotReplySpotPart(MemorySegment spot,
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

    static int spotSendSpotPart(MemorySegment spot,
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

    static int spotReplyRouterPart(MemorySegment spot,
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

    static int spotDispatchEventHandler(MemorySegment spot,
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

    static int spotRecvPart(MemorySegment spot,
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

    static MemorySegment spotNodeNew(MemorySegment ctx,
                                            MemorySegment options) {
        try {
            return (MemorySegment) MH_SPOT_NODE_NEW.invokeExact(ctx,
                options);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_new failed", t);
        }
    }

    static MemorySegment spotNew(MemorySegment node) {
        try {
            return (MemorySegment) MH_SPOT_NEW.invokeExact(node);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_new failed", t);
        }
    }

    static int spotDestroy(MemorySegment spotPtr) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment holder = arena.allocate(ValueLayout.ADDRESS);
            holder.set(ValueLayout.ADDRESS, 0, spotPtr);
            return (int) MH_SPOT_DESTROY.invokeExact(holder);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_destroy failed", t);
        }
    }

    static int spotNodeDestroy(MemorySegment nodePtr) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment p = arena.allocate(ValueLayout.ADDRESS);
            p.set(ValueLayout.ADDRESS, 0, nodePtr);
            return (int) MH_SPOT_NODE_DESTROY.invokeExact(p);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_destroy failed", t);
        }
    }

    static int spotNodeEntrySpot(MemorySegment node,
                                        MemorySegment spotOut) {
        try {
            return (int) MH_SPOT_NODE_ENTRY_SPOT.invokeExact(node, spotOut);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_entry_spot failed", t);
        }
    }

    static int spotNodeSpotLookup(MemorySegment node,
                                         MemorySegment spotRid,
                                         MemorySegment spotOut) {
        try {
            return (int) MH_SPOT_NODE_SPOT_LOOKUP.invokeExact(node, spotRid,
              spotOut);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_spot_lookup failed", t);
        }
    }

    static int spotNodeSpotGetOrNew(MemorySegment node,
                                           MemorySegment spotRid,
                                           MemorySegment spotOut,
                                           MemorySegment createdOut) {
        try {
            return (int) MH_SPOT_NODE_SPOT_GET_OR_NEW.invokeExact(node, spotRid,
              spotOut, createdOut);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_spot_get_or_new failed", t);
        }
    }

    static int spotNodeSetPubBind(MemorySegment node, MemorySegment ep) {
        try {
            return (int) MH_SPOT_NODE_SET_PUB_BIND.invokeExact(node, ep);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_set_pub_bind failed", t);
        }
    }

    static int spotNodeSetRouterBind(MemorySegment node, MemorySegment ep) {
        try {
            return (int) MH_SPOT_NODE_SET_ROUTER_BIND.invokeExact(node, ep);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_set_router_bind failed", t);
        }
    }

    static int spotNodeSetPubRoutingId(MemorySegment node,
                                              MemorySegment value,
                                              long len) {
        try {
            return (int) MH_SPOT_NODE_SET_PUB_ROUTING_ID.invokeExact(
                node, value, len);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_set_pub_routing_id failed", t);
        }
    }

    static int spotNodeSetSubRoutingId(MemorySegment node,
                                              MemorySegment value,
                                              long len) {
        try {
            return (int) MH_SPOT_NODE_SET_SUB_ROUTING_ID.invokeExact(
                node, value, len);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_set_sub_routing_id failed", t);
        }
    }

    static int spotNodeConnectPeer(MemorySegment node, MemorySegment ep) {
        try {
            return (int) MH_SPOT_NODE_CONN_PEER.invokeExact(node, ep);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_connect_peer failed", t);
        }
    }

    static int spotNodeConnectPeerRid(MemorySegment node,
                                             MemorySegment rid,
                                             MemorySegment ep) {
        try {
            return (int) MH_SPOT_NODE_CONN_PEER_RID.invokeExact(node, rid, ep);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_connect_peer_rid failed", t);
        }
    }

    static int spotNodeDisconnectPeer(MemorySegment node, MemorySegment ep) {
        try {
            return (int) MH_SPOT_NODE_DISC_PEER.invokeExact(node, ep);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_disconnect_peer failed", t);
        }
    }

    static int spotNodeDisconnectPeerRid(MemorySegment node,
                                                MemorySegment rid) {
        try {
            return (int) MH_SPOT_NODE_DISC_PEER_RID.invokeExact(node, rid);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_disconnect_peer_rid failed", t);
        }
    }

    static int spotNodeActorNew(MemorySegment node,
                                       MemorySegment actorId,
                                       MemorySegment out) {
        try {
            return (int) MH_SPOT_NODE_ACTOR_NEW.invokeExact(node, actorId, out);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_actor_new failed", t);
        }
    }

    static int spotNodeActorNewWithRequest(MemorySegment node,
                                                  MemorySegment actorId,
                                                  MemorySegment parts,
                                                  long partCount,
                                                  MemorySegment out) {
        try {
            return (int) MH_SPOT_NODE_ACTOR_NEW_WITH_REQUEST.invokeExact(
              node, actorId, parts, partCount, out);
        } catch (Throwable t) {
            throw new RuntimeException(
              "zlink_spot_node_actor_new_with_request failed", t);
        }
    }

    static int spotNodeActorDestroy(MemorySegment node,
                                           MemorySegment actor,
                                           MemorySegment handler,
                                           MemorySegment userdata,
                                           int timeoutMs) {
        try {
            return (int) MH_SPOT_NODE_ACTOR_DESTROY.invokeExact(node, actor,
              handler, userdata, timeoutMs);
        } catch (Throwable t) {
            throw new RuntimeException(
              "zlink_spot_node_actor_destroy failed", t);
        }
    }

    static int spotNodeActorLookup(MemorySegment node,
                                          MemorySegment actorId,
                                          MemorySegment out) {
        try {
            return (int) MH_SPOT_NODE_ACTOR_LOOKUP.invokeExact(node, actorId,
              out);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_actor_lookup failed", t);
        }
    }

    static int remoteActorGetRef(MemorySegment node,
                                        MemorySegment targetNodeRid,
                                        MemorySegment actorId,
                                        MemorySegment handler,
                                        MemorySegment userdata,
                                        int timeoutMs) {
        try {
            return (int) MH_REMOTE_ACTOR_GET_REF.invokeExact(node,
              targetNodeRid, actorId, handler, userdata, timeoutMs);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_remote_actor_get_ref failed", t);
        }
    }

    static int spotNodeActorJoinSpot(MemorySegment node,
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

    static int spotNodeActorJoinEntrySpot(MemorySegment node,
                                                 MemorySegment actor,
                                                 MemorySegment destNodeRid,
                                                 MemorySegment parts,
                                                 long partCount,
                                                 MemorySegment handler,
                                                 MemorySegment userdata,
                                                 int flags,
                                                 int timeoutMs) {
        try {
            return (int) MH_SPOT_NODE_ACTOR_JOIN_ENTRY_SPOT.invokeExact(node,
              actor, destNodeRid, parts, partCount, handler, userdata, flags,
              timeoutMs);
        } catch (Throwable t) {
            throw new RuntimeException(
              "zlink_spot_node_actor_join_entry_spot failed", t);
        }
    }

    static int spotNodeActorLeaveSpot(MemorySegment node,
                                             MemorySegment actor,
                                             MemorySegment destSpotRid,
                                             MemorySegment handler,
                                             MemorySegment userdata,
                                             int timeoutMs) {
        try {
            return (int) MH_SPOT_NODE_ACTOR_LEAVE_SPOT.invokeExact(node, actor,
              destSpotRid, handler, userdata, timeoutMs);
        } catch (Throwable t) {
            throw new RuntimeException(
              "zlink_spot_node_actor_leave_spot failed", t);
        }
    }

    static int spotNodeActorRecvPart(MemorySegment node,
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

    static int spotNodeActorSendBoundSessionMessage(MemorySegment node,
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

    static int spotNodeSendToActor(MemorySegment node,
                                          MemorySegment actor,
                                          MemorySegment parts,
                                          long partCount,
                                          MemorySegment callback,
                                          MemorySegment userdata,
                                          int flags,
                                          int timeoutMs) {
        try {
            return (int) MH_SPOT_NODE_SEND_TO_ACTOR.invokeExact(
              node, actor, parts, partCount, callback, userdata, flags, timeoutMs);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_send_to_actor failed", t);
        }
    }

    static int spotNodeRequestToActor(MemorySegment node,
                                             MemorySegment actor,
                                             MemorySegment parts,
                                             long partCount,
                                             MemorySegment callback,
                                             MemorySegment userdata,
                                             int flags,
                                             int timeoutMs) {
        try {
            return (int) MH_SPOT_NODE_REQUEST_TO_ACTOR.invokeExact(
              node, actor, parts, partCount, callback, userdata, flags, timeoutMs);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_request_to_actor failed", t);
        }
    }

    static int spotNodeActorReplyNoBind(MemorySegment node,
                                               MemorySegment info,
                                               MemorySegment parts,
                                               long partCount,
                                               int result) {
        try {
            return (int) MH_SPOT_NODE_ACTOR_REPLY_NO_BIND.invokeExact(
              node, info, parts, partCount, result);
        } catch (Throwable t) {
            throw new RuntimeException(
              "zlink_spot_node_actor_reply_no_bind failed", t);
        }
    }

    static int spotNodeActorForwardBoundSessionPart(
                                                       MemorySegment node,
                                                       MemorySegment actor,
                                                       MemorySegment sourceNodeRid,
                                                       MemorySegment sourceSessionRid,
                                                       MemorySegment message,
                                                       int flags,
                                                       int partFlag) {
        try {
            return (int) MH_SPOT_NODE_ACTOR_FORWARD_BOUND_SESSION_PART.invokeExact(
              node, actor, sourceNodeRid, sourceSessionRid, message, flags, partFlag);
        } catch (Throwable t) {
            throw new RuntimeException(
              "zlink_spot_node_actor_forward_bound_session_part failed", t);
        }
    }

    static int spotNodeActorBindRemoteSession(
                                                       MemorySegment node,
                                                       MemorySegment actor,
                                                       MemorySegment sourceNodeRid,
                                                       MemorySegment sourceSessionRid) {
        try {
            return (int) MH_SPOT_NODE_ACTOR_BIND_REMOTE_SESSION.invokeExact(
              node, actor, sourceNodeRid, sourceSessionRid);
        } catch (Throwable t) {
            throw new RuntimeException(
              "zlink_spot_node_actor_bind_remote_session failed", t);
        }
    }

    static int spotNodeActorCloseBoundSession(MemorySegment node,
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

    static int spotNodeRegister(MemorySegment node, MemorySegment service,
                                       MemorySegment ep) {
        try {
            return (int) MH_SPOT_NODE_REG.invokeExact(node, service, ep);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_register failed", t);
        }
    }

    static int spotNodeUnregister(MemorySegment node,
                                         MemorySegment service) {
        try {
            return (int) MH_SPOT_NODE_UNREG.invokeExact(node, service);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_unregister failed", t);
        }
    }

    static MemorySegment spotRouteBridgeNew(MemorySegment ctx,
                                                   MemorySegment node,
                                                   MemorySegment options) {
        try {
            return (MemorySegment) MH_SPOT_ROUTE_BRIDGE_NEW.invokeExact(ctx, node,
              options);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_route_bridge_new failed", t);
        }
    }

    static int spotRouteBridgeAttachRouterChannel(MemorySegment bridge,
                                                         MemorySegment channelName,
                                                         MemorySegment router,
                                                         MemorySegment options) {
        try {
            return (int) MH_SPOT_ROUTE_BRIDGE_ATTACH_ROUTER_CHANNEL.invokeExact(
              bridge, channelName, router, options);
        } catch (Throwable t) {
            throw new RuntimeException(
              "zlink_spot_route_bridge_attach_router_channel failed", t);
        }
    }

    static int spotRouteBridgeSend(MemorySegment bridge,
                                          MemorySegment channelName,
                                          MemorySegment targetNodeRid,
                                          MemorySegment targetSpotRid,
                                          MemorySegment parts,
                                          long partCount,
                                          int flags) {
        try {
            return (int) MH_SPOT_ROUTE_BRIDGE_SEND.invokeExact(bridge,
              channelName, targetNodeRid, targetSpotRid, parts, partCount, flags);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_route_bridge_send failed", t);
        }
    }

    static int spotRouteBridgeRequest(MemorySegment bridge,
                                             MemorySegment channelName,
                                             MemorySegment targetNodeRid,
                                             MemorySegment targetSpotRid,
                                             MemorySegment parts,
                                             long partCount,
                                             MemorySegment callback,
                                             MemorySegment userData,
                                             int flags,
                                             int timeoutMs) {
        try {
            return (int) MH_SPOT_ROUTE_BRIDGE_REQUEST.invokeExact(bridge,
              channelName, targetNodeRid, targetSpotRid, parts, partCount, callback,
              userData, flags, timeoutMs);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_route_bridge_request failed",
              t);
        }
    }

    static int spotRouteBridgeHandleRouterReceived(
        MemorySegment bridge,
        MemorySegment channelName,
        MemorySegment sourceNodeRid,
        long requestSeq,
        MemorySegment parts,
        long partCount,
        MemorySegment handledOut) {
        try {
            return (int) MH_SPOT_ROUTE_BRIDGE_HANDLE_ROUTER_RECEIVED.invokeExact(
              bridge, channelName, sourceNodeRid, requestSeq, parts, partCount,
              handledOut);
        } catch (Throwable t) {
            throw new RuntimeException(
              "zlink_spot_route_bridge_handle_router_received failed", t);
        }
    }

    static int spotRouteBridgeDrain(MemorySegment bridge) {
        try {
            return (int) MH_SPOT_ROUTE_BRIDGE_DRAIN.invokeExact(bridge);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_route_bridge_drain failed", t);
        }
    }

    static int spotRouteBridgeClose(MemorySegment bridge) {
        try {
            return (int) MH_SPOT_ROUTE_BRIDGE_CLOSE.invokeExact(bridge);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_route_bridge_close failed", t);
        }
    }

    static MemorySegment spotNodePublisherNew(MemorySegment node) {
        try {
            return (MemorySegment) MH_SPOT_NODE_PUBLISHER_NEW.invokeExact(node);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_publisher_new failed", t);
        }
    }

    static int spotNodePublisherPublish(MemorySegment publisher,
                                               MemorySegment topic,
                                               MemorySegment parts,
                                               long partCount,
                                               int flags) {
        try {
            return (int) MH_SPOT_NODE_PUBLISHER_PUBLISH.invokeExact(publisher,
              topic, parts, partCount, flags);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_publisher_publish failed",
              t);
        }
    }

    static int spotNodePublisherClose(MemorySegment publisher) {
        try {
            return (int) MH_SPOT_NODE_PUBLISHER_CLOSE.invokeExact(publisher);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_publisher_close failed",
              t);
        }
    }

    static int spotNodeStatus(MemorySegment node,
                                             MemorySegment out) {
        try {
            return (int) MH_SPOT_NODE_STATUS_SNAPSHOT.invokeExact(node, out);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_status failed",
              t);
        }
    }

    static int spotNodePeers(MemorySegment node,
                                            MemorySegment entries,
                                            MemorySegment count) {
        try {
            return (int) MH_SPOT_NODE_PEERS.invokeExact(node,
              MemorySegment.NULL, entries, count);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_peers failed",
              t);
        }
    }

    static int spotNodePeersQuery(MemorySegment node,
                                         MemorySegment filter,
                                         MemorySegment entries,
                                         MemorySegment count) {
        try {
            return (int) MH_SPOT_NODE_PEERS.invokeExact(node, filter,
              entries, count);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_peers failed", t);
        }
    }

    static int spotNodeSubjects(MemorySegment node,
                                               MemorySegment filter,
                                               MemorySegment entries,
                                               MemorySegment count) {
        try {
            return (int) MH_SPOT_NODE_SUBJECTS_SNAPSHOT.invokeExact(node, filter,
              entries, count);
        } catch (Throwable t) {
            throw new RuntimeException(
              "zlink_spot_node_subjects failed", t);
        }
    }

    static int spotNodeInternalSockets(MemorySegment node,
                                                      MemorySegment filter,
                                                      MemorySegment entries,
                                                      MemorySegment count) {
        try {
            return (int) MH_SPOT_NODE_INTERNAL_SOCKETS_SNAPSHOT.invokeExact(
              node, filter, entries, count);
        } catch (Throwable t) {
            throw new RuntimeException(
              "zlink_spot_node_internal_sockets failed", t);
        }
    }

    static int spotNodeSpots(MemorySegment node,
                                            MemorySegment entries,
                                            MemorySegment count) {
        try {
            return (int) MH_SPOT_NODE_SPOTS_SNAPSHOT.invokeExact(node, entries,
              count);
        } catch (Throwable t) {
            throw new RuntimeException(
              "zlink_spot_node_spots failed", t);
        }
    }

    static int spotNodeActors(MemorySegment node,
                                             MemorySegment entries,
                                             MemorySegment count) {
        try {
            return (int) MH_SPOT_NODE_ACTORS_SNAPSHOT.invokeExact(node,
              entries, count);
        } catch (Throwable t) {
            throw new RuntimeException(
              "zlink_spot_node_actors failed", t);
        }
    }

    static int spotSendChannelPart(MemorySegment spot,
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

    static int spotPublishPart(MemorySegment spot,
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

    static int spotSubscribePart(MemorySegment spot,
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

    static int spotSubscribePartNoWaitCritical(MemorySegment spot,
                                        MemorySegment sourceRidOut,
                                        MemorySegment topicIdOut,
                                        long topicIdCapacity,
                                        MemorySegment topicIdLenOut,
                                        MemorySegment partOut,
                                        MemorySegment hasMoreOut,
                                        int flags) {
        try {
            return (int) MH_SPOT_SUBSCRIBE_PART_CRITICAL.invokeExact(spot,
              sourceRidOut, topicIdOut, topicIdCapacity, topicIdLenOut, partOut,
              hasMoreOut, flags);
        } catch (Throwable t) {
            throw new RuntimeException(
              "zlink_spot_subscribe_part (critical) failed", t);
        }
    }

    static int spotRequestChannelPart(MemorySegment spot,
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

    static int spotRequestSpotPart(MemorySegment spot,
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

    static int spotRequestRouterPart(MemorySegment spot,
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

    static int spotActorJoinRecv(MemorySegment spot,
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

    static int spotRecvActorLifecycle(MemorySegment spot,
                                             MemorySegment eventOut,
                                             int flags) {
        try {
            return (int) MH_SPOT_RECV_ACTOR_LIFECYCLE.invokeExact(spot,
              eventOut, flags);
        } catch (Throwable t) {
            throw new RuntimeException(
              "zlink_spot_recv_actor_lifecycle failed", t);
        }
    }

    static int spotRecvActorLifecycleWithRequest(MemorySegment spot,
                                                        MemorySegment eventOut,
                                                        MemorySegment partsOut,
                                                        MemorySegment countOut,
                                                        int flags) {
        try {
            return (int) MH_SPOT_RECV_ACTOR_LIFECYCLE_WITH_REQUEST.invokeExact(
              spot, eventOut, partsOut, countOut, flags);
        } catch (Throwable t) {
            throw new RuntimeException(
              "zlink_spot_recv_actor_lifecycle_with_request failed", t);
        }
    }

    static int spotActorJoinReply(MemorySegment spot,
                                         MemorySegment info,
                                         int joinResultCode,
                                         MemorySegment parts,
                                         long partCount) {
        try {
            return (int) MH_SPOT_ACTOR_JOIN_REPLY.invokeExact(spot, info,
              joinResultCode, parts, partCount);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_actor_join_reply failed", t);
        }
    }

    static int spotActors(MemorySegment spot,
                                         MemorySegment entries,
                                         MemorySegment count) {
        try {
            return (int) MH_SPOT_ACTORS_SNAPSHOT.invokeExact(spot, entries,
              count);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_actors failed", t);
        }
    }

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
