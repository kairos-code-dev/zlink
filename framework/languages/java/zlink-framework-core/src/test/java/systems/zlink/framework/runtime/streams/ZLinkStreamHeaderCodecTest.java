package systems.zlink.framework.runtime.streams;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.EnumSet;
import java.util.Map;
import java.util.Optional;
import org.junit.jupiter.api.Test;
import systems.zlink.framework.streams.ZLinkStreamCodec;
import systems.zlink.framework.streams.ZLinkStreamMessageKind;

final class ZLinkStreamHeaderCodecTest {
    @Test
    void encodeDecodePreservesDotnetHeaderFields() {
        ZLinkStreamHeader header = new ZLinkStreamHeader(
            ZLinkStreamMessageKind.REQUEST,
            ZLinkStreamCodec.JSON,
            EnumSet.of(ZLinkStreamHeaderFlag.PAYLOAD_COMPRESSED),
            Optional.of(42L),
            "JsonRelayReq",
            Map.of("trace-id", "abc", "tenant", "sample"));

        ZLinkStreamHeader decoded =
            ZLinkStreamHeaderCodec.decodeOrPlain(ZLinkStreamHeaderCodec.encode(header));

        assertEquals(ZLinkStreamMessageKind.REQUEST, decoded.kind());
        assertEquals(ZLinkStreamCodec.JSON, decoded.codec());
        assertEquals(Optional.of(42L), decoded.requestSequence());
        assertEquals("JsonRelayReq", decoded.packetName());
        assertEquals(header.metadata(), decoded.metadata());
        assertTrue(decoded.flags().contains(ZLinkStreamHeaderFlag.HAS_REQUEST_SEQUENCE));
        assertTrue(decoded.flags().contains(ZLinkStreamHeaderFlag.HAS_METADATA));
        assertTrue(decoded.flags().contains(ZLinkStreamHeaderFlag.PAYLOAD_COMPRESSED));
    }

    @Test
    void encodeDecodePreservesCorrelationId() {
        ZLinkStreamHeader header = new ZLinkStreamHeader(
            ZLinkStreamMessageKind.SEND,
            ZLinkStreamCodec.JSON,
            EnumSet.noneOf(ZLinkStreamHeaderFlag.class),
            Optional.empty(),
            "MatchBingoReq",
            Map.of(),
            Optional.of("corr-java-bingo"));

        ZLinkStreamHeader decoded =
            ZLinkStreamHeaderCodec.decodeOrPlain(ZLinkStreamHeaderCodec.encode(header));

        assertEquals(ZLinkStreamMessageKind.SEND, decoded.kind());
        assertEquals(ZLinkStreamCodec.JSON, decoded.codec());
        assertEquals(Optional.empty(), decoded.requestSequence());
        assertEquals("MatchBingoReq", decoded.packetName());
        assertEquals(Optional.of("corr-java-bingo"), decoded.correlationId());
        assertTrue(decoded.flags().contains(ZLinkStreamHeaderFlag.HAS_CORRELATION_ID));
    }
}
