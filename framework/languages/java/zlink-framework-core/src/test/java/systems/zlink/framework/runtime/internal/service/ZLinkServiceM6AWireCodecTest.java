package systems.zlink.framework.runtime.internal.service;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

import java.util.List;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.runtime.protocol.ServiceWireConstants;

final class ZLinkServiceM6AWireCodecTest {
    private final ZLinkServiceM6AWireCodec codec =
        new ZLinkServiceM6AWireCodec();

    @Test
    void admissionRoundTripsAllM6ACommandsAndDescriptorFields() {
        var source = RoutingId.from("node-a");
        var descriptor = descriptor(source);

        for (int command : List.of(
            ServiceWireConstants.COMMAND_HELLO,
            ServiceWireConstants.COMMAND_ADMIT,
            ServiceWireConstants.COMMAND_UPDATE)) {
            assertEquals(
                descriptor,
                codec.decodeAdmission(
                    codec.encodeAdmission(command, descriptor),
                    command,
                    source));
        }
        assertEquals(12, codec.decodeReject(codec.encodeReject(12)));
    }

    @Test
    void nodeChannelReplyAndApplicationPayloadRoundTrip() {
        assertEquals(
            ServiceWireConstants.FLAG_METADATA,
            codec.decodeHeader(codec.encodeNodeSendHeader(
                ServiceWireConstants.FLAG_METADATA)).flags());
        assertEquals(
            41,
            codec.decodeNodeRequestHeader(
                codec.encodeNodeRequestHeader(41, 0)));
        assertEquals(
            "orders",
            codec.decodeChannelSendHeader(
                codec.encodeChannelSendHeader("orders", 0)));
        assertEquals(
            new ZLinkServiceM6AWireCodec.ChannelRequest(42, "orders"),
            codec.decodeChannelRequestHeader(
                codec.encodeChannelRequestHeader(
                    42,
                    "orders",
                    ServiceWireConstants.FLAG_METADATA)));
        assertEquals(
            new ZLinkServiceM6AWireCodec.Reply(43, 0, 0),
            codec.decodeReplyHeader(codec.encodeReplyHeader(43, 0, 0)));
        assertEquals(
            new ZLinkServiceM6AWireCodec.Reply(44, 102, 9),
            codec.decodeReplyHeader(codec.encodeReplyHeader(44, 102, 9)));

        var payload = new ZLinkServiceM6AWireCodec.ApplicationPayload(
            "OrderPlaced",
            "application/zlink-framework-json-v1",
            new byte[] {1, 2, 3});
        var decoded = codec.decodeApplicationPayload(
            codec.encodeApplicationPayload(payload));
        assertEquals(payload.packetName(), decoded.packetName());
        assertEquals(payload.contentType(), decoded.contentType());
        assertArrayEquals(payload.payload(), decoded.payload());
    }

    @Test
    void malformedAndNonCanonicalRecordsAreRejected() {
        byte[] admission = codec.encodeAdmission(
            ServiceWireConstants.COMMAND_HELLO,
            descriptor(RoutingId.from("node-a")));
        assertThrows(
            ZLinkServiceWireException.class,
            () -> codec.decodeAdmission(
                truncated(admission),
                ServiceWireConstants.COMMAND_HELLO,
                RoutingId.from("node-a")));

        byte[] badMagic = codec.encodeNodeSendHeader(0);
        badMagic[0] = 0;
        assertThrows(
            ZLinkServiceWireException.class,
            () -> codec.decodeHeader(badMagic));
        assertThrows(
            ZLinkServiceWireException.class,
            () -> codec.encodeNodeRequestHeader(0, 0));
        assertThrows(
            ZLinkServiceWireException.class,
            () -> codec.encodeChannelRequestHeader(0, "orders", 0));
        assertThrows(
            ZLinkServiceWireException.class,
            () -> codec.decodeChannelRequestHeader(
                withTrailingByte(
                    codec.encodeChannelRequestHeader(1, "orders", 0))));
        assertThrows(
            ZLinkServiceWireException.class,
            () -> codec.encodeReplyHeader(1, 0, 9));
        assertThrows(
            ZLinkServiceWireException.class,
            () -> codec.encodeReplyHeader(1, 101, 9));
        assertThrows(
            ZLinkServiceWireException.class,
            () -> codec.decodeApplicationPayload(
                withTrailingByte(codec.encodeApplicationPayload(
                    new ZLinkServiceM6AWireCodec.ApplicationPayload(
                        "event",
                        "application/octet-stream",
                        new byte[] {1})))));
        byte[] invalidRejectReason = codec.encodeReject(1);
        invalidRejectReason[invalidRejectReason.length - 1] = 0;
        assertThrows(
            ZLinkServiceWireException.class,
            () -> codec.decodeReject(invalidRejectReason));
    }

    private static ZLinkServiceNodeDescriptor descriptor(RoutingId source) {
        return new ZLinkServiceNodeDescriptor(
            "mesh",
            source,
            7,
            11,
            "tcp://127.0.0.1:3001",
            List.of(
                new ZLinkServiceNodeDescriptor.Channel("chat", 50),
                new ZLinkServiceNodeDescriptor.Channel("orders", 100)),
            ZLinkServiceNodeDescriptor.State.SERVING,
            "service-a",
            4 * 1024 * 1024,
            3,
            List.of(
                ZLinkServiceNodeDescriptor.REQUIRED_CAPABILITY,
                "typed-json-v1"),
            ZLinkServiceNodeDescriptor.ObjectRole.SERVER,
            80,
            1000,
            100,
            4,
            2);
    }

    private static byte[] truncated(byte[] value) {
        return java.util.Arrays.copyOf(value, value.length - 1);
    }

    private static byte[] withTrailingByte(byte[] value) {
        return java.util.Arrays.copyOf(value, value.length + 1);
    }
}
