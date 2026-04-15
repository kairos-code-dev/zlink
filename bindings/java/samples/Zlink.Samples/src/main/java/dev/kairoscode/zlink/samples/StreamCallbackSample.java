package dev.kairoscode.zlink.samples;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.RoutingId;
import dev.kairoscode.zlink.SendFlags;
import dev.kairoscode.zlink.StreamSocket;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.CountDownLatch;

public final class StreamCallbackSample {
    public static void main(String[] args) throws Exception {
        SampleSupport.ensureNative();
        String endpoint = SampleSupport.tcpEndpoint();
        CountDownLatch delivered = new CountDownLatch(1);

        try (Context ctx = new Context();
             StreamSocket server = new StreamSocket(ctx);
             var monitor = server.monitorOpen(
                 dev.kairoscode.zlink.MonitorEventType.ACCEPTED,
                 dev.kairoscode.zlink.MonitorEventType.CONNECTION_READY)) {
            server.bind(endpoint);
            try (var rawClient = SampleSupport.connectRawTcp(endpoint)) {
                SampleSupport.waitStreamConnected(monitor);

                server.onPacket((routingId, payload) -> {
                    String value = payload.toUtf8String();
                    if (!SampleSupport.STREAM_PAYLOAD.equals(value) || routingId == null) {
                        return 0;
                    }
                    try (Message reply = Message.copyOfUtf8(
                             SampleSupport.STREAM_PAYLOAD)) {
                        server.send(routingId, reply, SendFlags.DONT_WAIT);
                    }
                    delivered.countDown();
                    return 0;
                });

                byte[] payload =
                    SampleSupport.STREAM_PAYLOAD.getBytes(StandardCharsets.UTF_8);
                SampleSupport.sendRawTcp(rawClient, payload);
                SampleSupport.await(delivered, "stream callback");

                String echoed = new String(
                    SampleSupport.recvExactRawTcp(rawClient, payload.length),
                    StandardCharsets.UTF_8);
                if (!SampleSupport.STREAM_PAYLOAD.equals(echoed)) {
                    throw new IllegalStateException("unexpected stream echo: " + echoed);
                }
                System.out.println("[stream/callback] send: \"" +
                    SampleSupport.STREAM_PAYLOAD + "\" \u2192 recv: \"" +
                    echoed + "\"");
            }
        }
    }
}
