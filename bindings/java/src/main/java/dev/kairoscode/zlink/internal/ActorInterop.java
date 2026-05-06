/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.internal;

import dev.kairoscode.zlink.RoutingId;
import dev.kairoscode.zlink.service.spot.ActorCreateResult;
import dev.kairoscode.zlink.service.spot.ActorCreateStatus;
import dev.kairoscode.zlink.service.spot.ActorRecvInfo;
import dev.kairoscode.zlink.service.spot.ActorRef;
import dev.kairoscode.zlink.service.spot.ActorRoute;
import dev.kairoscode.zlink.service.spot.SpotNodeActorEntry;
import dev.kairoscode.zlink.service.spot.SpotNodeSpotEntry;
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
          readCString(view.asSlice(NativeLayouts.ACTOR_REF_ACTOR_ID_OFFSET,
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
          view.get(ValueLayout.JAVA_INT,
            NativeLayouts.ACTOR_RECV_INFO_FLAGS_OFFSET));
    }

    public static ActorCreateResult actorCreateResultFromNative(
      MemorySegment segment) {
        MemorySegment view = segment.reinterpret(
          NativeLayouts.ACTOR_CREATE_RESULT_LAYOUT.byteSize());
        return new ActorCreateResult(
          ActorCreateStatus.fromValue(view.get(ValueLayout.JAVA_INT,
            NativeLayouts.ACTOR_CREATE_RESULT_STATUS_OFFSET)),
          actorRefFromNative(view.asSlice(
            NativeLayouts.ACTOR_CREATE_RESULT_ACTOR_OFFSET,
            NativeLayouts.ACTOR_REF_LAYOUT.byteSize())));
    }

    public static ActorRoute actorRouteFromNative(MemorySegment segment) {
        MemorySegment view = segment.reinterpret(
          NativeLayouts.ACTOR_ROUTE_LAYOUT.byteSize());
        boolean joined = view.get(ValueLayout.JAVA_INT,
          NativeLayouts.ACTOR_ROUTE_JOINED_OFFSET) != 0;
        return new ActorRoute(
          actorRefFromNative(view.asSlice(
            NativeLayouts.ACTOR_ROUTE_ACTOR_OFFSET,
            NativeLayouts.ACTOR_REF_LAYOUT.byteSize())),
          joined,
          joined
            ? Optional.ofNullable(readRoutingId(view.asSlice(
                NativeLayouts.ACTOR_ROUTE_JOINED_SPOT_RID_OFFSET,
                NativeLayouts.ROUTING_ID_LAYOUT.byteSize())))
            : Optional.empty());
    }

    public static SpotNodeSpotEntry spotEntryFromNative(MemorySegment segment) {
        MemorySegment view = segment.reinterpret(
          NativeLayouts.SPOT_NODE_SPOT_ENTRY_LAYOUT.byteSize());
        return new SpotNodeSpotEntry(
          readRoutingId(view.asSlice(
            NativeLayouts.SPOT_NODE_SPOT_ENTRY_SPOT_RID_OFFSET,
            NativeLayouts.ROUTING_ID_LAYOUT.byteSize())),
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
        boolean joined = view.get(ValueLayout.JAVA_INT,
          NativeLayouts.SPOT_NODE_ACTOR_ENTRY_JOINED_OFFSET) != 0;
        return new SpotNodeActorEntry(
          actorRefFromNative(view.asSlice(
            NativeLayouts.SPOT_NODE_ACTOR_ENTRY_ACTOR_OFFSET,
            NativeLayouts.ACTOR_REF_LAYOUT.byteSize())),
          joined,
          joined
            ? Optional.ofNullable(readRoutingId(view.asSlice(
                NativeLayouts.SPOT_NODE_ACTOR_ENTRY_JOINED_SPOT_RID_OFFSET,
                NativeLayouts.ROUTING_ID_LAYOUT.byteSize())))
            : Optional.empty(),
          view.get(ValueLayout.JAVA_INT,
            NativeLayouts.SPOT_NODE_ACTOR_ENTRY_ROUTE_SYNCED_OFFSET) != 0,
          view.get(ValueLayout.JAVA_INT,
            NativeLayouts.SPOT_NODE_ACTOR_ENTRY_PENDING_MESSAGE_COUNT_OFFSET),
          view.get(ValueLayout.JAVA_LONG_UNALIGNED,
            NativeLayouts.SPOT_NODE_ACTOR_ENTRY_LAST_CHANGED_MS_OFFSET));
    }

    public static String readCString(MemorySegment segment, int maxLen) {
        int len = NativeHelpers.cStringLength(segment, maxLen);
        if (len == 0) {
            return "";
        }
        byte[] bytes = new byte[len];
        MemorySegment.copy(segment, 0, MemorySegment.ofArray(bytes), 0, len);
        return new String(bytes, StandardCharsets.UTF_8);
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
