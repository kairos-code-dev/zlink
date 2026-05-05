/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.spot;

import dev.kairoscode.zlink.ConfigException;
import dev.kairoscode.zlink.ConfigResult;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.RecvException;
import dev.kairoscode.zlink.RecvFlags;
import dev.kairoscode.zlink.RecvResult;
import dev.kairoscode.zlink.RequestException;
import dev.kairoscode.zlink.RequestResult;
import dev.kairoscode.zlink.SendFlags;
import dev.kairoscode.zlink.SubmitException;
import dev.kairoscode.zlink.SubmitResult;
import dev.kairoscode.zlink.internal.ActorInterop;
import dev.kairoscode.zlink.internal.InternalAccess;
import dev.kairoscode.zlink.internal.Native;
import dev.kairoscode.zlink.internal.NativeLayouts;
import dev.kairoscode.zlink.internal.NativeMsg;
import dev.kairoscode.zlink.internal.RequestProgressPump;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.time.Duration;
import java.util.List;
import java.util.Objects;
import java.util.function.BiConsumer;

public final class Actor implements AutoCloseable {
    private MemorySegment handle;

    Actor(MemorySegment handle) {
        Objects.requireNonNull(handle, "handle");
        if (handle.address() == 0) {
            throw new IllegalArgumentException("actor handle must not be null");
        }
        this.handle = handle;
    }

    MemorySegment handle() {
        return handle;
    }

    public ActorRef ref() {
        ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment out = arena.allocate(NativeLayouts.ACTOR_REF_LAYOUT);
            int rc = Native.actorGetRef(handle, out);
            if (rc != 0) {
                throw new ConfigException(ConfigResult.fromValue(rc));
            }
            return ActorInterop.actorRefFromNative(out);
        }
    }

    public boolean join(Spot spot,
                        Message message,
                        BiConsumer<RequestResult, List<Message>> callback,
                        Duration timeout) {
        Objects.requireNonNull(spot, "spot");
        Objects.requireNonNull(message, "message");
        Objects.requireNonNull(callback, "callback");
        ensureOpen();
        ActorRequestCallbacks.PendingToken pending =
          ActorRequestCallbacks.register(callback);
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeMsg = arena.allocate(NativeLayouts.MSG_LAYOUT);
            InternalAccess.messageCopyTo(message, nativeMsg);
            int rc = Native.actorJoinSpot(handle, InternalAccess.spotHandle(spot),
              nativeMsg, ActorRequestCallbacks.REPLY_CALLBACK,
              MemorySegment.ofAddress(pending.id()), SendFlags.NONE.value(),
              timeoutMillis(timeout));
            if (rc != 0) {
                ActorRequestCallbacks.remove(pending.id());
                NativeMsg.msgClose(nativeMsg);
                throw new SubmitException(SubmitResult.fromValue(rc));
            }
            RequestProgressPump.trackSpotRequest(pending.future(),
              InternalAccess.spotHandle(spot), "zlink-actor-join-progress");
            return true;
        }
    }

    public boolean join(Spot spot,
                        Message message,
                        BiConsumer<RequestResult, List<Message>> callback) {
        return join(spot, message, callback, Duration.ofMillis(5_000L));
    }

    public void leave(Spot spot) {
        Objects.requireNonNull(spot, "spot");
        ensureOpen();
        int rc = Native.actorLeaveSpot(handle, InternalAccess.spotHandle(spot));
        if (rc != 0) {
            throw new ConfigException(ConfigResult.fromValue(rc));
        }
    }

    public ActorPart recvPart(RecvFlags flags) {
        Objects.requireNonNull(flags, "flags");
        ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment infoOut = arena.allocate(
              NativeLayouts.ACTOR_RECV_INFO_LAYOUT);
            MemorySegment hasMoreOut = arena.allocate(ValueLayout.JAVA_INT);
            Message message = new Message();
            boolean success = false;
            try {
                int rc = Native.actorRecvPart(handle, infoOut,
                  InternalAccess.messageNativeHandle(message), hasMoreOut,
                  flags.value());
                if (rc != 0) {
                    if (flags == RecvFlags.DONT_WAIT
                        && rc == RecvResult.NO_DATA.value()) {
                        return null;
                    }
                    throw new RecvException(RecvResult.fromValue(rc));
                }
                boolean hasMore = hasMoreOut.get(ValueLayout.JAVA_INT, 0) != 0;
                InternalAccess.messageFinishReceive(message, hasMore);
                success = true;
                return new ActorPart(ActorInterop.actorRecvInfoFromNative(
                  infoOut), message, hasMore);
            } finally {
                if (!success) {
                    message.close();
                }
            }
        }
    }

    public ActorPart recvPart() {
        return recvPart(RecvFlags.NONE);
    }

    public boolean sendBoundSession(Message message, SendFlags flags) {
        Objects.requireNonNull(message, "message");
        Objects.requireNonNull(flags, "flags");
        ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeMsg = arena.allocate(NativeLayouts.MSG_LAYOUT);
            InternalAccess.messageCopyTo(message, nativeMsg);
            int rc = Native.actorSendBoundSessionMsg(handle, nativeMsg,
              flags.value());
            if (rc != 0) {
                NativeMsg.msgClose(nativeMsg);
                throw new SubmitException(SubmitResult.fromValue(rc));
            }
            return true;
        }
    }

    public boolean sendBoundSession(Message message) {
        return sendBoundSession(message, SendFlags.NONE);
    }

    public boolean sendBoundSessionPacket(Message header,
                                          Message body,
                                          SendFlags flags) {
        Objects.requireNonNull(header, "header");
        Objects.requireNonNull(body, "body");
        Objects.requireNonNull(flags, "flags");
        ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeHeader = arena.allocate(NativeLayouts.MSG_LAYOUT);
            MemorySegment nativeBody = arena.allocate(NativeLayouts.MSG_LAYOUT);
            InternalAccess.messageCopyTo(header, nativeHeader);
            InternalAccess.messageCopyTo(body, nativeBody);
            int rc = Native.actorSendBoundSessionPacket(handle, nativeHeader,
              nativeBody, flags.value());
            if (rc != 0) {
                NativeMsg.msgClose(nativeHeader);
                NativeMsg.msgClose(nativeBody);
                throw new SubmitException(SubmitResult.fromValue(rc));
            }
            return true;
        }
    }

    public boolean sendBoundSessionPacket(Message header, Message body) {
        return sendBoundSessionPacket(header, body, SendFlags.NONE);
    }

    @Override
    public void close() {
        close(Duration.ZERO);
    }

    public void close(Duration timeout) {
        MemorySegment current = handle;
        if (current == null || current.address() == 0) {
            return;
        }
        int rc = Native.actorDestroy(current, timeoutMillis(timeout));
        if (rc != 0) {
            throw new RequestException(RequestResult.fromValue(rc));
        }
        handle = MemorySegment.NULL;
    }

    private void ensureOpen() {
        if (handle == null || handle.address() == 0) {
            throw new IllegalStateException("actor is closed");
        }
    }

    private static int timeoutMillis(Duration timeout) {
        if (timeout == null || timeout.isZero()) {
            return 0;
        }
        long millis = Math.max(1L, timeout.toMillis());
        return millis >= Integer.MAX_VALUE ? Integer.MAX_VALUE : (int) millis;
    }
}
