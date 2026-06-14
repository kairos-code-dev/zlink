/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.service.spot;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.util.Objects;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.errors.ZlinkRecvException;
import systems.zlink.contracts.errors.ZlinkSubmitException;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.service.spot.ActorJoinInfo;
import systems.zlink.contracts.service.spot.ActorJoinReplyOperation;
import systems.zlink.contracts.service.spot.ActorJoinRequest;
import systems.zlink.contracts.sockets.RecvFlags;
import systems.zlink.contracts.sockets.RecvResult;
import systems.zlink.contracts.sockets.SubmitResult;
import systems.zlink.contracts.internal.ContractAccess;
import systems.zlink.runtime.nativeapi.ActorInterop;
import systems.zlink.runtime.nativeapi.InternalAccess;
import systems.zlink.runtime.nativeapi.MessagePartsBuffer;
import systems.zlink.runtime.nativeapi.Native;
import systems.zlink.runtime.nativeapi.NativeLayouts;
import systems.zlink.runtime.nativeapi.NativeMessage;

final class SpotActorJoinSupport {
    private final NativeSpot spot;

    SpotActorJoinSupport(NativeSpot spot) {
        this.spot = spot;
    }

    ActorJoinRequest recvActorJoin(RecvFlags flags) {
        Objects.requireNonNull(flags, "flags");
        spot.ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment infoOut = arena.allocate(
                NativeLayouts.ACTOR_JOIN_INFO_LAYOUT);
            MemorySegment partsOut = arena.allocate(ValueLayout.ADDRESS);
            MemorySegment partCountOut = arena.allocate(ValueLayout.JAVA_LONG);
            Message[] messages = null;
            boolean success = false;
            try {
                int rc = Native.spotActorJoinRecv(spot.handle(), infoOut,
                    partsOut, partCountOut, flags.value());
                if (rc != 0) {
                    if (flags == RecvFlags.DONT_WAIT
                        && rc == RecvResult.NO_DATA.value()) {
                        return null;
                    }
                    throw new ZlinkRecvException(RecvResult.fromValue(rc));
                }
                MemorySegment parts = partsOut.get(ValueLayout.ADDRESS, 0);
                long partCount = partCountOut.get(ValueLayout.JAVA_LONG, 0);
                messages = partCount > 0
                    ? InternalAccess.messageFromOwnedMessageVector(parts, partCount)
                    : new Message[] { new Message() };
                NativeMessage.multipartClose(parts, partCount);
                success = true;
                return ContractAccess.actorJoinRequest(
                    readActorJoinInfo(infoOut), java.util.List.of(messages));
            } finally {
                if (!success && messages != null) {
                    for (Message message : messages) {
                        message.close();
                    }
                }
            }
        }
    }

    ActorJoinReplyOperation replyActorJoin(ActorJoinRequest request,
                                           int joinResultCode) {
        Objects.requireNonNull(request, "request");
        spot.ensureOpen();
        return new ActorJoinReplyBuilder(request, joinResultCode);
    }

    private final class ActorJoinReplyBuilder implements ActorJoinReplyOperation {
        private final ActorJoinRequest request;
        private final int joinResultCode;
        private final MessagePartsBuffer parts = new MessagePartsBuffer();
        private boolean submitted;

        ActorJoinReplyBuilder(ActorJoinRequest request, int joinResultCode) {
            this.request = request;
            this.joinResultCode = joinResultCode;
        }

        @Override
        public ActorJoinReplyOperation message(Message part) {
            ensureNotSubmitted();
            parts.add(Objects.requireNonNull(part, "part"));
            return this;
        }

        @Override
        public void submit() {
            ensureNotSubmitted();
            submitted = true;
            spot.ensureOpen();
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment nativeInfo = arena.allocate(
                    NativeLayouts.ACTOR_JOIN_INFO_LAYOUT);
                writeActorJoinInfo(nativeInfo, request.info());
                MemorySegment partsArr = parts.copyToNativeArray(arena);
                int rc = Native.spotActorJoinReply(spot.handle(), nativeInfo,
                    joinResultCode, partsArr, parts.size());
                if (rc != 0) {
                    MessagePartsBuffer.closeNativeArray(partsArr, parts.size());
                    throw new ZlinkSubmitException(SubmitResult.fromValue(rc));
                }
            }
        }

        private void ensureNotSubmitted() {
            if (submitted)
                throw new IllegalStateException("operation already submitted");
        }
    }

    private static ActorJoinInfo readActorJoinInfo(MemorySegment segment) {
        MemorySegment view = segment.reinterpret(
            NativeLayouts.ACTOR_JOIN_INFO_LAYOUT.byteSize());
        return ContractAccess.actorJoinInfoFromNative(
            ActorInterop.actorRefFromNative(view.asSlice(
                NativeLayouts.ACTOR_JOIN_INFO_SOURCE_ACTOR_OFFSET,
                NativeLayouts.ACTOR_REF_LAYOUT.byteSize())),
            ActorInterop.actorRefFromNative(view.asSlice(
                NativeLayouts.ACTOR_JOIN_INFO_TARGET_ACTOR_OFFSET,
                NativeLayouts.ACTOR_REF_LAYOUT.byteSize())),
            ActorInterop.readRoutingId(view.asSlice(
                NativeLayouts.ACTOR_JOIN_INFO_SOURCE_NODE_RID_OFFSET,
                NativeLayouts.ROUTING_ID_LAYOUT.byteSize())),
            ActorInterop.readRoutingId(view.asSlice(
                NativeLayouts.ACTOR_JOIN_INFO_SOURCE_SPOT_RID_OFFSET,
                NativeLayouts.ROUTING_ID_LAYOUT.byteSize())),
            ActorInterop.readRoutingId(view.asSlice(
                NativeLayouts.ACTOR_JOIN_INFO_TARGET_NODE_RID_OFFSET,
                NativeLayouts.ROUTING_ID_LAYOUT.byteSize())),
            ActorInterop.readRoutingId(view.asSlice(
                NativeLayouts.ACTOR_JOIN_INFO_TARGET_SPOT_RID_OFFSET,
                NativeLayouts.ROUTING_ID_LAYOUT.byteSize())),
            view.get(ValueLayout.JAVA_LONG_UNALIGNED,
                NativeLayouts.ACTOR_JOIN_INFO_JOIN_EPOCH_OFFSET),
            view.get(ValueLayout.ADDRESS,
                NativeLayouts.ACTOR_JOIN_INFO_REQUEST_OFFSET),
            view.get(ValueLayout.JAVA_INT,
                NativeLayouts.ACTOR_JOIN_INFO_FLAGS_OFFSET));
    }

    private static void writeActorJoinInfo(MemorySegment out,
                                           ActorJoinInfo info) {
        ActorInterop.writeActorRef(out.asSlice(
            NativeLayouts.ACTOR_JOIN_INFO_SOURCE_ACTOR_OFFSET,
            NativeLayouts.ACTOR_REF_LAYOUT.byteSize()), info.sourceActor());
        ActorInterop.writeActorRef(out.asSlice(
            NativeLayouts.ACTOR_JOIN_INFO_TARGET_ACTOR_OFFSET,
            NativeLayouts.ACTOR_REF_LAYOUT.byteSize()), info.targetActor());
        writeRoutingId(out.asSlice(
            NativeLayouts.ACTOR_JOIN_INFO_SOURCE_NODE_RID_OFFSET,
            NativeLayouts.ROUTING_ID_LAYOUT.byteSize()),
            ContractAccess.actorJoinInfoSourceNodeRidRaw(info));
        writeRoutingId(out.asSlice(
            NativeLayouts.ACTOR_JOIN_INFO_SOURCE_SPOT_RID_OFFSET,
            NativeLayouts.ROUTING_ID_LAYOUT.byteSize()),
            ContractAccess.actorJoinInfoSourceSpotRidRaw(info));
        writeRoutingId(out.asSlice(
            NativeLayouts.ACTOR_JOIN_INFO_TARGET_NODE_RID_OFFSET,
            NativeLayouts.ROUTING_ID_LAYOUT.byteSize()),
            ContractAccess.actorJoinInfoTargetNodeRidRaw(info));
        writeRoutingId(out.asSlice(
            NativeLayouts.ACTOR_JOIN_INFO_TARGET_SPOT_RID_OFFSET,
            NativeLayouts.ROUTING_ID_LAYOUT.byteSize()),
            ContractAccess.actorJoinInfoTargetSpotRidRaw(info));
        out.set(ValueLayout.JAVA_LONG_UNALIGNED,
            NativeLayouts.ACTOR_JOIN_INFO_JOIN_EPOCH_OFFSET, info.joinEpoch());
        out.set(ValueLayout.ADDRESS,
            NativeLayouts.ACTOR_JOIN_INFO_REQUEST_OFFSET,
            actorJoinRequestState(info));
        out.set(ValueLayout.JAVA_INT,
            NativeLayouts.ACTOR_JOIN_INFO_FLAGS_OFFSET, info.flags());
    }

    private static void writeRoutingId(MemorySegment out, RoutingId rid) {
        byte[] value = rid == null ? new byte[0]
            : InternalAccess.routingIdTrustedBytes(rid);
        out.set(ValueLayout.JAVA_BYTE, NativeLayouts.ROUTING_ID_SIZE_OFFSET,
            (byte) value.length);
        if (value.length > 0) {
            MemorySegment.copy(MemorySegment.ofArray(value), 0, out,
                NativeLayouts.ROUTING_ID_DATA_OFFSET, value.length);
        }
    }

    private static MemorySegment actorJoinRequestState(ActorJoinInfo info) {
        Object state = ContractAccess.actorJoinInfoRequestState(info);
        return state instanceof MemorySegment segment ? segment
            : MemorySegment.NULL;
    }
}
