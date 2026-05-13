/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink;

import systems.zlink.service.discovery.Discovery;
import systems.zlink.service.spot.RequestOp;
import systems.zlink.service.spot.SendOp;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CompletableFuture;

public final class DealerSocket extends Socket {
    private static final boolean DEBUG_REQREP =
      Boolean.getBoolean("zlink.reqrep.debug");
    private final DealerSocketOptions options = new DealerSocketOptions(this);
    private final DealerRequestSupport dealerRequests =
      new DealerRequestSupport(this, false);

    public DealerSocket(Context ctx) {
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
    public RoutingId routingId() { return super.routingId(); }
    public SendOp send() {
        return SocketOperations.send((parts, flags) ->
            super.send(parts, SendFlag.fromValue(flags.value())));
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
    public void onSendReady(SendReadyHandler handler) { super.onSendReady(handler); }
    public RequestOp request() {
        return SocketOperations.request(this::requestAsync, this::requestCallback);
    }

    private CompletableFuture<List<Message>> requestAsync(List<Message> parts,
                                                          SendFlags flags,
                                                          Duration timeout) {
        return dealerRequests.request(parts, timeout, flags).thenApply(reply ->
            RequestReplySupport.takeReceivedParts(reply));
    }

    private boolean requestCallback(List<Message> parts,
                                    RequestCallback callback,
                                    SendFlags flags,
                                    Duration timeout) {
        try {
            dealerRequests.request(parts, (result, reply) -> {
                List<Message> payload = List.of();
                if (reply != null) {
                    payload = RequestReplySupport.takeReceivedParts(reply);
                }
                callback.onComplete(result, payload);
            }, flags, timeout);
            return true;
        } catch (SubmitException ex) {
            if (flags == SendFlags.DONT_WAIT
                && ex.getResult() == SubmitResult.BACKPRESSURED) {
                return false;
            }
            throw ex;
        }
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
}
