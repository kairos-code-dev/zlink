package dev.kairoscode.zlink.samples;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.DealerSocket;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.RequestDealer;
import dev.kairoscode.zlink.RequestRouter;
import dev.kairoscode.zlink.RouterSocket;
import dev.kairoscode.zlink.RoutingId;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicReference;

public final class RequestReplyCallbackSample {
    public static void main(String[] args) {
        SampleSupport.ensureNative();
        String endpoint = SampleSupport.tcpEndpoint();
        CountDownLatch requestHandled = new CountDownLatch(1);
        CountDownLatch replyHandled = new CountDownLatch(1);
        AtomicReference<Throwable> failure = new AtomicReference<>();

        try (Context ctx = new Context();
             RouterSocket routerSocket = new RouterSocket(ctx);
             DealerSocket dealerSocket = new DealerSocket(ctx);
             RequestRouter router = new RequestRouter(routerSocket);
             RequestDealer dealer = new RequestDealer(dealerSocket);
             var routerMonitor = routerSocket.monitorOpen(
                 dev.kairoscode.zlink.MonitorEventType.CONNECTION_READY);
             var dealerMonitor = dealerSocket.monitorOpen(
                 dev.kairoscode.zlink.MonitorEventType.CONNECTION_READY)) {
            dealerSocket.setRoutingId(RoutingId.copyOf("request-reply-client".getBytes()));
            routerSocket.bind(endpoint);
            dealerSocket.connect(endpoint);
            SampleSupport.waitConnected(routerMonitor, dealerMonitor);

            router.onReceive(received -> {
                try (received) {
                    String request = SampleSupport.singleUtf8(received);
                    if (!SampleSupport.DEALER_REQUEST.equals(request)) {
                        failure.compareAndSet(null,
                            new IllegalStateException("unexpected request: " + request));
                    } else {
                        if (!received.hasRequestSequence()) {
                            failure.compareAndSet(null,
                                new IllegalStateException("missing request sequence"));
                            return;
                        }
                        try (Message reply = Message.copyOfUtf8(SampleSupport.DEALER_REPLY)) {
                            router.reply(received.routingId(),
                                received.requestSequence(), reply).join();
                        }
                    }
                } finally {
                    requestHandled.countDown();
                }
            });

            try (Message request = Message.copyOfUtf8(SampleSupport.DEALER_REQUEST)) {
                dealer.request(
                    List.of(request),
                    Duration.ofSeconds(2),
                    (error, reply) -> {
                        try {
                            if (error != null) {
                                failure.compareAndSet(null, error);
                                return;
                            }
                            try (reply) {
                                String value = SampleSupport.singleUtf8(reply);
                                if (!SampleSupport.DEALER_REPLY.equals(value)) {
                                    failure.compareAndSet(null,
                                        new IllegalStateException("unexpected reply: " + value));
                                }
                            }
                        } finally {
                            replyHandled.countDown();
                        }
                    });
            }

            SampleSupport.await(requestHandled, "request reply callback request");
            SampleSupport.await(replyHandled, "request reply callback reply");
            if (failure.get() != null) {
                throw new IllegalStateException("request reply callback sample failed",
                    failure.get());
            }
            System.out.println("[dealer-router/request-reply/callback] send: \""
                + SampleSupport.DEALER_REQUEST + "\" -> recv: \""
                + SampleSupport.DEALER_REPLY + "\"");
        }
    }
}
