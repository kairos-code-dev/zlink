/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.nativeapi;

import systems.zlink.contracts.service.spot.ActorJoinEntrySpotResult;
import systems.zlink.contracts.service.spot.ActorJoinResult;
import systems.zlink.contracts.service.spot.ActorLookupResult;
import systems.zlink.contracts.service.spot.ActorRecvInfo;
import systems.zlink.contracts.service.spot.ActorRef;
import systems.zlink.contracts.service.spot.ActorRoute;
import systems.zlink.contracts.sockets.RequestResult;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.service.spot.SpotActorLifecycleInfo;
import systems.zlink.contracts.service.spot.SpotNodeActorEntry;
import systems.zlink.contracts.service.spot.SpotNodeSpotEntry;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.nio.charset.StandardCharsets;
import java.util.Optional;

public final class ActorInterop {
    private ActorInterop() {
    }

    public static MemorySegment nativeRoutingId(Arena arena, RoutingId rid) {
        byte[] value = InternalAccess.routingIdTrustedBytes(rid);
        MemorySegment nativeRid = arena.allocate(NativeLayouts.ROUTING_ID_LAYOUT);
        nativeRid.set(ValueLayout.JAVA_BYTE,
          NativeLayouts.ROUTING_ID_SIZE_OFFSET, (byte) value.length);
        if (value.length > 0) {
            MemorySegment.copy(MemorySegment.ofArray(value), 0, nativeRid,
              NativeLayouts.ROUTING_ID_DATA_OFFSET, value.length);
        }
        return nativeRid;
    }

    public static RoutingId readRoutingId(MemorySegment nativeRid) {
        if (nativeRid == null || nativeRid.address() == 0) {
            return null;
        }
        int size = nativeRid.get(ValueLayout.JAVA_BYTE,
          NativeLayouts.ROUTING_ID_SIZE_OFFSET) & 0xFF;
        byte[] value = new byte[size];
        if (size > 0) {
            MemorySegment.copy(nativeRid, NativeLayouts.ROUTING_ID_DATA_OFFSET,
              MemorySegment.ofArray(value), 0, size);
        }
        return InternalAccess.routingIdFromTrusted(value);
    }

    public static MemorySegment actorRefToNative(Arena arena, ActorRef ref) {
        MemorySegment out = arena.allocate(NativeLayouts.ACTOR_REF_LAYOUT);
        writeActorRef(out, ref);
        return out;
    }

    public static void writeActorRef(MemorySegment out, ActorRef ref) {
        writeRoutingId(out.asSlice(NativeLayouts.ACTOR_REF_NODE_RID_OFFSET,
          NativeLayouts.ROUTING_ID_LAYOUT.byteSize()), ref.nodeRid());
        writeCString(out.asSlice(NativeLayouts.ACTOR_REF_ACTOR_ID_OFFSET,
          NativeLayouts.ACTOR_ID_MAX), ref.actorId(),
          NativeLayouts.ACTOR_ID_MAX);
        out.set(ValueLayout.JAVA_LONG_UNALIGNED,
          NativeLayouts.ACTOR_REF_GENERATION_OFFSET, ref.generation());
    }

    public static ActorRef actorRefFromNative(MemorySegment segment) {
        MemorySegment view = segment.reinterpret(
          NativeLayouts.ACTOR_REF_LAYOUT.byteSize());
        return new ActorRef(
          readRoutingId(view.asSlice(NativeLayouts.ACTOR_REF_NODE_RID_OFFSET,
            NativeLayouts.ROUTING_ID_LAYOUT.byteSize())),
          NativeHelpers.fromCString(view.asSlice(
            NativeLayouts.ACTOR_REF_ACTOR_ID_OFFSET,
            NativeLayouts.ACTOR_ID_MAX), NativeLayouts.ACTOR_ID_MAX),
          view.get(ValueLayout.JAVA_LONG_UNALIGNED,
            NativeLayouts.ACTOR_REF_GENERATION_OFFSET));
    }

    public static ActorRecvInfo actorRecvInfoFromNative(MemorySegment segment) {
        MemorySegment view = segment.reinterpret(
          NativeLayouts.ACTOR_RECV_INFO_LAYOUT.byteSize());
        return new ActorRecvInfo(
          actorRefFromNative(view.asSlice(
            NativeLayouts.ACTOR_RECV_INFO_ACTOR_OFFSET,
            NativeLayouts.ACTOR_REF_LAYOUT.byteSize())),
          readRoutingId(view.asSlice(
            NativeLayouts.ACTOR_RECV_INFO_SOURCE_NODE_RID_OFFSET,
            NativeLayouts.ROUTING_ID_LAYOUT.byteSize())),
          readRoutingId(view.asSlice(
            NativeLayouts.ACTOR_RECV_INFO_SOURCE_SESSION_RID_OFFSET,
            NativeLayouts.ROUTING_ID_LAYOUT.byteSize())),
          view.get(ValueLayout.JAVA_LONG_UNALIGNED,
            NativeLayouts.ACTOR_RECV_INFO_REQUEST_ID_OFFSET),
          view.get(ValueLayout.JAVA_INT,
            NativeLayouts.ACTOR_RECV_INFO_FLAGS_OFFSET));
    }

    public static MemorySegment actorRecvInfoToNative(Arena arena,
                                                      ActorRecvInfo info) {
        MemorySegment out = arena.allocate(NativeLayouts.ACTOR_RECV_INFO_LAYOUT);
        writeActorRef(out.asSlice(NativeLayouts.ACTOR_RECV_INFO_ACTOR_OFFSET,
          NativeLayouts.ACTOR_REF_LAYOUT.byteSize()), info.actor());
        writeRoutingId(out.asSlice(
          NativeLayouts.ACTOR_RECV_INFO_SOURCE_NODE_RID_OFFSET,
          NativeLayouts.ROUTING_ID_LAYOUT.byteSize()), info.sourceNodeRid());
        writeRoutingId(out.asSlice(
          NativeLayouts.ACTOR_RECV_INFO_SOURCE_SESSION_RID_OFFSET,
          NativeLayouts.ROUTING_ID_LAYOUT.byteSize()), info.sourceSessionRid());
        out.set(ValueLayout.JAVA_LONG_UNALIGNED,
          NativeLayouts.ACTOR_RECV_INFO_REQUEST_ID_OFFSET, info.requestId());
        out.set(ValueLayout.JAVA_INT,
          NativeLayouts.ACTOR_RECV_INFO_FLAGS_OFFSET, info.flags());
        return out;
    }

    public static ActorRoute actorRouteFromNative(MemorySegment segment) {
        MemorySegment view = segment.reinterpret(
          NativeLayouts.ACTOR_ROUTE_LAYOUT.byteSize());
        return new ActorRoute(
          actorRefFromNative(view.asSlice(
            NativeLayouts.ACTOR_ROUTE_ACTOR_OFFSET,
            NativeLayouts.ACTOR_REF_LAYOUT.byteSize())),
          readRoutingId(view.asSlice(
            NativeLayouts.ACTOR_ROUTE_CURRENT_SPOT_RID_OFFSET,
            NativeLayouts.ROUTING_ID_LAYOUT.byteSize())),
          EnumCodecs.spotKindFromValue(view.get(ValueLayout.JAVA_INT,
            NativeLayouts.ACTOR_ROUTE_CURRENT_SPOT_KIND_OFFSET)));
    }

    public static SpotNodeSpotEntry spotEntryFromNative(MemorySegment segment) {
        MemorySegment view = segment.reinterpret(
          NativeLayouts.SPOT_NODE_SPOT_ENTRY_LAYOUT.byteSize());
        return new SpotNodeSpotEntry(
          readRoutingId(view.asSlice(
            NativeLayouts.SPOT_NODE_SPOT_ENTRY_SPOT_RID_OFFSET,
            NativeLayouts.ROUTING_ID_LAYOUT.byteSize())),
          EnumCodecs.spotKindFromValue(view.get(ValueLayout.JAVA_INT,
            NativeLayouts.SPOT_NODE_SPOT_ENTRY_SPOT_KIND_OFFSET)),
          view.get(ValueLayout.JAVA_INT,
            NativeLayouts.SPOT_NODE_SPOT_ENTRY_DISPATCH_HANDLER_ATTACHED_OFFSET) != 0,
          view.get(ValueLayout.JAVA_INT,
            NativeLayouts.SPOT_NODE_SPOT_ENTRY_JOINED_ACTOR_COUNT_OFFSET),
          view.get(ValueLayout.JAVA_INT,
            NativeLayouts.SPOT_NODE_SPOT_ENTRY_PENDING_ACTOR_JOIN_COUNT_OFFSET),
          view.get(ValueLayout.JAVA_INT,
            NativeLayouts.SPOT_NODE_SPOT_ENTRY_ROUTE_SYNCED_OFFSET) != 0,
          view.get(ValueLayout.JAVA_LONG_UNALIGNED,
            NativeLayouts.SPOT_NODE_SPOT_ENTRY_LAST_CHANGED_MS_OFFSET));
    }

    public static SpotNodeActorEntry actorEntryFromNative(
      MemorySegment segment) {
        MemorySegment view = segment.reinterpret(
          NativeLayouts.SPOT_NODE_ACTOR_ENTRY_LAYOUT.byteSize());
        return new SpotNodeActorEntry(
          actorRefFromNative(view.asSlice(
            NativeLayouts.SPOT_NODE_ACTOR_ENTRY_ACTOR_OFFSET,
            NativeLayouts.ACTOR_REF_LAYOUT.byteSize())),
          readRoutingId(view.asSlice(
            NativeLayouts.SPOT_NODE_ACTOR_ENTRY_CURRENT_SPOT_RID_OFFSET,
            NativeLayouts.ROUTING_ID_LAYOUT.byteSize())),
          EnumCodecs.spotKindFromValue(view.get(ValueLayout.JAVA_INT,
            NativeLayouts.SPOT_NODE_ACTOR_ENTRY_CURRENT_SPOT_KIND_OFFSET)),
          view.get(ValueLayout.JAVA_INT,
            NativeLayouts.SPOT_NODE_ACTOR_ENTRY_ROUTE_SYNCED_OFFSET) != 0,
          view.get(ValueLayout.JAVA_INT,
            NativeLayouts.SPOT_NODE_ACTOR_ENTRY_PENDING_MESSAGE_COUNT_OFFSET),
          view.get(ValueLayout.JAVA_LONG_UNALIGNED,
            NativeLayouts.SPOT_NODE_ACTOR_ENTRY_LAST_CHANGED_MS_OFFSET));
    }

    public static ActorJoinResult actorJoinResultFromNative(MemorySegment segment) {
        MemorySegment view = segment.reinterpret(
          NativeLayouts.ACTOR_JOIN_RESULT_LAYOUT.byteSize());
        return new ActorJoinResult(
          RequestResult.fromValue(view.get(ValueLayout.JAVA_INT,
            NativeLayouts.ACTOR_JOIN_RESULT_RESULT_OFFSET)),
          view.get(ValueLayout.JAVA_INT,
            NativeLayouts.ACTOR_JOIN_RESULT_JOIN_RESULT_CODE_OFFSET),
          actorRefFromNative(view.asSlice(
            NativeLayouts.ACTOR_JOIN_RESULT_ACTOR_OFFSET,
            NativeLayouts.ACTOR_REF_LAYOUT.byteSize())),
          readRoutingId(view.asSlice(
            NativeLayouts.ACTOR_JOIN_RESULT_JOINED_SPOT_RID_OFFSET,
            NativeLayouts.ROUTING_ID_LAYOUT.byteSize())),
          view.get(ValueLayout.JAVA_LONG_UNALIGNED,
            NativeLayouts.ACTOR_JOIN_RESULT_JOIN_EPOCH_OFFSET),
          view.get(ValueLayout.JAVA_INT,
            NativeLayouts.ACTOR_JOIN_RESULT_FLAGS_OFFSET));
    }

    public static ActorJoinEntrySpotResult actorJoinEntrySpotResultFromNative(
      MemorySegment segment) {
        MemorySegment view = segment.reinterpret(
          NativeLayouts.ACTOR_JOIN_ENTRY_SPOT_RESULT_LAYOUT.byteSize());
        return new ActorJoinEntrySpotResult(
          RequestResult.fromValue(view.get(ValueLayout.JAVA_INT,
            NativeLayouts.ACTOR_JOIN_ENTRY_SPOT_RESULT_RESULT_OFFSET)),
          view.get(ValueLayout.JAVA_INT,
            NativeLayouts.ACTOR_JOIN_ENTRY_SPOT_RESULT_JOIN_RESULT_CODE_OFFSET),
          actorRefFromNative(view.asSlice(
            NativeLayouts.ACTOR_JOIN_ENTRY_SPOT_RESULT_ACTOR_OFFSET,
            NativeLayouts.ACTOR_REF_LAYOUT.byteSize())),
          readRoutingId(view.asSlice(
            NativeLayouts.ACTOR_JOIN_ENTRY_SPOT_RESULT_TARGET_NODE_RID_OFFSET,
            NativeLayouts.ROUTING_ID_LAYOUT.byteSize())),
          readRoutingId(view.asSlice(
            NativeLayouts.ACTOR_JOIN_ENTRY_SPOT_RESULT_JOINED_SPOT_RID_OFFSET,
            NativeLayouts.ROUTING_ID_LAYOUT.byteSize())),
          view.get(ValueLayout.JAVA_LONG_UNALIGNED,
            NativeLayouts.ACTOR_JOIN_ENTRY_SPOT_RESULT_JOIN_EPOCH_OFFSET),
          view.get(ValueLayout.JAVA_INT,
            NativeLayouts.ACTOR_JOIN_ENTRY_SPOT_RESULT_FLAGS_OFFSET));
    }

    public static ActorLookupResult actorLookupResultFromNative(MemorySegment segment) {
        MemorySegment view = segment.reinterpret(
          NativeLayouts.ACTOR_LOOKUP_RESULT_LAYOUT.byteSize());
        return new ActorLookupResult(
          RequestResult.fromValue(view.get(ValueLayout.JAVA_INT,
            NativeLayouts.ACTOR_LOOKUP_RESULT_RESULT_OFFSET)),
          actorRefFromNative(view.asSlice(
            NativeLayouts.ACTOR_LOOKUP_RESULT_ACTOR_OFFSET,
            NativeLayouts.ACTOR_REF_LAYOUT.byteSize())),
          view.get(ValueLayout.JAVA_INT,
            NativeLayouts.ACTOR_LOOKUP_RESULT_FLAGS_OFFSET));
    }

    public static SpotActorLifecycleInfo lifecycleInfoFromNative(MemorySegment segment) {
        MemorySegment view = segment.reinterpret(
          NativeLayouts.SPOT_ACTOR_LIFECYCLE_INFO_LAYOUT.byteSize());
        RoutingId previousSpotRid = readRoutingId(view.asSlice(
          NativeLayouts.SPOT_ACTOR_LIFECYCLE_INFO_PREVIOUS_SPOT_RID_OFFSET,
          NativeLayouts.ROUTING_ID_LAYOUT.byteSize()));
        RoutingId currentSpotRid = readRoutingId(view.asSlice(
          NativeLayouts.SPOT_ACTOR_LIFECYCLE_INFO_CURRENT_SPOT_RID_OFFSET,
          NativeLayouts.ROUTING_ID_LAYOUT.byteSize()));
        return new SpotActorLifecycleInfo(
          actorRefFromNative(view.asSlice(
            NativeLayouts.SPOT_ACTOR_LIFECYCLE_INFO_PREVIOUS_ACTOR_OFFSET,
            NativeLayouts.ACTOR_REF_LAYOUT.byteSize())),
          actorRefFromNative(view.asSlice(
            NativeLayouts.SPOT_ACTOR_LIFECYCLE_INFO_CURRENT_ACTOR_OFFSET,
            NativeLayouts.ACTOR_REF_LAYOUT.byteSize())),
          previousSpotRid == null || previousSpotRid.size() == 0
            ? Optional.empty() : Optional.of(previousSpotRid),
          currentSpotRid == null || currentSpotRid.size() == 0
            ? Optional.empty() : Optional.of(currentSpotRid),
          view.get(ValueLayout.JAVA_LONG_UNALIGNED,
            NativeLayouts.SPOT_ACTOR_LIFECYCLE_INFO_JOIN_EPOCH_OFFSET),
          view.get(ValueLayout.JAVA_INT,
            NativeLayouts.SPOT_ACTOR_LIFECYCLE_INFO_FLAGS_OFFSET));
    }

    private static void writeRoutingId(MemorySegment out, RoutingId rid) {
        byte[] value = InternalAccess.routingIdTrustedBytes(rid);
        out.set(ValueLayout.JAVA_BYTE, NativeLayouts.ROUTING_ID_SIZE_OFFSET,
          (byte) value.length);
        if (value.length > 0) {
            MemorySegment.copy(MemorySegment.ofArray(value), 0, out,
              NativeLayouts.ROUTING_ID_DATA_OFFSET, value.length);
        }
    }

    private static void writeCString(MemorySegment out, String value, int maxLen) {
        byte[] bytes = value.getBytes(StandardCharsets.UTF_8);
        if (bytes.length >= maxLen) {
            throw new IllegalArgumentException(
              "value exceeds fixed buffer size " + (maxLen - 1));
        }
        if (bytes.length > 0) {
            MemorySegment.copy(MemorySegment.ofArray(bytes), 0, out, 0,
              bytes.length);
        }
        out.set(ValueLayout.JAVA_BYTE, bytes.length, (byte) 0);
    }
}
