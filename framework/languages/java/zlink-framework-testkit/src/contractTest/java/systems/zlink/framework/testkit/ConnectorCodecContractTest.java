package systems.zlink.framework.testkit;

import static org.junit.jupiter.api.Assertions.assertEquals;

import java.net.URI;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import org.junit.jupiter.api.Test;
import systems.zlink.stream.connector.ZLinkStreamConnector;
import systems.zlink.stream.connector.ZLinkStreamConnectorFactory;
import systems.zlink.stream.connector.ZLinkStreamConnectorOptions;
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
            payload -> ZLinkStreamJson.decode(payload, String.class));
        assertCodecRoundtrip(
            "MsgpackPacket",
            ZLinkStreamMessagePack.CONTENT_TYPE,
            ZLinkStreamMessagePack.encode("MsgpackPacket", "msgpack-body"),
            payload -> ZLinkStreamMessagePack.decode(payload, String.class));
        assertCodecRoundtrip(
            "ProtobufPacket",
            ZLinkStreamProtobuf.CONTENT_TYPE,
            ZLinkStreamProtobuf.encode("ProtobufPacket", "protobuf-body"),
            payload -> ZLinkStreamProtobuf.decode(payload, String.class));
    }

    @Test
    void jsonTypedHelperUsesConnectorSendRequestAndOnSurface() throws Exception {
        try (ZLinkStreamConnector connector =
                 ZLinkStreamConnectorFactory.create(options())) {
            List<String> handled = new ArrayList<>();
            ZLinkStreamJson.on(connector, "String", String.class, message -> {
                handled.add(message.payload() + ":" + message.metadata().get("content-type"));
                return java.util.concurrent.CompletableFuture.completedFuture(null);
            });

            connector.connectAsync().toCompletableFuture().join();
            ZLinkStreamJson.send(connector, "hello")
                .submitAsync()
                .toCompletableFuture()
                .join();
            connector.dispatchAsync().toCompletableFuture().join();

            ZLinkStreamEncodedPayload reply = ZLinkStreamJson.request(connector, "reply")
                .submitAsync()
                .toCompletableFuture()
                .join();
            try {
                assertEquals("reply", ZLinkStreamJson.decode(reply, String.class));
            } finally {
                reply.payload().close();
            }

            assertEquals(List.of(
                "hello:" + ZLinkStreamJson.CONTENT_TYPE), handled);
        }
    }

    private static void assertCodecRoundtrip(
        String packetName,
        String contentType,
        ZLinkStreamEncodedPayload encoded,
        java.util.function.Function<ZLinkStreamEncodedPayload, String> decode) {
        try {
            assertEquals(packetName, encoded.packetName());
            assertEquals(contentType, encoded.metadata().get("content-type"));
            assertEquals(encoded.payload().toUtf8String(), decode.apply(encoded));
        } finally {
            encoded.payload().close();
        }
    }

    private static ZLinkStreamConnectorOptions options() {
        return new ZLinkStreamConnectorOptions(
            URI.create("tcp://127.0.0.1:7100"),
            ZLinkStreamDispatchMode.MANUAL,
            Duration.ofSeconds(1),
            1);
    }
}
