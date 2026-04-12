package dev.kairoscode.zlink;

import dev.kairoscode.zlink.options.SocketOptions;
import java.nio.charset.StandardCharsets;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertThrows;

public class SendResultContractTest {
    @Test
    public void trySubscribeReturnsEmptyWhenNoTopicDeliveryExists() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             SubSocket sub = new SubSocket(ctx)) {
            RecvException ex = assertThrows(RecvException.class,
                () -> sub.subscribe(RecvFlags.DONT_WAIT));
            assertEquals(RecvResult.NO_DATA, ex.getResult());
        }
    }

    @Test
    public void tryReceiveSubscriptionEventReturnsEmptyWhenNoEventExists() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             XPubSocket pub = new XPubSocket(ctx)) {
            RecvException ex = assertThrows(RecvException.class,
                () -> pub.receiveSubscriptionEvent(RecvFlags.DONT_WAIT));
            assertEquals(RecvResult.NO_DATA, ex.getResult());
        }
    }

    @Test
    public void trySendReturnsNotReadyWhenRouterHasNoMatchingPeer() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             RouterSocket router = new RouterSocket(ctx);
             DealerSocket dealer = new DealerSocket(ctx);
             Message payload = Message.copyOfUtf8("router-payload")) {
            router.options().mandatory(true);
            String endpoint = TestSupport.tcpEndpoint();
            try (var routerMon = router.monitorOpen(
                     MonitorEventType.CONNECTION_READY);
                 var dealerMon = dealer.monitorOpen(
                     MonitorEventType.CONNECTION_READY)) {
                dealer.setRoutingId(RoutingId.copyOf(
                    "router-send-target".getBytes(StandardCharsets.UTF_8)));
                router.bind(endpoint);
                dealer.connect(endpoint);
                routerMon.recv();
                dealerMon.recv();
            }
            RoutingId missingRid = dealer.routingId();
            try (var routerMon = router.monitorOpen(
                     MonitorEventType.DISCONNECTED);
                 var dealerMon = dealer.monitorOpen(
                     MonitorEventType.DISCONNECTED)) {
                dealer.disconnect(endpoint);
                routerMon.recv();
                dealerMon.recv();
            }
            SubmitException ex = assertThrows(SubmitException.class,
                () -> router.send(missingRid, payload, SendFlags.DONT_WAIT));
            assertEquals(SubmitResult.NOT_CONNECTED, ex.getResult());
        }
    }

    @Test
    public void trySendReturnsBackpressuredWhenPairQueueFills() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             PairSocket server = new PairSocket(ctx);
             PairSocket client = new PairSocket(ctx)) {
            server.setOption(SocketOptions.RCVHWM, 1);
            client.setOption(SocketOptions.SNDHWM, 1);
            server.setOption(SocketOptions.RCVBUF, 1);
            client.setOption(SocketOptions.SNDBUF, 1);
            String endpoint = TestSupport.inprocEndpoint("pair-backpressure");
            try (var serverMon = server.monitorOpen(
                     MonitorEventType.CONNECTION_READY);
                 var clientMon = client.monitorOpen(
                     MonitorEventType.CONNECTION_READY)) {
                server.bind(endpoint);
                client.connect(endpoint);
                serverMon.recv();
                clientMon.recv();
            }

            SubmitException failure = null;
            byte[] bytes = new byte[256 * 1024];
            java.util.Arrays.fill(bytes, (byte) 'b');
            for (int i = 0; i < 1_000; i++) {
                try (Message payload = Message.copyOf(bytes)) {
                    client.send(payload, SendFlags.DONT_WAIT);
                }
                catch (SubmitException ex) {
                    failure = ex;
                    break;
                }
            }
            assertNotNull(failure, "pair send never backpressured");
            assertEquals(SubmitResult.BACKPRESSURED, failure.getResult());
        }
    }
}
