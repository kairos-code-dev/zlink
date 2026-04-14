package dev.kairoscode.zlink.samples;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.DealerSocket;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.RouterSocket;
import dev.kairoscode.zlink.RoutingId;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

public final class RequestReplyAsyncSample {
    public static void main(String[] args) throws Exception {
        SampleSupport.ensureNative();
        String endpoint = SampleSupport.tcpEndpoint();
        CountDownLatch requestHandled = new CountDownLatch(1);

        try (Context ctx = new Context();
             RouterSocket routerSocket = new RouterSocket(ctx);
             DealerSocket dealerSocket = new DealerSocket(ctx);
             var routerMonitor = routerSocket.monitorOpen(
                 dev.kairoscode.zlink.MonitorEventType.CONNECTION_READY);
             var dealerMonitor = dealerSocket.monitorOpen(
                 dev.kairoscode.zlink.MonitorEventType.CONNECTION_READY)) {
            dealerSocket.setRoutingId(RoutingId.fromBytes("request-reply-client".getBytes()));
            routerSocket.bind(endpoint);
            dealerSocket.connect(endpoint);
            SampleSupport.waitConnected(routerMonitor, dealerMonitor);

            CompletableFuture<Void> replyHandled = new CompletableFuture<>();

            routerSocket.onReceive(received -> {
                try (received) {
                    String request = SampleSupport.singleUtf8(received);
                    if (!SampleSupport.DEALER_REQUEST.equals(request)) {
                        throw new IllegalStateException("unexpected request: " + request);
                    }
                    if (received.requestSeq().isEmpty()) {
                        throw new IllegalStateException("missing request sequence");
                    }
                    try (Message reply = Message.copyOfUtf8(SampleSupport.DEALER_REPLY)) {
                        received.reply(reply);
                    }
                    requestHandled.countDown();
                    replyHandled.complete(null);
                } catch (Throwable error) {
                    replyHandled.completeExceptionally(error);
                    throw error instanceof RuntimeException runtimeException
                        ? runtimeException
                        : new IllegalStateException(
                            "request reply async sample failed", error);
                }
            });

            CompletableFuture<Void> roundTrip;
            try (Message request = Message.copyOfUtf8(SampleSupport.DEALER_REQUEST)) {
                roundTrip = dealerSocket.request(request, Duration.ofSeconds(2))
                    .thenAccept(reply -> {
                        try {
                            String value = reply.get(0).toUtf8String();
                            if (!SampleSupport.DEALER_REPLY.equals(value)) {
                                throw new IllegalStateException("unexpected reply: " + value);
                            }
                        } finally {
                            Message.closeAll(reply);
                        }
                    });
            }

            roundTrip.get(2, TimeUnit.SECONDS);
            replyHandled.get(2, TimeUnit.SECONDS);
            SampleSupport.await(requestHandled, "request reply async");
            System.out.println("[dealer-router/request-reply/async] send: \""
                + SampleSupport.DEALER_REQUEST + "\" -> recv: \""
                + SampleSupport.DEALER_REPLY + "\"");
        }
    }
}
