package systems.zlink.samples;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;

import systems.zlink.contracts.Context;
import systems.zlink.contracts.DealerSocket;
import systems.zlink.contracts.Message;
import systems.zlink.contracts.RouterSocket;

public final class DealerRouterRecvSample {
    public static void main(String[] args) {
        SampleSupport.ensureNative();
        String endpoint = SampleSupport.tcpEndpoint();

        try (Context ctx = new Context();
             RouterSocket router = new RouterSocket(ctx);
             DealerSocket dealer = new DealerSocket(ctx);
             var routerMonitor = router.monitorOpen(
                 systems.zlink.contracts.MonitorEventType.CONNECTION_READY);
             var dealerMonitor = dealer.monitorOpen(
                 systems.zlink.contracts.MonitorEventType.CONNECTION_READY)) {
            router.bind(endpoint);
            dealer.connect(endpoint);
            SampleSupport.waitConnected(routerMonitor, dealerMonitor);

            try (Message request = Message.copyOfUtf8(SampleSupport.DEALER_REQUEST)) {
                dealer.send().message(request).submit();
            }

            try (systems.zlink.contracts.Received received = new systems.zlink.contracts.Received()) {
                router.recv(received, systems.zlink.contracts.RecvFlags.NONE);
                String value = SampleSupport.singleUtf8(received);
                if (!SampleSupport.DEALER_REQUEST.equals(value)) {
                    throw new IllegalStateException("unexpected request: " + value);
                }
                try (Message reply = Message.copyOfUtf8(SampleSupport.DEALER_REPLY)) {
                    received.send().message(reply).submit();
                }
            }

            try (systems.zlink.contracts.Received received = new systems.zlink.contracts.Received()) {
                dealer.recv(received, systems.zlink.contracts.RecvFlags.NONE);
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
