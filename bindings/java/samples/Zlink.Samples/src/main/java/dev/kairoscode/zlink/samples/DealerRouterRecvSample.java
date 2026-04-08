package dev.kairoscode.zlink.samples;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.DealerSocket;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.RouterSocket;
import dev.kairoscode.zlink.RoutingId;

public final class DealerRouterRecvSample {
    public static void main(String[] args) {
        SampleSupport.ensureNative();
        String endpoint = SampleSupport.tcpEndpoint();

        try (Context ctx = new Context();
             RouterSocket router = new RouterSocket(ctx);
             DealerSocket dealer = new DealerSocket(ctx);
             var routerMonitor = router.monitorOpen(
                 dev.kairoscode.zlink.MonitorEventType.CONNECTION_READY);
             var dealerMonitor = dealer.monitorOpen(
                 dev.kairoscode.zlink.MonitorEventType.CONNECTION_READY)) {
            router.bind(endpoint);
            dealer.connect(endpoint);
            SampleSupport.waitConnected(routerMonitor, dealerMonitor);

            try (Message request = Message.copyOfUtf8(SampleSupport.DEALER_REQUEST)) {
                dealer.send(request);
            }

            RoutingId rid;
            try (var received = router.recv()) {
                String value = SampleSupport.singleUtf8(received);
                if (!SampleSupport.DEALER_REQUEST.equals(value)) {
                    throw new IllegalStateException("unexpected request: " + value);
                }
                rid = received.routingId();
                if (rid == null) {
                    throw new IllegalStateException("router delivery missing routing id");
                }
            }

            try (Message reply = Message.copyOfUtf8(SampleSupport.DEALER_REPLY)) {
                router.send(rid, reply);
            }

            try (var received = dealer.recv()) {
                String value = SampleSupport.singleUtf8(received);
                if (!SampleSupport.DEALER_REPLY.equals(value)) {
                    throw new IllegalStateException("unexpected reply: " + value);
                }
                System.out.println("[dealer-router/recv] send: \""
                    + SampleSupport.DEALER_REQUEST + "\" \u2192 recv: \"" + value + "\"");
            }
        }
    }
}
