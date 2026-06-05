package systems.zlink.framework.testkit;

import static org.junit.jupiter.api.Assertions.assertEquals;

import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.net.ServerSocket;
import java.net.Socket;
import java.net.URI;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import org.junit.jupiter.api.Test;
import systems.zlink.stream.connector.ZLinkStreamConnector;
import systems.zlink.stream.connector.ZLinkStreamConnectorFactory;
import systems.zlink.stream.connector.ZLinkStreamConnectorOptions;
import systems.zlink.stream.connector.ZLinkStreamCompression;
import systems.zlink.stream.connector.ZLinkStreamDispatchMode;
import systems.zlink.stream.connector.ZLinkStreamEncodedPayload;
import systems.zlink.stream.connector.json.ZLinkStreamJson;
import systems.zlink.stream.connector.msgpack.ZLinkStreamMessagePack;
import systems.zlink.stream.connector.protobuf.ZLinkStreamProtobuf;

final class ConnectorCodecContractTest {
    @Test
    void jsonMsgpackProtobufTypedHelperRoundtrip() {
        assertCodecRoundtrip(
            "JsonPacket",
            ZLinkStreamJson.CONTENT_TYPE,
            ZLinkStreamJson.encode("JsonPacket", "json-body"),
            "json-body",
            payload -> ZLinkStreamJson.decode(payload, String.class));
        assertCodecRoundtrip(
            "MsgpackPacket",
            ZLinkStreamMessagePack.CONTENT_TYPE,
            ZLinkStreamMessagePack.encode("MsgpackPacket", "msgpack-body"),
            "msgpack-body",
            payload -> ZLinkStreamMessagePack.decode(payload, String.class));
        assertCodecRoundtrip(
            "ProtobufPacket",
            ZLinkStreamProtobuf.CONTENT_TYPE,
            ZLinkStreamProtobuf.encode("ProtobufPacket", "protobuf-body"),
            "protobuf-body",
            payload -> ZLinkStreamProtobuf.decode(payload, String.class));
    }

    @Test
    void jsonTypedHelperUsesConnectorSendRequestAndOnSurface() throws Exception {
        try (TcpServer server = new TcpServer();
             ZLinkStreamConnector connector =
                 ZLinkStreamConnectorFactory.create(options(server.endpoint()))) {
            List<String> handled = new ArrayList<>();
            ZLinkStreamJson.on(connector, "String", String.class, message -> {
                handled.add(message.payload() + ":" + message.metadata().get("content-type"));
                return java.util.concurrent.CompletableFuture.completedFuture(null);
            });

            connector.connectAsync().toCompletableFuture().join();
            ZLinkStreamJson.send(connector, "hello")
                .compress()
                .submitAsync()
                .toCompletableFuture()
                .join();
            Frame sent = server.readFrame();
            assertEquals(1, sent.kind());
            assertEquals("String", sent.name());

            server.sendFrame(new Frame(
                1,
                null,
                "String",
                "\"server\"".getBytes(StandardCharsets.UTF_8),
                true));
            awaitPendingDispatch(connector);
            connector.dispatchAsync().toCompletableFuture().join();

            var replyFuture = ZLinkStreamJson.request(connector, "reply")
                .compress()
                .submitAsync()
                .toCompletableFuture();
            Frame request = server.readFrame();
            assertEquals(2, request.kind());
            assertEquals("String", request.name());
            server.sendFrame(new Frame(
                3,
                request.requestSeq(),
                "String",
                "\"reply\"".getBytes(StandardCharsets.UTF_8),
                true));

            ZLinkStreamEncodedPayload reply = replyFuture.join();
            try {
                assertEquals("reply", ZLinkStreamJson.decode(reply, String.class));
            } finally {
                reply.payload().close();
            }

            assertEquals(List.of(
                "server:" + ZLinkStreamJson.CONTENT_TYPE), handled);
        }
    }

    private static void assertCodecRoundtrip(
        String packetName,
        String contentType,
        ZLinkStreamEncodedPayload encoded,
        String expected,
        java.util.function.Function<ZLinkStreamEncodedPayload, String> decode) {
        try {
            assertEquals(packetName, encoded.packetName());
            assertEquals(contentType, encoded.metadata().get("content-type"));
            assertEquals(expected, decode.apply(encoded));
        } finally {
            encoded.payload().close();
        }
    }

    private static ZLinkStreamConnectorOptions options(URI endpoint) {
        return new ZLinkStreamConnectorOptions(
            endpoint,
            ZLinkStreamDispatchMode.MANUAL,
            Duration.ofSeconds(1),
            1,
            Duration.ofSeconds(1),
            64 * 1024,
            true,
            Duration.ofSeconds(1),
            Duration.ofSeconds(5),
            true,
            Duration.ofMillis(250),
            Duration.ofSeconds(5),
            2.0,
            false,
            ZLinkStreamCompression.LZ4);
    }

    private static void awaitPendingDispatch(ZLinkStreamConnector connector) {
        long deadline = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(5);
        while (System.nanoTime() < deadline) {
            if (connector.pendingDispatchCount() > 0) {
                return;
            }
            Thread.onSpinWait();
        }
        throw new AssertionError("connector did not queue inbound dispatch within 5s");
    }

    private record Frame(
        int kind,
        Long requestSeq,
        String name,
        byte[] payload,
        boolean jsonMetadata) {
    }

    private static final class TcpServer implements AutoCloseable {
        private final ServerSocket server;
        private final java.util.concurrent.BlockingQueue<Socket> sockets =
            new java.util.concurrent.LinkedBlockingQueue<>();
        private final Thread acceptThread;
        private Socket current;

        TcpServer() throws IOException {
            server = new ServerSocket(0);
            acceptThread = new Thread(() -> {
                while (!server.isClosed()) {
                    try {
                        sockets.add(server.accept());
                    } catch (IOException ex) {
                        if (!server.isClosed()) {
                            throw new RuntimeException(ex);
                        }
                    }
                }
            }, "zlink-connector-codec-contract-server");
            acceptThread.setDaemon(true);
            acceptThread.start();
        }

        URI endpoint() {
            return URI.create("tcp://127.0.0.1:" + server.getLocalPort());
        }

        Frame readFrame() throws Exception {
            DataInputStream input = new DataInputStream(socket().getInputStream());
            int headerLength = input.readUnsignedShort();
            int payloadLength = input.readInt();
            byte[] header = input.readNBytes(headerLength);
            byte[] payload = input.readNBytes(payloadLength);
            Header decoded = decodeHeader(header);
            return new Frame(
                decoded.kind(),
                decoded.requestSeq(),
                decoded.name(),
                payload,
                decoded.jsonMetadata());
        }

        void sendFrame(Frame frame) throws Exception {
            byte[] header = encodeHeader(frame);
            byte[] encoded = ByteBuffer.allocate(6 + header.length + frame.payload().length)
                .putShort((short) header.length)
                .putInt(frame.payload().length)
                .put(header)
                .put(frame.payload())
                .array();
            DataOutputStream output = new DataOutputStream(socket().getOutputStream());
            output.write(encoded);
            output.flush();
        }

        @Override
        public void close() throws Exception {
            if (current != null) {
                current.close();
            }
            server.close();
        }

        private Socket socket() throws InterruptedException {
            if (current != null && !current.isClosed()) {
                return current;
            }
            current = sockets.poll(5, java.util.concurrent.TimeUnit.SECONDS);
            if (current == null) {
                throw new AssertionError("client did not connect within 5s");
            }
            return current;
        }

        private static byte[] encodeHeader(Frame frame) {
            byte[] name = frame.name().getBytes(StandardCharsets.UTF_8);
            byte[] metadata = frame.jsonMetadata()
                ? new byte[] {
                    1,
                    12,
                    'c', 'o', 'n', 't', 'e', 'n', 't', '-', 't', 'y', 'p', 'e',
                    0, 16,
                    'a', 'p', 'p', 'l', 'i', 'c', 'a', 't', 'i', 'o', 'n', '/',
                    'j', 's', 'o', 'n'
                }
                : new byte[0];
            int flags = 0;
            if (frame.requestSeq() != null) {
                flags |= 0x01;
            }
            if (frame.jsonMetadata()) {
                flags |= 0x02;
            }
            ByteBuffer buffer = ByteBuffer.allocate(
                3
                    + (frame.requestSeq() == null ? 0 : 8)
                    + 1
                    + name.length
                    + (metadata.length == 0 ? 0 : 2 + metadata.length));
            buffer.put((byte) frame.kind());
            buffer.put((byte) 0);
            buffer.put((byte) flags);
            if (frame.requestSeq() != null) {
                buffer.putLong(frame.requestSeq());
            }
            buffer.put((byte) name.length);
            buffer.put(name);
            if (metadata.length > 0) {
                buffer.putShort((short) metadata.length);
                buffer.put(metadata);
            }
            return buffer.array();
        }

        private static Header decodeHeader(byte[] header) {
            ByteBuffer buffer = ByteBuffer.wrap(header);
            int kind = Byte.toUnsignedInt(buffer.get());
            buffer.get();
            int flags = Byte.toUnsignedInt(buffer.get());
            Long requestSeq = (flags & 0x01) == 0 ? null : buffer.getLong();
            int nameLength = Byte.toUnsignedInt(buffer.get());
            byte[] name = new byte[nameLength];
            buffer.get(name);
            return new Header(
                kind,
                requestSeq,
                new String(name, StandardCharsets.UTF_8),
                (flags & 0x02) != 0);
        }

        private record Header(
            int kind,
            Long requestSeq,
            String name,
            boolean jsonMetadata) {
        }
    }
}
