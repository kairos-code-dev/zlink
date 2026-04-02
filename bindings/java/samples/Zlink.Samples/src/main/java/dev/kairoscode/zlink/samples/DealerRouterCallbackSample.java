package dev.kairoscode.zlink.samples;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.DealerSocket;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.RouterSocket;
import dev.kairoscode.zlink.RoutingId;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicReference;

public final class DealerRouterCallbackSample {
    public static void main(String[] args) {
        SampleSupport.ensureNative();
        String endpoint = SampleSupport.tcpEndpoint();
        CountDownLatch delivered = new CountDownLatch(1);
        AtomicReference<String> replyValue = new AtomicReference<>();

        try (Context ctx = new Context();
             RouterSocket router = new RouterSocket(ctx);
             DealerSocket dealer = new DealerSocket(ctx);
             var routerMonitor = router.monitorOpen(SampleSupport.CONNECTION_READY_EVENT);
             var dealerMonitor = dealer.monitorOpen(SampleSupport.CONNECTION_READY_EVENT)) {
            router.bind(endpoint);
            dealer.connect(endpoint);
            SampleSupport.waitConnected(routerMonitor, dealerMonitor);

            router.onReceive(received -> {
                String request = SampleSupport.singleUtf8(received);
                RoutingId rid = received.routingId();
                if (!SampleSupport.DEALER_REQUEST.equals(request) || rid == null) {
                    throw new IllegalStateException("unexpected routed request");
                }
                try (Message reply = Message.copyOfUtf8(SampleSupport.DEALER_REPLY)) {
                    router.send(rid, reply);
                }
            });

            dealer.onReceive(received -> {
                replyValue.set(SampleSupport.singleUtf8(received));
                delivered.countDown();
            });

            try (Message request = Message.copyOfUtf8(SampleSupport.DEALER_REQUEST)) {
                dealer.send(request);
            }

            SampleSupport.await(delivered, "dealer-router callback");
            String value = replyValue.get();
            if (!SampleSupport.DEALER_REPLY.equals(value)) {
                throw new IllegalStateException("unexpected reply: " + value);
            }
            System.out.println("[dealer-router/callback] send: \""
                + SampleSupport.DEALER_REQUEST + "\" \u2192 recv: \"" + value + "\"");
        }
    }
}
