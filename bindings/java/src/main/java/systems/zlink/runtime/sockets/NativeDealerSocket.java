/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.sockets;

import systems.zlink.contracts.sockets.*;

import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.service.discovery.Discovery;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.messaging.Received;
import systems.zlink.contracts.service.spot.RequestOperation;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.service.spot.SendOperation;
import systems.zlink.contracts.service.spot.SendSubmitOperation;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.time.Duration;
import java.util.List;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import systems.zlink.runtime.nativeapi.InternalAccess;

final class NativeDealerSocket extends NativeSocketBase implements DealerSocket {
    private static final boolean DEBUG_REQREP =
      Boolean.getBoolean("zlink.reqrep.debug");
    private final DealerSocketOptions options = new DealerSocketOptions(this);

    NativeDealerSocket(Context ctx) {
        super(ctx, SocketType.DEALER);
    }

    public void bind(String endpoint) { super.bind(endpoint); }
    public void connect(String endpoint) { super.connect(endpoint); }
    public void unbind(String endpoint) { super.unbind(endpoint); }
    public void disconnect(String endpoint) { super.disconnect(endpoint); }
    public void disconnectRid(RoutingId routingId) { super.disconnectRid(routingId); }
    public void attachDiscovery(Discovery discovery) { super.attachDiscovery(discovery); }
    public void setChannelName(String channelName) { super.setChannelName(channelName); }
    public String getChannelName() { return super.getChannelName(); }
    public void setRoutingId(RoutingId rid) { super.setRoutingId(rid); }
    public RoutingId getRoutingId() { return super.getRoutingId(); }
    public SendOperation send() {
        return new DealerSendBuilder();
    }
    SendResult sendNoWaitResult(Message part) { return super.sendNoWaitResult(part); }
    SendResult sendNoWaitResult(List<Message> parts) { return super.sendNoWaitResult(parts); }
    /**
     * Canonical caller-provided storage recv. Pass a long-lived
     * {@link Received} and the binding refills its internal state in place
     * each successful call.
     *
     * @return {@code true} on success, {@code false} when
     * {@link RecvFlags#DONT_WAIT} finds no data.
     */
    public boolean recv(Received result, RecvFlags flags) {
        java.util.Objects.requireNonNull(result, "result");
        java.util.Objects.requireNonNull(flags, "flags");
        return super.recvInto(result, ReceiveFlag.fromValue(flags.value()));
    }
    public void setSendReadyHandler(SendReadyHandler handler) { super.setSendReadyHandler(handler); }
    public RequestOperation request() {
        return SocketOperations.request(this::requestAsync, this::requestCallback);
    }

    private CompletableFuture<List<Message>> requestAsync(List<Message> parts,
                                                          SendFlags flags,
                                                          Duration timeout) {
        return InternalAccess.dealerRequestAsync(this, parts, flags, timeout);
    }

    private boolean requestCallback(List<Message> parts,
                                    RequestCallback callback,
                                    SendFlags flags,
                                    Duration timeout) {
        return InternalAccess.dealerRequestCallback(this, parts, callback, flags,
            timeout);
    }
    @Override
    public void close() {
        debug("dealer close begin");
        try {
            super.close();
        } finally {
            debug("dealer close end");
        }
    }
    @Override public DealerSocketOptions options() { return options; }

    private static void debug(String message) {
        if (DEBUG_REQREP) {
            try {
                Files.writeString(Path.of("/tmp/zlink-reqrep.log"),
                    "[dealer-socket] " + message + System.lineSeparator(),
                    StandardOpenOption.CREATE, StandardOpenOption.APPEND);
            } catch (Exception ignored) {
            }
        }
    }

    private final class DealerSendBuilder implements SendOperation, SendSubmitOperation {
        private Message singlePart;
        private MessageParts parts;
        private int partCount;
        private SendFlags flags = SendFlags.NONE;
        private boolean submitted;

        @Override
        public SendSubmitOperation message(Message part) {
            ensureNotSubmitted();
            Objects.requireNonNull(part, "part");
            if (partCount == 0) {
                singlePart = part;
            } else {
                if (parts == null) {
                    parts = new MessageParts();
                    parts.add(singlePart);
                    singlePart = null;
                }
                parts.add(part);
            }
            partCount++;
            return this;
        }

        @Override
        public SendSubmitOperation flags(SendFlags value) {
            ensureNotSubmitted();
            flags = Objects.requireNonNull(value, "flags");
            return this;
        }

        @Override
        public boolean submit() {
            markSubmitted();
            SendFlag nativeFlags = SendFlag.fromValue(flags.value());
            if (partCount == 1)
                return NativeDealerSocket.super.send(singlePart, nativeFlags);
            return NativeDealerSocket.super.send(parts.asList(), nativeFlags);
        }

        private void markSubmitted() {
            ensureNotSubmitted();
            if (partCount == 0)
                throw new IllegalArgumentException("at least one message required");
            submitted = true;
        }

        private void ensureNotSubmitted() {
            if (submitted)
                throw new IllegalStateException("operation already submitted");
        }
    }

}
