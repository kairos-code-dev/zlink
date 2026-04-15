package dev.kairoscode.zlink.samples;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.PairSocket;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicReference;

public final class PairCallbackSample {
    public static void main(String[] args) {
        SampleSupport.ensureNative();
        String endpoint = SampleSupport.tcpEndpoint();
        CountDownLatch delivered = new CountDownLatch(1);
        AtomicReference<String> receivedValue = new AtomicReference<>();

        try (Context ctx = new Context();
             PairSocket server = new PairSocket(ctx);
             PairSocket client = new PairSocket(ctx);
             var serverMonitor = server.monitorOpen(
                 dev.kairoscode.zlink.MonitorEventType.CONNECTION_READY);
             var clientMonitor = client.monitorOpen(
                 dev.kairoscode.zlink.MonitorEventType.CONNECTION_READY)) {
            server.bind(endpoint);
            client.connect(endpoint);
            SampleSupport.waitConnected(serverMonitor, clientMonitor);

            Thread receiver = new Thread(() -> {
                try (var received = server.recv()) {
                    receivedValue.set(SampleSupport.singleUtf8(received));
                } finally {
                    delivered.countDown();
                }
            }, "pair-recv-sample");
            receiver.start();

            try (Message outbound = Message.copyOfUtf8(SampleSupport.PAIR_PAYLOAD)) {
                client.send(outbound);
            }

            SampleSupport.await(delivered, "pair callback");
            String value = receivedValue.get();
            if (!SampleSupport.PAIR_PAYLOAD.equals(value)) {
                throw new IllegalStateException("unexpected payload: " + value);
            }
            System.out.println("[pair/recv] send: \"" + SampleSupport.PAIR_PAYLOAD
                + "\" \u2192 recv: \"" + value + "\"");
        }
    }
}
