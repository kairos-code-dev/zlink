package dev.kairoscode.zlink.samples;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.SendFlags;
import dev.kairoscode.zlink.StreamSocket;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.CountDownLatch;

public final class StreamPacketCallbackSample {
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

                server.onPacket((routingId, header, body) -> {
                    String value = body.toUtf8String();
                    if (!SampleSupport.STREAM_PAYLOAD.equals(value) || routingId == null) {
                        return;
                    }
                    try (Message replyHeader = Message.copyOf(new byte[0]);
                         Message replyBody = Message.copyOfUtf8(
                             SampleSupport.STREAM_PAYLOAD);
                         Message reply = frame(replyHeader, replyBody)) {
                        if (!server.send(routingId, reply, SendFlags.DONT_WAIT)) {
                            throw new IllegalStateException(
                                "stream reply backpressured");
                        }
                    }
                    delivered.countDown();
                });

                byte[] payload =
                    SampleSupport.STREAM_PAYLOAD.getBytes(StandardCharsets.UTF_8);
                byte[] frame = frameBytes(payload);
                SampleSupport.sendRawTcp(rawClient, frame);
                SampleSupport.await(delivered, "stream packet callback");

                byte[] echoedFrame =
                    SampleSupport.recvExactRawTcp(rawClient, frame.length);
                String echoed = new String(
                    java.util.Arrays.copyOfRange(echoedFrame, 6,
                        echoedFrame.length),
                    StandardCharsets.UTF_8);
                if (!SampleSupport.STREAM_PAYLOAD.equals(echoed)) {
                    throw new IllegalStateException("unexpected stream echo: " + echoed);
                }
                System.out.println("[stream/packet-callback] send: \"" +
                    SampleSupport.STREAM_PAYLOAD + "\" -> recv: \"" +
                    echoed + "\"");
            }
        }
    }

    private static byte[] frameBytes(byte[] body) {
        byte[] frame = new byte[6 + body.length];
        frame[2] = (byte) (body.length >>> 24);
        frame[3] = (byte) (body.length >>> 16);
        frame[4] = (byte) (body.length >>> 8);
        frame[5] = (byte) body.length;
        System.arraycopy(body, 0, frame, 6, body.length);
        return frame;
    }

    private static Message frame(Message header, Message body) {
        byte[] headerBytes = header.toByteArray();
        byte[] bodyBytes = body.toByteArray();
        byte[] frame = new byte[6 + headerBytes.length + bodyBytes.length];
        frame[0] = (byte) (headerBytes.length >>> 8);
        frame[1] = (byte) headerBytes.length;
        frame[2] = (byte) (bodyBytes.length >>> 24);
        frame[3] = (byte) (bodyBytes.length >>> 16);
        frame[4] = (byte) (bodyBytes.length >>> 8);
        frame[5] = (byte) bodyBytes.length;
        System.arraycopy(headerBytes, 0, frame, 6, headerBytes.length);
        System.arraycopy(bodyBytes, 0, frame, 6 + headerBytes.length,
            bodyBytes.length);
        return Message.copyOf(frame);
    }
}
