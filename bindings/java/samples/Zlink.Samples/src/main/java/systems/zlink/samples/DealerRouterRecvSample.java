package systems.zlink.samples;

import systems.zlink.Context;
import systems.zlink.DealerSocket;
import systems.zlink.Message;
import systems.zlink.RouterSocket;
import systems.zlink.RoutingId;

public final class DealerRouterRecvSample {
    public static void main(String[] args) {
        SampleSupport.ensureNative();
        String endpoint = SampleSupport.tcpEndpoint();

        try (Context ctx = new Context();
             RouterSocket router = new RouterSocket(ctx);
             DealerSocket dealer = new DealerSocket(ctx);
             var routerMonitor = router.monitorOpen(
                 systems.zlink.MonitorEventType.CONNECTION_READY);
             var dealerMonitor = dealer.monitorOpen(
                 systems.zlink.MonitorEventType.CONNECTION_READY)) {
            router.bind(endpoint);
            dealer.connect(endpoint);
            SampleSupport.waitConnected(routerMonitor, dealerMonitor);

            try (Message request = Message.copyOfUtf8(SampleSupport.DEALER_REQUEST)) {
                dealer.send(request);
            }

            RoutingId rid;
            try (systems.zlink.Received received = new systems.zlink.Received()) {
                router.recv(received, systems.zlink.RecvFlags.NONE);
                String value = SampleSupport.singleUtf8(received);
                if (!SampleSupport.DEALER_REQUEST.equals(value)) {
                    throw new IllegalStateException("unexpected request: " + value);
                }
                rid = received.routingId().orElse(null);
                if (rid == null) {
                    throw new IllegalStateException("router delivery missing routing id");
                }
            }

            try (Message reply = Message.copyOfUtf8(SampleSupport.DEALER_REPLY)) {
                router.send(rid, reply);
            }

            try (systems.zlink.Received received = new systems.zlink.Received()) {
                dealer.recv(received, systems.zlink.RecvFlags.NONE);
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
