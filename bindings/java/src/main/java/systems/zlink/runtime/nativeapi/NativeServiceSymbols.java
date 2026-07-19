/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.nativeapi;

import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;

/**
 * MethodHandle downcalls and thin invokers for the Core 10.0.0 service API
 * (mesh_node, actor, spot, dispatch, stream_session). Missing symbols resolve
 * lazily to throwing handles, so absent symbols do not break class loading.
 */
public final class NativeServiceSymbols {
    private static final ValueLayout.OfInt I = ValueLayout.JAVA_INT;
    private static final ValueLayout.OfLong L = ValueLayout.JAVA_LONG;
    private static final java.lang.foreign.AddressLayout A = ValueLayout.ADDRESS;

    /** Descriptor for the dispatch ready-handler upcall stub. */
    public static final FunctionDescriptor READY_HANDLER_DESCRIPTOR =
        FunctionDescriptor.of(I, A, I, A);

    private NativeServiceSymbols() {
    }

    private static MethodHandle dc(String name, FunctionDescriptor fd) {
        return NativeSymbols.downcall(name, fd);
    }

    private static RuntimeException fail(String name, Throwable t) {
        return new RuntimeException(name + " failed", t);
    }

    // --- mesh_node lifecycle ---
    private static final MethodHandle MESH_NODE_NEW =
        dc("zlink_mesh_node_new", FunctionDescriptor.of(A, A, A));
    private static final MethodHandle MESH_NODE_SET_BIND =
        dc("zlink_mesh_node_set_bind", FunctionDescriptor.of(I, A, A));
    private static final MethodHandle MESH_NODE_START =
        dc("zlink_mesh_node_start", FunctionDescriptor.of(I, A));
    private static final MethodHandle MESH_NODE_SHUTDOWN =
        dc("zlink_mesh_node_shutdown", FunctionDescriptor.of(I, A, I));
    private static final MethodHandle MESH_NODE_DESTROY =
        dc("zlink_mesh_node_destroy", FunctionDescriptor.of(I, A));
    private static final MethodHandle MESH_NODE_ADD_CHANNEL_NAME =
        dc("zlink_mesh_node_add_channel_name", FunctionDescriptor.of(I, A, A));
    private static final MethodHandle MESH_NODE_SET_CHANNEL_WEIGHT =
        dc("zlink_mesh_node_set_channel_weight", FunctionDescriptor.of(I, A, A, I));
    private static final MethodHandle MESH_NODE_CONNECT_PEER =
        dc("zlink_mesh_node_connect_peer", FunctionDescriptor.of(I, A, A, A));
    private static final MethodHandle MESH_NODE_REMOVE_PEER_CONNECTION =
        dc("zlink_mesh_node_remove_peer_connection", FunctionDescriptor.of(I, A, L));
    private static final MethodHandle MESH_NODE_DISCONNECT_PEER =
        dc("zlink_mesh_node_disconnect_peer", FunctionDescriptor.of(I, A, A, L));
    private static final MethodHandle MESH_NODE_STATUS =
        dc("zlink_mesh_node_status", FunctionDescriptor.of(I, A, A));
    private static final MethodHandle MESH_NODE_PEERS =
        dc("zlink_mesh_node_peers", FunctionDescriptor.of(I, A, A, A));
    private static final MethodHandle MESH_NODE_MONITOR_OPEN =
        dc("zlink_mesh_node_monitor_open", FunctionDescriptor.of(A, A, A));
    private static final MethodHandle MESH_NODE_MONITOR_RECV =
        dc("zlink_mesh_node_monitor_recv", FunctionDescriptor.of(I, A, A, I));
    private static final MethodHandle MESH_NODE_MONITOR_STATUS =
        dc("zlink_mesh_node_monitor_status", FunctionDescriptor.of(I, A, A));
    private static final MethodHandle MESH_NODE_MONITOR_CLOSE =
        dc("zlink_mesh_node_monitor_close", FunctionDescriptor.of(I, A));

    // --- node/channel messaging ---
    private static final MethodHandle MESH_NODE_SEND_TO_NODE =
        dc("zlink_mesh_node_send_to_node", FunctionDescriptor.of(I, A, A, A, A, L, I));
    private static final MethodHandle MESH_NODE_REQUEST_TO_NODE =
        dc("zlink_mesh_node_request_to_node", FunctionDescriptor.of(I, A, A, A, A, L, A, I, I));
    private static final MethodHandle MESH_NODE_SEND_TO_CHANNEL =
        dc("zlink_mesh_node_send_to_channel", FunctionDescriptor.of(I, A, A, A, A, L, I));
    private static final MethodHandle MESH_NODE_REQUEST_TO_CHANNEL =
        dc("zlink_mesh_node_request_to_channel", FunctionDescriptor.of(I, A, A, A, A, L, A, I, I));

    // --- publisher ---
    private static final MethodHandle MESH_NODE_PUBLISHER_NEW =
        dc("zlink_mesh_node_publisher_new", FunctionDescriptor.of(A, A));
    private static final MethodHandle MESH_NODE_PUBLISHER_PUBLISH =
        dc("zlink_mesh_node_publisher_publish", FunctionDescriptor.of(I, A, A, A, A, A, L, A, I));
    private static final MethodHandle MESH_NODE_PUBLISHER_DESTROY =
        dc("zlink_mesh_node_publisher_destroy", FunctionDescriptor.of(I, A));

    // --- actor ---
    private static final MethodHandle ACTOR_NEW =
        dc("zlink_mesh_node_actor_new", FunctionDescriptor.of(I, A, A, A, L, A, I, I));
    private static final MethodHandle ACTOR_LOOKUP =
        dc("zlink_mesh_node_actor_lookup", FunctionDescriptor.of(I, A, A, A));
    private static final MethodHandle ACTOR_LOOKUP_REMOTE =
        dc("zlink_mesh_node_actor_lookup_remote", FunctionDescriptor.of(I, A, A, A, A, I));
    private static final MethodHandle ACTOR_DESTROY =
        dc("zlink_mesh_node_actor_destroy", FunctionDescriptor.of(I, A, A, A, I));
    private static final MethodHandle ACTOR_JOIN_SPOT =
        dc("zlink_mesh_node_actor_join_spot", FunctionDescriptor.of(I, A, A, A, A, L, A, L, A, I));
    private static final MethodHandle ACTOR_JOIN_ENTRY_SPOT =
        dc("zlink_mesh_node_actor_join_entry_spot", FunctionDescriptor.of(I, A, A, A, A, L, A, I));
    private static final MethodHandle ACTOR_LEAVE_SPOT =
        dc("zlink_mesh_node_actor_leave_spot", FunctionDescriptor.of(I, A, A, L, A, I));
    private static final MethodHandle ACTOR_JOIN_REPLY =
        dc("zlink_actor_join_reply", FunctionDescriptor.of(I, A, I, A, L, I));
    private static final MethodHandle SEND_TO_ACTOR =
        dc("zlink_mesh_node_send_to_actor", FunctionDescriptor.of(I, A, A, A, A, L, I));
    private static final MethodHandle REQUEST_TO_ACTOR =
        dc("zlink_mesh_node_request_to_actor", FunctionDescriptor.of(I, A, A, A, A, L, A, I, I));
    private static final MethodHandle ACTOR_SEND_TO_ACTOR =
        dc("zlink_actor_send_to_actor", FunctionDescriptor.of(I, A, A, A, A, A, L, I));
    private static final MethodHandle ACTOR_REQUEST_TO_ACTOR =
        dc("zlink_actor_request_to_actor", FunctionDescriptor.of(I, A, A, A, A, A, L, A, I, I));

    // --- spot ---
    private static final MethodHandle SPOT_NEW =
        dc("zlink_spot_new", FunctionDescriptor.of(A, A));
    private static final MethodHandle NODE_ENTRY_SPOT =
        dc("zlink_mesh_node_entry_spot", FunctionDescriptor.of(I, A, A));
    private static final MethodHandle NODE_SPOT_LOOKUP =
        dc("zlink_mesh_node_spot_lookup", FunctionDescriptor.of(I, A, A, A));
    private static final MethodHandle NODE_SPOT_GET_OR_NEW =
        dc("zlink_mesh_node_spot_get_or_new", FunctionDescriptor.of(I, A, A, A, A));
    private static final MethodHandle SPOT_DESTROY =
        dc("zlink_spot_destroy", FunctionDescriptor.of(I, A));
    private static final MethodHandle SPOT_STATUS =
        dc("zlink_spot_status", FunctionDescriptor.of(I, A, A));
    private static final MethodHandle SPOT_SEND_TO_CHANNEL =
        dc("zlink_spot_send_to_channel", FunctionDescriptor.of(I, A, A, A, A, L, I));
    private static final MethodHandle SPOT_REQUEST_TO_CHANNEL =
        dc("zlink_spot_request_to_channel", FunctionDescriptor.of(I, A, A, A, A, L, A, I, I));
    private static final MethodHandle SPOT_SEND_TO_SPOT =
        dc("zlink_spot_send_to_spot", FunctionDescriptor.of(I, A, A, A, L, A, A, L, I));
    private static final MethodHandle SPOT_REQUEST_TO_SPOT =
        dc("zlink_spot_request_to_spot", FunctionDescriptor.of(I, A, A, A, L, A, A, L, A, I, I));
    private static final MethodHandle SPOT_PUBLISH =
        dc("zlink_spot_publish", FunctionDescriptor.of(I, A, A, A, A, A, L, A, I));
    private static final MethodHandle SPOT_SET_SUBSCRIPTION =
        dc("zlink_spot_set_subscription", FunctionDescriptor.of(I, A, A, A, I));
    private static final MethodHandle SPOT_UNSET_SUBSCRIPTION =
        dc("zlink_spot_unset_subscription", FunctionDescriptor.of(I, A, A, A, I));

    // --- dispatch ---
    private static final MethodHandle SET_READY_HANDLER =
        dc("zlink_mesh_node_set_ready_handler", FunctionDescriptor.of(I, A, A, A));
    private static final MethodHandle READY_BATCH_NEW =
        dc("zlink_mesh_ready_batch_new", FunctionDescriptor.of(A, L));
    private static final MethodHandle READY_BATCH_RESET =
        dc("zlink_mesh_ready_batch_reset", FunctionDescriptor.of(I, A));
    private static final MethodHandle READY_BATCH_DESTROY =
        dc("zlink_mesh_ready_batch_destroy", FunctionDescriptor.of(I, A));
    private static final MethodHandle DRAIN_READY =
        dc("zlink_mesh_node_drain_ready", FunctionDescriptor.of(I, A, I, A, A, I));
    private static final MethodHandle READY_BATCH_COUNT =
        dc("zlink_mesh_ready_batch_count", FunctionDescriptor.of(L, A));
    private static final MethodHandle READY_BATCH_DATA =
        dc("zlink_mesh_ready_batch_data", FunctionDescriptor.of(A, A));
    private static final MethodHandle READY_BATCH_TAKE_CLAIM =
        dc("zlink_mesh_ready_batch_take_claim", FunctionDescriptor.of(I, A, L, A));
    private static final MethodHandle CLAIM_RELEASE =
        dc("zlink_mesh_claim_release", FunctionDescriptor.of(I, A));
    private static final MethodHandle RECEIVE_BATCH_NEW =
        dc("zlink_mesh_receive_batch_new", FunctionDescriptor.of(A, L, L, L));
    private static final MethodHandle RECEIVE_BATCH_RESET =
        dc("zlink_mesh_receive_batch_reset", FunctionDescriptor.of(I, A));
    private static final MethodHandle RECEIVE_BATCH_DESTROY =
        dc("zlink_mesh_receive_batch_destroy", FunctionDescriptor.of(I, A));
    private static final MethodHandle CLAIM_RECV_BATCH =
        dc("zlink_mesh_claim_recv_batch", FunctionDescriptor.of(I, A, A, A, I));
    private static final MethodHandle RECEIVE_BATCH_COUNT =
        dc("zlink_mesh_receive_batch_count", FunctionDescriptor.of(L, A));
    private static final MethodHandle RECEIVE_BATCH_DATA =
        dc("zlink_mesh_receive_batch_data", FunctionDescriptor.of(A, A));
    private static final MethodHandle RECEIVE_BATCH_RETAIN_MESSAGE =
        dc("zlink_mesh_receive_batch_retain_message", FunctionDescriptor.of(I, A, L, A, A));
    private static final MethodHandle MESH_REPLY =
        dc("zlink_mesh_reply", FunctionDescriptor.of(I, A, A, L, I));

    // --- stream_session ---
    private static final MethodHandle STREAM_SESSION_NEW =
        dc("zlink_stream_session_service_new", FunctionDescriptor.of(A, A, A));
    private static final MethodHandle STREAM_SESSION_START =
        dc("zlink_stream_session_service_start", FunctionDescriptor.of(I, A));
    private static final MethodHandle STREAM_SESSION_SHUTDOWN =
        dc("zlink_stream_session_service_shutdown", FunctionDescriptor.of(I, A, I));
    private static final MethodHandle STREAM_SESSION_DESTROY =
        dc("zlink_stream_session_service_destroy", FunctionDescriptor.of(I, A));
    private static final MethodHandle STREAM_SESSION_STATUS =
        dc("zlink_stream_session_service_status", FunctionDescriptor.of(I, A, A));
    private static final MethodHandle STREAM_SESSION_BIND_ACTOR =
        dc("zlink_stream_session_bind_actor", FunctionDescriptor.of(I, A, A, A, A, I));
    private static final MethodHandle STREAM_SESSION_UNBIND_ACTOR =
        dc("zlink_stream_session_unbind_actor", FunctionDescriptor.of(I, A, A, A, L, A, I));
    private static final MethodHandle STREAM_SESSION_BINDINGS =
        dc("zlink_stream_session_bindings", FunctionDescriptor.of(I, A, A, A, A));
    private static final MethodHandle STREAM_SESSION_SEND_TO_ACTOR =
        dc("zlink_stream_session_send_to_actor", FunctionDescriptor.of(I, A, A, A, A, A, L, I));
    private static final MethodHandle STREAM_SESSION_REQUEST_TO_ACTOR =
        dc("zlink_stream_session_request_to_actor", FunctionDescriptor.of(I, A, A, A, A, A, L, A, I, I));
    private static final MethodHandle ACTOR_SEND_BOUND_SESSION =
        dc("zlink_mesh_node_actor_send_bound_session", FunctionDescriptor.of(I, A, A, A, L, I));
    private static final MethodHandle ACTOR_CLOSE_BOUND_SESSION =
        dc("zlink_mesh_node_actor_close_bound_session", FunctionDescriptor.of(I, A, A, L, A, I));

    // ================= invokers =================

    public static MemorySegment meshNodeNew(MemorySegment ctx, MemorySegment options) {
        try {
            return (MemorySegment) MESH_NODE_NEW.invokeExact(ctx, options);
        } catch (Throwable t) {
            throw fail("zlink_mesh_node_new", t);
        }
    }

    public static int meshNodeSetBind(MemorySegment node, MemorySegment ep) {
        try {
            return (int) MESH_NODE_SET_BIND.invokeExact(node, ep);
        } catch (Throwable t) {
            throw fail("zlink_mesh_node_set_bind", t);
        }
    }

    public static int meshNodeStart(MemorySegment node) {
        try {
            return (int) MESH_NODE_START.invokeExact(node);
        } catch (Throwable t) {
            throw fail("zlink_mesh_node_start", t);
        }
    }

    public static int meshNodeShutdown(MemorySegment node, int timeoutMs) {
        try {
            return (int) MESH_NODE_SHUTDOWN.invokeExact(node, timeoutMs);
        } catch (Throwable t) {
            throw fail("zlink_mesh_node_shutdown", t);
        }
    }

    public static int meshNodeDestroy(MemorySegment nodeHandle) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment holder = arena.allocate(ValueLayout.ADDRESS);
            holder.set(ValueLayout.ADDRESS, 0, nodeHandle);
            return (int) MESH_NODE_DESTROY.invokeExact(holder);
        } catch (Throwable t) {
            throw fail("zlink_mesh_node_destroy", t);
        }
    }

    public static int meshNodeAddChannelName(MemorySegment node, MemorySegment name) {
        try {
            return (int) MESH_NODE_ADD_CHANNEL_NAME.invokeExact(node, name);
        } catch (Throwable t) {
            throw fail("zlink_mesh_node_add_channel_name", t);
        }
    }

    public static int meshNodeSetChannelWeight(MemorySegment node, MemorySegment name, int weight) {
        try {
            return (int) MESH_NODE_SET_CHANNEL_WEIGHT.invokeExact(node, name, weight);
        } catch (Throwable t) {
            throw fail("zlink_mesh_node_set_channel_weight", t);
        }
    }

    public static int meshNodeConnectPeer(MemorySegment node, MemorySegment options,
                                   MemorySegment intentOut) {
        try {
            return (int) MESH_NODE_CONNECT_PEER.invokeExact(node, options, intentOut);
        } catch (Throwable t) {
            throw fail("zlink_mesh_node_connect_peer", t);
        }
    }

    public static int meshNodeRemovePeerConnection(MemorySegment node, long intentId) {
        try {
            return (int) MESH_NODE_REMOVE_PEER_CONNECTION.invokeExact(node, intentId);
        } catch (Throwable t) {
            throw fail("zlink_mesh_node_remove_peer_connection", t);
        }
    }

    public static int meshNodeDisconnectPeer(MemorySegment node, MemorySegment peerRid, long gen) {
        try {
            return (int) MESH_NODE_DISCONNECT_PEER.invokeExact(node, peerRid, gen);
        } catch (Throwable t) {
            throw fail("zlink_mesh_node_disconnect_peer", t);
        }
    }

    public static int meshNodeStatus(MemorySegment node, MemorySegment statusOut) {
        try {
            return (int) MESH_NODE_STATUS.invokeExact(node, statusOut);
        } catch (Throwable t) {
            throw fail("zlink_mesh_node_status", t);
        }
    }

    public static int meshNodePeers(MemorySegment node, MemorySegment entries, MemorySegment count) {
        try {
            return (int) MESH_NODE_PEERS.invokeExact(node, entries, count);
        } catch (Throwable t) {
            throw fail("zlink_mesh_node_peers", t);
        }
    }

    public static MemorySegment meshNodeMonitorOpen(MemorySegment node, MemorySegment options) {
        try {
            return (MemorySegment) MESH_NODE_MONITOR_OPEN.invokeExact(node, options);
        } catch (Throwable t) {
            throw fail("zlink_mesh_node_monitor_open", t);
        }
    }

    public static int meshNodeMonitorRecv(MemorySegment monitor, MemorySegment event, int flags) {
        try {
            return (int) MESH_NODE_MONITOR_RECV.invokeExact(monitor, event, flags);
        } catch (Throwable t) {
            throw fail("zlink_mesh_node_monitor_recv", t);
        }
    }

    public static int meshNodeMonitorStatus(MemorySegment monitor, MemorySegment status) {
        try {
            return (int) MESH_NODE_MONITOR_STATUS.invokeExact(monitor, status);
        } catch (Throwable t) {
            throw fail("zlink_mesh_node_monitor_status", t);
        }
    }

    public static int meshNodeMonitorClose(MemorySegment monitor) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment holder = arena.allocate(ValueLayout.ADDRESS);
            holder.set(ValueLayout.ADDRESS, 0, monitor);
            return (int) MESH_NODE_MONITOR_CLOSE.invokeExact(holder);
        } catch (Throwable t) {
            throw fail("zlink_mesh_node_monitor_close", t);
        }
    }

    public static int meshNodeSendToNode(MemorySegment node, MemorySegment rid, MemorySegment meta,
                                  MemorySegment parts, long count, int flags) {
        try {
            return (int) MESH_NODE_SEND_TO_NODE.invokeExact(node, rid, meta, parts, count, flags);
        } catch (Throwable t) {
            throw fail("zlink_mesh_node_send_to_node", t);
        }
    }

    public static int meshNodeRequestToNode(MemorySegment node, MemorySegment rid, MemorySegment meta,
                                     MemorySegment parts, long count, MemorySegment opidOut,
                                     int flags, int timeoutMs) {
        try {
            return (int) MESH_NODE_REQUEST_TO_NODE.invokeExact(node, rid, meta, parts, count,
                opidOut, flags, timeoutMs);
        } catch (Throwable t) {
            throw fail("zlink_mesh_node_request_to_node", t);
        }
    }

    public static int meshNodeSendToChannel(MemorySegment node, MemorySegment name, MemorySegment meta,
                                     MemorySegment parts, long count, int flags) {
        try {
            return (int) MESH_NODE_SEND_TO_CHANNEL.invokeExact(node, name, meta, parts, count, flags);
        } catch (Throwable t) {
            throw fail("zlink_mesh_node_send_to_channel", t);
        }
    }

    public static int meshNodeRequestToChannel(MemorySegment node, MemorySegment name, MemorySegment meta,
                                        MemorySegment parts, long count, MemorySegment opidOut,
                                        int flags, int timeoutMs) {
        try {
            return (int) MESH_NODE_REQUEST_TO_CHANNEL.invokeExact(node, name, meta, parts, count,
                opidOut, flags, timeoutMs);
        } catch (Throwable t) {
            throw fail("zlink_mesh_node_request_to_channel", t);
        }
    }

    public static MemorySegment publisherNew(MemorySegment node) {
        try {
            return (MemorySegment) MESH_NODE_PUBLISHER_NEW.invokeExact(node);
        } catch (Throwable t) {
            throw fail("zlink_mesh_node_publisher_new", t);
        }
    }

    public static int publisherPublish(MemorySegment pub, MemorySegment name, MemorySegment topic,
                                MemorySegment meta, MemorySegment parts, long count,
                                MemorySegment detailOut, int flags) {
        try {
            return (int) MESH_NODE_PUBLISHER_PUBLISH.invokeExact(pub, name, topic, meta, parts,
                count, detailOut, flags);
        } catch (Throwable t) {
            throw fail("zlink_mesh_node_publisher_publish", t);
        }
    }

    public static int publisherDestroy(MemorySegment handle) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment holder = arena.allocate(ValueLayout.ADDRESS);
            holder.set(ValueLayout.ADDRESS, 0, handle);
            return (int) MESH_NODE_PUBLISHER_DESTROY.invokeExact(holder);
        } catch (Throwable t) {
            throw fail("zlink_mesh_node_publisher_destroy", t);
        }
    }

    public static int actorNew(MemorySegment node, MemorySegment actorId, MemorySegment parts,
                        long count, MemorySegment refOut, int flags, int timeoutMs) {
        try {
            return (int) ACTOR_NEW.invokeExact(node, actorId, parts, count, refOut, flags, timeoutMs);
        } catch (Throwable t) {
            throw fail("zlink_mesh_node_actor_new", t);
        }
    }

    public static int actorLookup(MemorySegment node, MemorySegment actorId, MemorySegment locationOut) {
        try {
            return (int) ACTOR_LOOKUP.invokeExact(node, actorId, locationOut);
        } catch (Throwable t) {
            throw fail("zlink_mesh_node_actor_lookup", t);
        }
    }

    public static int actorLookupRemote(MemorySegment node, MemorySegment rid, MemorySegment actorId,
                                 MemorySegment opidOut, int timeoutMs) {
        try {
            return (int) ACTOR_LOOKUP_REMOTE.invokeExact(node, rid, actorId, opidOut, timeoutMs);
        } catch (Throwable t) {
            throw fail("zlink_mesh_node_actor_lookup_remote", t);
        }
    }

    public static int actorDestroy(MemorySegment node, MemorySegment ref, MemorySegment opidOut,
                            int timeoutMs) {
        try {
            return (int) ACTOR_DESTROY.invokeExact(node, ref, opidOut, timeoutMs);
        } catch (Throwable t) {
            throw fail("zlink_mesh_node_actor_destroy", t);
        }
    }

    public static int actorJoinSpot(MemorySegment node, MemorySegment ref, MemorySegment nodeRid,
                             MemorySegment spotRid, long gen, MemorySegment parts, long count,
                             MemorySegment opidOut, int timeoutMs) {
        try {
            return (int) ACTOR_JOIN_SPOT.invokeExact(node, ref, nodeRid, spotRid, gen, parts,
                count, opidOut, timeoutMs);
        } catch (Throwable t) {
            throw fail("zlink_mesh_node_actor_join_spot", t);
        }
    }

    public static int actorJoinEntrySpot(MemorySegment node, MemorySegment ref, MemorySegment nodeRid,
                                  MemorySegment parts, long count, MemorySegment opidOut,
                                  int timeoutMs) {
        try {
            return (int) ACTOR_JOIN_ENTRY_SPOT.invokeExact(node, ref, nodeRid, parts, count,
                opidOut, timeoutMs);
        } catch (Throwable t) {
            throw fail("zlink_mesh_node_actor_join_entry_spot", t);
        }
    }

    public static int actorLeaveSpot(MemorySegment node, MemorySegment ref, long expectedEpoch,
                              MemorySegment opidOut, int timeoutMs) {
        try {
            return (int) ACTOR_LEAVE_SPOT.invokeExact(node, ref, expectedEpoch, opidOut, timeoutMs);
        } catch (Throwable t) {
            throw fail("zlink_mesh_node_actor_leave_spot", t);
        }
    }

    public static int actorJoinReply(MemorySegment token, int joinResult, MemorySegment parts,
                              long count, int flags) {
        try {
            return (int) ACTOR_JOIN_REPLY.invokeExact(token, joinResult, parts, count, flags);
        } catch (Throwable t) {
            throw fail("zlink_actor_join_reply", t);
        }
    }

    public static int sendToActor(MemorySegment node, MemorySegment ref, MemorySegment meta,
                           MemorySegment parts, long count, int flags) {
        try {
            return (int) SEND_TO_ACTOR.invokeExact(node, ref, meta, parts, count, flags);
        } catch (Throwable t) {
            throw fail("zlink_mesh_node_send_to_actor", t);
        }
    }

    public static int requestToActor(MemorySegment node, MemorySegment ref, MemorySegment meta,
                              MemorySegment parts, long count, MemorySegment opidOut,
                              int flags, int timeoutMs) {
        try {
            return (int) REQUEST_TO_ACTOR.invokeExact(node, ref, meta, parts, count, opidOut,
                flags, timeoutMs);
        } catch (Throwable t) {
            throw fail("zlink_mesh_node_request_to_actor", t);
        }
    }

    public static int actorSendToActor(MemorySegment node, MemorySegment srcRef, MemorySegment tgtRef,
                                MemorySegment meta, MemorySegment parts, long count, int flags) {
        try {
            return (int) ACTOR_SEND_TO_ACTOR.invokeExact(node, srcRef, tgtRef, meta, parts, count, flags);
        } catch (Throwable t) {
            throw fail("zlink_actor_send_to_actor", t);
        }
    }

    public static int actorRequestToActor(MemorySegment node, MemorySegment srcRef, MemorySegment tgtRef,
                                   MemorySegment meta, MemorySegment parts, long count,
                                   MemorySegment opidOut, int flags, int timeoutMs) {
        try {
            return (int) ACTOR_REQUEST_TO_ACTOR.invokeExact(node, srcRef, tgtRef, meta, parts,
                count, opidOut, flags, timeoutMs);
        } catch (Throwable t) {
            throw fail("zlink_actor_request_to_actor", t);
        }
    }

    public static MemorySegment spotNew(MemorySegment node) {
        try {
            return (MemorySegment) SPOT_NEW.invokeExact(node);
        } catch (Throwable t) {
            throw fail("zlink_spot_new", t);
        }
    }

    public static int nodeEntrySpot(MemorySegment node, MemorySegment spotOut) {
        try {
            return (int) NODE_ENTRY_SPOT.invokeExact(node, spotOut);
        } catch (Throwable t) {
            throw fail("zlink_mesh_node_entry_spot", t);
        }
    }

    public static int nodeSpotLookup(MemorySegment node, MemorySegment rid, MemorySegment spotOut) {
        try {
            return (int) NODE_SPOT_LOOKUP.invokeExact(node, rid, spotOut);
        } catch (Throwable t) {
            throw fail("zlink_mesh_node_spot_lookup", t);
        }
    }

    public static int nodeSpotGetOrNew(MemorySegment node, MemorySegment rid, MemorySegment spotOut,
                                MemorySegment createdOut) {
        try {
            return (int) NODE_SPOT_GET_OR_NEW.invokeExact(node, rid, spotOut, createdOut);
        } catch (Throwable t) {
            throw fail("zlink_mesh_node_spot_get_or_new", t);
        }
    }

    public static int spotDestroy(MemorySegment handle) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment holder = arena.allocate(ValueLayout.ADDRESS);
            holder.set(ValueLayout.ADDRESS, 0, handle);
            return (int) SPOT_DESTROY.invokeExact(holder);
        } catch (Throwable t) {
            throw fail("zlink_spot_destroy", t);
        }
    }

    public static int spotStatus(MemorySegment spot, MemorySegment statusOut) {
        try {
            return (int) SPOT_STATUS.invokeExact(spot, statusOut);
        } catch (Throwable t) {
            throw fail("zlink_spot_status", t);
        }
    }

    public static int spotSendToChannel(MemorySegment spot, MemorySegment name, MemorySegment meta,
                                 MemorySegment parts, long count, int flags) {
        try {
            return (int) SPOT_SEND_TO_CHANNEL.invokeExact(spot, name, meta, parts, count, flags);
        } catch (Throwable t) {
            throw fail("zlink_spot_send_to_channel", t);
        }
    }

    public static int spotRequestToChannel(MemorySegment spot, MemorySegment name, MemorySegment meta,
                                    MemorySegment parts, long count, MemorySegment opidOut,
                                    int flags, int timeoutMs) {
        try {
            return (int) SPOT_REQUEST_TO_CHANNEL.invokeExact(spot, name, meta, parts, count,
                opidOut, flags, timeoutMs);
        } catch (Throwable t) {
            throw fail("zlink_spot_request_to_channel", t);
        }
    }

    public static int spotSendToSpot(MemorySegment spot, MemorySegment nodeRid, MemorySegment spotRid,
                              long gen, MemorySegment meta, MemorySegment parts, long count,
                              int flags) {
        try {
            return (int) SPOT_SEND_TO_SPOT.invokeExact(spot, nodeRid, spotRid, gen, meta, parts,
                count, flags);
        } catch (Throwable t) {
            throw fail("zlink_spot_send_to_spot", t);
        }
    }

    public static int spotRequestToSpot(MemorySegment spot, MemorySegment nodeRid, MemorySegment spotRid,
                                 long gen, MemorySegment meta, MemorySegment parts, long count,
                                 MemorySegment opidOut, int flags, int timeoutMs) {
        try {
            return (int) SPOT_REQUEST_TO_SPOT.invokeExact(spot, nodeRid, spotRid, gen, meta, parts,
                count, opidOut, flags, timeoutMs);
        } catch (Throwable t) {
            throw fail("zlink_spot_request_to_spot", t);
        }
    }

    public static int spotPublish(MemorySegment spot, MemorySegment name, MemorySegment topic,
                           MemorySegment meta, MemorySegment parts, long count,
                           MemorySegment detailOut, int flags) {
        try {
            return (int) SPOT_PUBLISH.invokeExact(spot, name, topic, meta, parts, count,
                detailOut, flags);
        } catch (Throwable t) {
            throw fail("zlink_spot_publish", t);
        }
    }

    public static int spotSetSubscription(MemorySegment spot, MemorySegment name, MemorySegment filter,
                                   int kind) {
        try {
            return (int) SPOT_SET_SUBSCRIPTION.invokeExact(spot, name, filter, kind);
        } catch (Throwable t) {
            throw fail("zlink_spot_set_subscription", t);
        }
    }

    public static int spotUnsetSubscription(MemorySegment spot, MemorySegment name, MemorySegment filter,
                                     int kind) {
        try {
            return (int) SPOT_UNSET_SUBSCRIPTION.invokeExact(spot, name, filter, kind);
        } catch (Throwable t) {
            throw fail("zlink_spot_unset_subscription", t);
        }
    }

    public static int setReadyHandler(MemorySegment node, MemorySegment fn, MemorySegment userdata) {
        try {
            return (int) SET_READY_HANDLER.invokeExact(node, fn, userdata);
        } catch (Throwable t) {
            throw fail("zlink_mesh_node_set_ready_handler", t);
        }
    }

    public static MemorySegment readyBatchNew(long capacity) {
        try {
            return (MemorySegment) READY_BATCH_NEW.invokeExact(capacity);
        } catch (Throwable t) {
            throw fail("zlink_mesh_ready_batch_new", t);
        }
    }

    public static int readyBatchReset(MemorySegment batch) {
        try {
            return (int) READY_BATCH_RESET.invokeExact(batch);
        } catch (Throwable t) {
            throw fail("zlink_mesh_ready_batch_reset", t);
        }
    }

    public static int readyBatchDestroy(MemorySegment handle) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment holder = arena.allocate(ValueLayout.ADDRESS);
            holder.set(ValueLayout.ADDRESS, 0, handle);
            return (int) READY_BATCH_DESTROY.invokeExact(holder);
        } catch (Throwable t) {
            throw fail("zlink_mesh_ready_batch_destroy", t);
        }
    }

    public static int drainReady(MemorySegment node, int domains, MemorySegment batch,
                          MemorySegment residueOut, int flags) {
        try {
            return (int) DRAIN_READY.invokeExact(node, domains, batch, residueOut, flags);
        } catch (Throwable t) {
            throw fail("zlink_mesh_node_drain_ready", t);
        }
    }

    public static long readyBatchCount(MemorySegment batch) {
        try {
            return (long) READY_BATCH_COUNT.invokeExact(batch);
        } catch (Throwable t) {
            throw fail("zlink_mesh_ready_batch_count", t);
        }
    }

    public static MemorySegment readyBatchData(MemorySegment batch) {
        try {
            return (MemorySegment) READY_BATCH_DATA.invokeExact(batch);
        } catch (Throwable t) {
            throw fail("zlink_mesh_ready_batch_data", t);
        }
    }

    public static int readyBatchTakeClaim(MemorySegment batch, long index, MemorySegment claimOut) {
        try {
            return (int) READY_BATCH_TAKE_CLAIM.invokeExact(batch, index, claimOut);
        } catch (Throwable t) {
            throw fail("zlink_mesh_ready_batch_take_claim", t);
        }
    }

    public static int claimRelease(MemorySegment claim) {
        try {
            return (int) CLAIM_RELEASE.invokeExact(claim);
        } catch (Throwable t) {
            throw fail("zlink_mesh_claim_release", t);
        }
    }

    public static MemorySegment receiveBatchNew(long msgCap, long partCap, long byteCap) {
        try {
            return (MemorySegment) RECEIVE_BATCH_NEW.invokeExact(msgCap, partCap, byteCap);
        } catch (Throwable t) {
            throw fail("zlink_mesh_receive_batch_new", t);
        }
    }

    public static int receiveBatchReset(MemorySegment batch) {
        try {
            return (int) RECEIVE_BATCH_RESET.invokeExact(batch);
        } catch (Throwable t) {
            throw fail("zlink_mesh_receive_batch_reset", t);
        }
    }

    public static int receiveBatchDestroy(MemorySegment handle) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment holder = arena.allocate(ValueLayout.ADDRESS);
            holder.set(ValueLayout.ADDRESS, 0, handle);
            return (int) RECEIVE_BATCH_DESTROY.invokeExact(holder);
        } catch (Throwable t) {
            throw fail("zlink_mesh_receive_batch_destroy", t);
        }
    }

    public static int claimRecvBatch(MemorySegment claim, MemorySegment batch, MemorySegment requiredOut,
                              int flags) {
        try {
            return (int) CLAIM_RECV_BATCH.invokeExact(claim, batch, requiredOut, flags);
        } catch (Throwable t) {
            throw fail("zlink_mesh_claim_recv_batch", t);
        }
    }

    public static long receiveBatchCount(MemorySegment batch) {
        try {
            return (long) RECEIVE_BATCH_COUNT.invokeExact(batch);
        } catch (Throwable t) {
            throw fail("zlink_mesh_receive_batch_count", t);
        }
    }

    public static MemorySegment receiveBatchData(MemorySegment batch) {
        try {
            return (MemorySegment) RECEIVE_BATCH_DATA.invokeExact(batch);
        } catch (Throwable t) {
            throw fail("zlink_mesh_receive_batch_data", t);
        }
    }

    public static int receiveBatchRetainMessage(MemorySegment batch, long index, MemorySegment partsOut,
                                         MemorySegment countInout) {
        try {
            return (int) RECEIVE_BATCH_RETAIN_MESSAGE.invokeExact(batch, index, partsOut, countInout);
        } catch (Throwable t) {
            throw fail("zlink_mesh_receive_batch_retain_message", t);
        }
    }

    public static int meshReply(MemorySegment token, MemorySegment parts, long count, int flags) {
        try {
            return (int) MESH_REPLY.invokeExact(token, parts, count, flags);
        } catch (Throwable t) {
            throw fail("zlink_mesh_reply", t);
        }
    }

    public static MemorySegment streamSessionNew(MemorySegment node, MemorySegment stream) {
        try {
            return (MemorySegment) STREAM_SESSION_NEW.invokeExact(node, stream);
        } catch (Throwable t) {
            throw fail("zlink_stream_session_service_new", t);
        }
    }

    public static int streamSessionStart(MemorySegment svc) {
        try {
            return (int) STREAM_SESSION_START.invokeExact(svc);
        } catch (Throwable t) {
            throw fail("zlink_stream_session_service_start", t);
        }
    }

    public static int streamSessionShutdown(MemorySegment svc, int timeoutMs) {
        try {
            return (int) STREAM_SESSION_SHUTDOWN.invokeExact(svc, timeoutMs);
        } catch (Throwable t) {
            throw fail("zlink_stream_session_service_shutdown", t);
        }
    }

    public static int streamSessionDestroy(MemorySegment handle) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment holder = arena.allocate(ValueLayout.ADDRESS);
            holder.set(ValueLayout.ADDRESS, 0, handle);
            return (int) STREAM_SESSION_DESTROY.invokeExact(holder);
        } catch (Throwable t) {
            throw fail("zlink_stream_session_service_destroy", t);
        }
    }

    public static int streamSessionStatus(MemorySegment svc, MemorySegment statusOut) {
        try {
            return (int) STREAM_SESSION_STATUS.invokeExact(svc, statusOut);
        } catch (Throwable t) {
            throw fail("zlink_stream_session_service_status", t);
        }
    }

    public static int streamSessionBindActor(MemorySegment svc, MemorySegment sessionRid,
                                      MemorySegment ref, MemorySegment opidOut, int timeoutMs) {
        try {
            return (int) STREAM_SESSION_BIND_ACTOR.invokeExact(svc, sessionRid, ref, opidOut, timeoutMs);
        } catch (Throwable t) {
            throw fail("zlink_stream_session_bind_actor", t);
        }
    }

    public static int streamSessionUnbindActor(MemorySegment svc, MemorySegment sessionRid,
                                        MemorySegment ref, long expectedGen, MemorySegment opidOut,
                                        int timeoutMs) {
        try {
            return (int) STREAM_SESSION_UNBIND_ACTOR.invokeExact(svc, sessionRid, ref, expectedGen,
                opidOut, timeoutMs);
        } catch (Throwable t) {
            throw fail("zlink_stream_session_unbind_actor", t);
        }
    }

    public static int streamSessionBindings(MemorySegment svc, MemorySegment sessionRid,
                                     MemorySegment entries, MemorySegment count) {
        try {
            return (int) STREAM_SESSION_BINDINGS.invokeExact(svc, sessionRid, entries, count);
        } catch (Throwable t) {
            throw fail("zlink_stream_session_bindings", t);
        }
    }

    public static int streamSessionSendToActor(MemorySegment svc, MemorySegment sessionRid,
                                        MemorySegment ref, MemorySegment meta, MemorySegment parts,
                                        long count, int flags) {
        try {
            return (int) STREAM_SESSION_SEND_TO_ACTOR.invokeExact(svc, sessionRid, ref, meta, parts,
                count, flags);
        } catch (Throwable t) {
            throw fail("zlink_stream_session_send_to_actor", t);
        }
    }

    public static int streamSessionRequestToActor(MemorySegment svc, MemorySegment sessionRid,
                                           MemorySegment ref, MemorySegment meta, MemorySegment parts,
                                           long count, MemorySegment opidOut, int flags,
                                           int timeoutMs) {
        try {
            return (int) STREAM_SESSION_REQUEST_TO_ACTOR.invokeExact(svc, sessionRid, ref, meta,
                parts, count, opidOut, flags, timeoutMs);
        } catch (Throwable t) {
            throw fail("zlink_stream_session_request_to_actor", t);
        }
    }

    public static int actorSendBoundSession(MemorySegment node, MemorySegment ref, MemorySegment parts,
                                     long count, int flags) {
        try {
            return (int) ACTOR_SEND_BOUND_SESSION.invokeExact(node, ref, parts, count, flags);
        } catch (Throwable t) {
            throw fail("zlink_mesh_node_actor_send_bound_session", t);
        }
    }

    public static int actorCloseBoundSession(MemorySegment node, MemorySegment ref, long expectedGen,
                                      MemorySegment opidOut, int timeoutMs) {
        try {
            return (int) ACTOR_CLOSE_BOUND_SESSION.invokeExact(node, ref, expectedGen, opidOut, timeoutMs);
        } catch (Throwable t) {
            throw fail("zlink_mesh_node_actor_close_bound_session", t);
        }
    }
}
