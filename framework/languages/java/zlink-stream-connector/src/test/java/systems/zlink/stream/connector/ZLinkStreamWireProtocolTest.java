package systems.zlink.stream.connector;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

import java.nio.charset.StandardCharsets;
import java.util.LinkedHashMap;
import java.util.Map;
import org.junit.jupiter.api.Test;

final class ZLinkStreamWireProtocolTest {
    @Test
    void headerProtocol_matchesDotnetAndNodeGoldenVector() {
        Map<String, String> metadata = new LinkedHashMap<>();
        metadata.put("trace", "abc");
        ZLinkStreamWireProtocol.Header header = new ZLinkStreamWireProtocol.Header(
            ZLinkStreamWireProtocol.KIND_REQUEST,
            ZLinkStreamWireProtocol.CODEC_JSON,
            ZLinkStreamWireProtocol.FLAG_HAS_REQUEST_SEQ
                | ZLinkStreamWireProtocol.FLAG_HAS_METADATA,
            7L,
            "Join",
            metadata);

        byte[] encoded = ZLinkStreamWireProtocol.encodeHeader(header);

        assertArrayEquals(hex(
                "02 01 03 00 00 00 00 00 00 00 07 04 4a 6f 69 6e "
                    + "00 0c 01 05 74 72 61 63 65 00 03 61 62 63"),
            encoded);

        ZLinkStreamWireProtocol.Header decoded = ZLinkStreamWireProtocol.decodeHeader(encoded);

        assertEquals(ZLinkStreamWireProtocol.KIND_REQUEST, decoded.kind());
        assertEquals(ZLinkStreamWireProtocol.CODEC_JSON, decoded.codec());
        assertEquals(7L, decoded.requestSeq());
        assertEquals("Join", decoded.name());
        assertEquals("abc", decoded.metadata().get("trace"));
    }

    @Test
    void frameProtocol_matchesDotnetAndNodePrefixLayout() {
        byte[] header = hex("01 00 00 05 52 65 61 64 79");
        byte[] payload = "{\"ok\":true}".getBytes(StandardCharsets.UTF_8);

        byte[] frame = ZLinkStreamWireProtocol.encodeFrame(header, payload, 64 * 1024);

        assertArrayEquals(hex("00 09 00 00 00 0b"), java.util.Arrays.copyOf(frame, 6));
        ZLinkStreamWireProtocol.Frame decoded = ZLinkStreamWireProtocol.decodeFrame(frame);
        assertArrayEquals(header, decoded.header());
        assertArrayEquals(payload, decoded.payload());
    }

    @Test
    void headerProtocol_rejectsRequestWithoutRequestSeq() {
        ZLinkStreamWireProtocol.Header header = new ZLinkStreamWireProtocol.Header(
            ZLinkStreamWireProtocol.KIND_REQUEST,
            ZLinkStreamWireProtocol.CODEC_JSON,
            ZLinkStreamWireProtocol.FLAG_HAS_METADATA,
            null,
            "Join",
            Map.of("trace", "abc"));

        assertThrows(IllegalArgumentException.class, () ->
            ZLinkStreamWireProtocol.encodeHeader(header));
    }

    private static byte[] hex(String value) {
        String compact = value.replace(" ", "");
        byte[] bytes = new byte[compact.length() / 2];
        for (int i = 0; i < bytes.length; i++) {
            bytes[i] = (byte) Integer.parseInt(compact.substring(i * 2, i * 2 + 2), 16);
        }
        return bytes;
    }
}
