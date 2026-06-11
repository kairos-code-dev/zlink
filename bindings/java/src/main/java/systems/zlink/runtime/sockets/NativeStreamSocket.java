/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.sockets;

import systems.zlink.internal.sockets.ReceiveFlag;

import systems.zlink.internal.sockets.SendFlag;

import systems.zlink.internal.ContractAccess;

import systems.zlink.contracts.sockets.*;

import systems.zlink.contracts.service.spot.ActorBindOperation;
import systems.zlink.contracts.service.spot.ActorRef;
import systems.zlink.contracts.service.spot.ActorUnbindOperation;
import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.messaging.Received;
import systems.zlink.contracts.service.spot.ReplyHandler;
import systems.zlink.contracts.errors.ZlinkRequestException;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.service.spot.SendOperation;
import systems.zlink.contracts.service.spot.SpotNode;
import systems.zlink.runtime.messaging.MessageOperations;
import systems.zlink.runtime.nativeapi.InternalAccess;
import systems.zlink.runtime.sockets.StreamUInt32FramedNativeHandler;
import systems.zlink.runtime.sockets.StreamUInt32RawNativeHandler;
import java.lang.foreign.MemorySegment;
import java.time.Duration;
import java.util.List;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;

final class NativeStreamSocket extends NativeSocketBase implements StreamSocket {
    private final StreamSocketOptions options = ContractAccess.streamSocketOptions(this);

    NativeStreamSocket(Context ctx) {
        super(ctx, SocketType.STREAM);
    }

    boolean send(int rid, Message part) {
        return super.send(RoutingId.from(Integer.toUnsignedLong(rid)), part,
            SendFlag.NONE);
    }
    boolean send(int rid, Message part, SendFlags flags) {
        return super.send(RoutingId.from(Integer.toUnsignedLong(rid)), part,
            SendFlag.fromValue(flags.value()));
    }
    SendResult sendNoWaitResult(int rid, Message part) {
        return super.sendNoWaitResult(RoutingId.from(Integer.toUnsignedLong(rid)),
            part);
    }
    SendResult sendNoWaitResult(RoutingId rid, Message part) {
        return super.sendNoWaitResult(rid, part);
    }
    int send(int rid, MemorySegment payload, int length, SendFlags flags) {
        return super.send(rid, payload, length, flags.value());
    }
    int sendCopied(int rid, MemorySegment payload, int length,
                   SendFlags flags) {
        return super.sendCopied(rid, payload, length, flags.value());
    }
    SendResult sendNoWaitResult(RoutingId rid, List<Message> parts) {
        return super.sendNoWaitResult(rid, parts);
    }
    public SendOperation send(RoutingId rid) {
        Objects.requireNonNull(rid, "rid");
        return MessageOperations.send((parts, flags) ->
            NativeStreamSocket.super.send(rid, parts,
                SendFlag.fromValue(flags.value())));
    }
    /** Canonical caller-provided storage recv. See doc/spec/bindings/README.md. */
    public boolean recv(Received result, RecvFlags flags) {
        Objects.requireNonNull(result, "result");
        Objects.requireNonNull(flags, "flags");
        Received fresh = super.recv(ReceiveFlag.fromValue(flags.value()));
        if (fresh == null) return false;
        ContractAccess.receivedAdoptFrom(result, fresh);
        result.getRoutingId().ifPresent(rid ->
            InternalAccess.receivedSetSendSender(result, (parts, sendFlags) -> super.send(rid, parts,
                SendFlag.fromValue(sendFlags.value()))));
        return true;
    }
    public void setSendReadyHandler(SendReadyHandler handler) { super.setSendReadyHandler(handler); }
    public void onPacket(StreamPacketHandler handler) {
        Objects.requireNonNull(handler, "handler");
        super.attachStreamPacket((StreamFramedPacketHandler)
            (routingId, header, body) -> handler.onPacket(routingId, header,
                body));
    }
    void onPacketNative(StreamUInt32RawNativeHandler handler) {
        super.attachStreamRaw(handler);
    }
    void onFramedPacket(StreamFramedPacketHandler handler) {
        super.attachStreamPacket(handler);
    }
    void onFramedPacket(StreamUInt32FramedPacketHandler handler) {
        super.attachStreamPacket(handler);
    }
    void onFramedPacketNative(StreamUInt32FramedNativeHandler handler) {
        super.attachStreamPacket(handler);
    }
    void detachStream() { super.detachStream(); }
    @Override
    public void close() {
        if (handle() == null || handle().address() == 0) {
            super.close();
            return;
        }
        try {
            detachStream();
        } finally {
            super.close();
        }
    }
    public void attachActorGateway(SpotNode node) {
        Objects.requireNonNull(node, "node");
        InternalAccess.streamAttachActorGateway(this, node);
    }

    public ActorBindOperation bindActor(RoutingId sessionRid, ActorRef actor) {
        Objects.requireNonNull(sessionRid, "sessionRid");
        Objects.requireNonNull(actor, "actor");
        return new ActorBindBuilder(sessionRid, actor);
    }

    public ActorUnbindOperation unbindActor(RoutingId sessionRid, String actorId) {
        Objects.requireNonNull(sessionRid, "sessionRid");
        Objects.requireNonNull(actorId, "actorId");
        return new ActorUnbindBuilder(sessionRid, actorId);
    }

    public SendOperation sendBoundActor(RoutingId sessionRid, String actorId) {
        Objects.requireNonNull(sessionRid, "sessionRid");
        Objects.requireNonNull(actorId, "actorId");
        return MessageOperations.send((parts, flags) ->
            sendBoundActorReceiveds(sessionRid, actorId, parts, flags));
    }

    public List<ActorRef> boundActors(RoutingId sessionRid) {
        Objects.requireNonNull(sessionRid, "sessionRid");
        return InternalAccess.streamBoundActors(this, sessionRid);
    }

    @Override public StreamSocketOptions options() { return options; }

    private CompletableFuture<List<Message>> submitBindStage(
      RoutingId sessionRid, ActorRef actor, Duration timeout) {
        CompletableFuture<List<Message>> future = new CompletableFuture<>();
        submitBind(sessionRid, actor, timeout, (result, parts) -> {
            if (result == RequestResult.OK) {
                future.complete(parts);
            } else {
                future.completeExceptionally(new ZlinkRequestException(result));
                parts.forEach(Message::close);
            }
        });
        return future;
    }

    private boolean submitBind(RoutingId sessionRid, ActorRef actor,
                               Duration timeout, ReplyHandler callback) {
        return InternalAccess.streamSubmitBind(this, sessionRid, actor,
            timeout, callback);
    }

    private CompletableFuture<List<Message>> submitUnbindStage(
      RoutingId sessionRid, String actorId, Duration timeout) {
        CompletableFuture<List<Message>> future = new CompletableFuture<>();
        submitUnbind(sessionRid, actorId, timeout, (result, parts) -> {
            if (result == RequestResult.OK) {
                future.complete(parts);
            } else {
                future.completeExceptionally(new ZlinkRequestException(result));
                parts.forEach(Message::close);
            }
        });
        return future;
    }

    private boolean submitUnbind(RoutingId sessionRid, String actorId,
                                 Duration timeout, ReplyHandler callback) {
        return InternalAccess.streamSubmitUnbind(this, sessionRid, actorId,
            timeout, callback);
    }

    private boolean sendBoundActorReceiveds(RoutingId sessionRid, String actorId,
                                        List<Message> parts, SendFlags flags) {
        return InternalAccess.streamSendBoundActorReceiveds(this, sessionRid,
            actorId, parts, flags);
    }

    private final class ActorBindBuilder implements ActorBindOperation {
        private final RoutingId sessionRid;
        private final ActorRef actor;
        private Duration timeout = Duration.ofMillis(5_000L);
        private boolean submitted;

        ActorBindBuilder(RoutingId sessionRid, ActorRef actor) {
            this.sessionRid = sessionRid;
            this.actor = actor;
        }

        @Override
        public ActorBindOperation timeout(Duration value) {
            ensureNotSubmitted();
            timeout = Objects.requireNonNull(value, "timeout");
            return this;
        }

        @Override
        public CompletableFuture<List<Message>> submit() {
            markSubmitted();
            return submitBindStage(sessionRid, actor, timeout);
        }

        @Override
        public boolean submit(ReplyHandler callback) {
            markSubmitted();
            return submitBind(sessionRid, actor, timeout, callback);
        }

        private void markSubmitted() {
            ensureNotSubmitted();
            submitted = true;
        }

        private void ensureNotSubmitted() {
            if (submitted)
                throw new IllegalStateException("operation already submitted");
        }
    }

    private final class ActorUnbindBuilder implements ActorUnbindOperation {
        private final RoutingId sessionRid;
        private final String actorId;
        private Duration timeout = Duration.ofMillis(5_000L);
        private boolean submitted;

        ActorUnbindBuilder(RoutingId sessionRid, String actorId) {
            this.sessionRid = sessionRid;
            this.actorId = actorId;
        }

        @Override
        public ActorUnbindOperation timeout(Duration value) {
            ensureNotSubmitted();
            timeout = Objects.requireNonNull(value, "timeout");
            return this;
        }

        @Override
        public CompletableFuture<List<Message>> submit() {
            markSubmitted();
            return submitUnbindStage(sessionRid, actorId, timeout);
        }

        @Override
        public boolean submit(ReplyHandler callback) {
            markSubmitted();
            return submitUnbind(sessionRid, actorId, timeout, callback);
        }

        private void markSubmitted() {
            ensureNotSubmitted();
            submitted = true;
        }

        private void ensureNotSubmitted() {
            if (submitted)
                throw new IllegalStateException("operation already submitted");
        }
    }

}
