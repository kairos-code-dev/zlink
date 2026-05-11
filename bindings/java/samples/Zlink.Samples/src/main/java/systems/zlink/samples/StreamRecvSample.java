package systems.zlink.samples;

import systems.zlink.Context;
import systems.zlink.Message;
import systems.zlink.RoutingId;
import systems.zlink.StreamSocket;
import java.nio.charset.StandardCharsets;

public final class StreamRecvSample {
    public static void main(String[] args) throws Exception {
        SampleSupport.ensureNative();
        String endpoint = SampleSupport.tcpEndpoint();

        try (Context ctx = new Context();
             StreamSocket server = new StreamSocket(ctx);
             var monitor = server.monitorOpen(
                 systems.zlink.MonitorEventType.ACCEPTED,
                 systems.zlink.MonitorEventType.CONNECTION_READY)) {
            server.bind(endpoint);
            try (var rawClient = SampleSupport.connectRawTcp(endpoint)) {
                SampleSupport.waitStreamConnected(monitor);

                byte[] payload =
                    SampleSupport.STREAM_PAYLOAD.getBytes(StandardCharsets.UTF_8);
                SampleSupport.sendRawTcp(rawClient, payload);

                RoutingId rid;
                try (systems.zlink.Received received = new systems.zlink.Received()) {
                    server.recv(received, systems.zlink.RecvFlags.NONE);
                    String value = SampleSupport.singleUtf8(received);
                    rid = received.routingId().orElse(null);
                    if (!SampleSupport.STREAM_PAYLOAD.equals(value) || rid == null) {
                        throw new IllegalStateException("unexpected stream delivery");
                    }
                }

                try (Message reply = Message.copyOfUtf8(
                         SampleSupport.STREAM_PAYLOAD)) {
                    server.send(rid, reply);
                }

                String echoed = new String(
                    SampleSupport.recvExactRawTcp(rawClient, payload.length),
                    StandardCharsets.UTF_8);
                if (!SampleSupport.STREAM_PAYLOAD.equals(echoed)) {
                    throw new IllegalStateException("unexpected stream echo: " + echoed);
                }
                System.out.println("[stream/recv] send: \"" +
                    SampleSupport.STREAM_PAYLOAD + "\" \u2192 recv: \"" +
                    echoed + "\"");
            }
        }
    }
}
