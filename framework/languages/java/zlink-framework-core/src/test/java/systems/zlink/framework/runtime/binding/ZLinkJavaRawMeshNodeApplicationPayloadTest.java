package systems.zlink.framework.runtime.binding;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;

import java.util.EnumSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeader;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeaderCodec;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeaderFlag;
import systems.zlink.framework.streams.ZLinkStreamCodec;
import systems.zlink.framework.streams.ZLinkStreamMessageKind;

final class ZLinkJavaRawMeshNodeApplicationPayloadTest {
    @Test
    void typedActorRequestUsesStreamPacketNameAndRestoresRequestHeader() {
        byte[] body = "{\"actorId\":\"actor-1\"}".getBytes(
            java.nio.charset.StandardCharsets.UTF_8);
        ZLinkStreamHeader request = new ZLinkStreamHeader(
            ZLinkStreamMessageKind.REQUEST,
            ZLinkStreamCodec.JSON,
            EnumSet.noneOf(ZLinkStreamHeaderFlag.class),
            Optional.of(7L),
            "JoinTargetReq",
            Map.of());

        try (Message header = Message.from(ZLinkStreamHeaderCodec.encode(request));
             Message payload = Message.from(body)) {
            var wirePayload = ZLinkJavaRawMeshNode.applicationPayload(
                List.of(header, payload));

            assertEquals("JoinTargetReq", wirePayload.packetName());
            assertArrayEquals(body, wirePayload.payload());

            List<Message> restored =
                ZLinkJavaRawMeshNode.applicationMessages(
                    wirePayload,
                    true,
                    7L);
            try {
                ZLinkStreamHeader restoredHeader =
                    ZLinkStreamHeaderCodec.decodeOrPlain(
                        restored.getFirst().toByteArray());
                assertEquals(
                    ZLinkStreamMessageKind.REQUEST,
                    restoredHeader.kind());
                assertEquals(Optional.of(7L), restoredHeader.requestSequence());
                assertEquals("JoinTargetReq", restoredHeader.packetName());
                assertEquals(ZLinkStreamCodec.JSON, restoredHeader.codec());
                assertArrayEquals(body, restored.get(1).toByteArray());
            } finally {
                restored.forEach(Message::close);
            }
        }
    }

    @Test
    void plainTwoPartMessageKeepsItsPacketName() {
        try (Message packetName = Message.from("plain.packet");
             Message payload = Message.from(new byte[] {1, 2, 3})) {
            var wirePayload = ZLinkJavaRawMeshNode.applicationPayload(
                List.of(packetName, payload));

            assertEquals("plain.packet", wirePayload.packetName());
            assertArrayEquals(new byte[] {1, 2, 3}, wirePayload.payload());
        }
    }

    @Test
    void applicationMessagesPreserveTheSelectedStreamCodec() {
        var payload = new systems.zlink.framework.runtime.internal.service
            .ZLinkServiceM6AWireCodec.ApplicationPayload(
                "CustomReq",
                "application/x-protobuf",
                new byte[] {4, 5, 6});

        List<Message> restored = ZLinkJavaRawMeshNode.applicationMessages(
            payload,
            true,
            11L,
            ZLinkStreamCodec.PROTOBUF);
        try {
            ZLinkStreamHeader header = ZLinkStreamHeaderCodec.decodeOrPlain(
                restored.getFirst().toByteArray());
            assertEquals(ZLinkStreamCodec.PROTOBUF, header.codec());
            assertEquals(Optional.of(11L), header.requestSequence());
        } finally {
            restored.forEach(Message::close);
        }
    }
}
